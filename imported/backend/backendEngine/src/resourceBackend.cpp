/**
* Copyright (c) 2013-2020 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"

#include "resourceBackend.h"

#include "configSessionDataProvider.h"
#include "exporter.h"
#include "factory.h"
#include "fallbackResourceEditor.h"
#include "importer.h"
#include "objectClipboardWindows.h"
#include "resourceMetadata.h"
#include "resourceImportMetadata.h"
#include "thumbnailGenerator.h"
#include "batchTool.h"
#include "previewPanel.h"
#include "tweakdbInteropHelper.h"
#include "prefabProxyReconciler.h"
#include "resourceDatabaseProvider.h"
#include "commandletUtils.h"
#include "baseSceneRecorder.h"
#include "bugFlagsService.h"
#include "environmentPresetsManager.h"

#include "../../../common/redReflection/include/deferredBufferMapper.h"
#include "../../../common/redReflection/include/pathResolver.h"
#include "../../../common/redReflection/include/serializationBinaryStructureMapper.h"
#include "../../../common/redReflection/include/packageCompiler.h"
#include "../../../common/redReflection/include/serializationBinaryUtilities.h"
#include "../../../common/redContainers/include/string/stringUtils.h"
#include "../../../common/world/include/worldPrefab.h"
#include "../../../common/world/include/worldNode.h"
#include "../../../common/worldEntities/include/entity.h"
#include "../../../common/worldEntities/include/entityTemplate.h"
#include "../../../common/worldEntities/include/appearanceResource.h"
#include "../../../common/worldEntities/include/appearanceDefinition.h"
#include "../../../common/generalEditorBackend/include/fileInfo.h"
#include "../../../common/generalEditorBackend/include/editorMessage.h"

#include "../../../common/interop/include/interopUtilsRTTI.h"
#include "../../../common/interop/include/resourceCommand.h"
#include "../../../common/redConfig/include/configVarSystem.h"
#include "../../../common/redReflection/include/resourceClassLookup.h"
#include "../../../common/redReflection/include/serializationSaver.h"
#include "../../../common/redReflection/include/streamedResourceManager.h"
#include "../../../common/redReflection/include/resourceMonitor.h"
#include "../../../common/redReflection/include/resourceLoaderThrottler.h"
#include "../../../common/resource/include/resourceSaver.h"
#include "../../../common/engine/include/ruidGenerator.h"

#ifndef RED_USE_RESOURCEPATH_STRINGS
#error The define RED_USE_RESOURCEPATH_STRINGS is incompatible with running the editor, check your settings!
#endif


using red::DynArray;

namespace Config
{
	// This should contain semicolon ';' separated wildcard strings, for example "*.prefab;*.terraintile". Can be empty, in which case disables auto backing up.
	TConfigVar< String > cvAutoBackupOnSaveFilesFilter( "Editor/Resources", "AutoBackupOnSaveFilesFilter", "", eConsoleVarFlag_Save );
	TConfigVar < Bool > cvValidationErrorsBypass( "Editor/Validation", "IgnoreErrors", false );
}

namespace
{
	const rtti::ClassType* FindResourceEditorClassByFileExtension( const String& fileExtension )
	{
		const rtti::ClassType* resourceClassType = ResourceClassByExtension( fileExtension );

		if( resourceClassType == nullptr )
		{
			RED_LOG_ERROR( "No const rtti::ClassType associated with file extension '%hs'", fileExtension.AsChar() );

			return nullptr;
		}

		const rtti::ClassType* resourceEditorClass = ClassID< tools::FallbackResourceEditor >();
		{
			DynArray< const rtti::ClassType* > resourceEditorClasses{ red::PoolBackend() };
			GetRttiSystem().EnumClasses( ClassID< tools::IResourceEditor >(), resourceEditorClasses );

			for ( const auto* ptr : resourceEditorClasses )
			{
				const auto* resourceEditor = ptr->GetDefaultObject< tools::IResourceEditor >();
				if ( resourceEditor->SupportsResourceClass( resourceClassType ) )
				{
					// find first that matches
					// TODO: we should find the editor that is a best match
					resourceEditorClass = ptr;
					break;
				}
			}
		}
		return resourceEditorClass;
	}
}

namespace tools
{
	namespace consts
	{
		const String sessionOpenedEditorsParamName = "OpenedEditors";
		const AnsiChar sessionArraySeparator = ';';
		const AnsiChar* workspotExtension = ".workspot";
		const AnsiChar* prefabExtension = ".prefab";
		const AnsiChar* entityExtension = ".ent";
	}

	namespace prv
	{
		//////////////////////////////////////////////////////////////////////////
		// ResModificationListener
		//////////////////////////////////////////////////////////////////////////
		class ResModificationListener : public res::IModificationListener
		{
		public:
			ResModificationListener( interop::InteropNotifier* modifiedNotifer, interop::InteropNotifier* unmodifiedNotifier, red::HashSet<String>* modifiedFiles )
				: m_notifyModified( modifiedNotifer )
				, m_notifyUnmodified( unmodifiedNotifier )
				, m_modifiedFilesList( modifiedFiles )
			{
			}

			virtual void OnModified( const res::ResourcePath& path ) override final
			{
				RED_SCOPE_LOCK( m_lock );
				if (m_modifiedFilesList->Insert(path.ToString()).IsSuccessful())
				{
					RED_LOG("Resource modified: [%s]", path.ToDebugString());
					m_notifyModified->Notify<String>(path.ToString());
				}
			}

			virtual void OnUnmodified(const res::ResourcePath& path) override final
			{
				RED_SCOPE_LOCK( m_lock );
				if (m_modifiedFilesList->Remove(path.ToString()).IsSuccessful())
				{
					RED_LOG("Resource unmodified: [%s]", path.ToDebugString());
					m_notifyUnmodified->Notify<String>(path.ToString());
				}
			}

		private:
			interop::InteropNotifier* m_notifyModified;
			interop::InteropNotifier* m_notifyUnmodified;
			red::HashSet<String>* m_modifiedFilesList;
			red::RWSpinLock m_lock;
		};
	}

	//////////////////////////////////////////////////////////////
	// CommandsHandler
	//////////////////////////////////////////////////////////////
	CommandsHandler::CommandsHandler( ResourceBackend* backend )
		: m_resourceCommands{ red::PoolBackend() }
	{
		m_commandFactory = red::CreateUniquePtr< ResourceCommandFactory >( backend );
	}

	ResourceCommandWrapperSharedPtr CommandsHandler::GetCommand( const res::ResourcePath& path )
	{
		auto op = m_resourceCommands.FindPtr( path );
		if ( op )
		{
			return *op;
		}
		else
		{
			return nullptr;
		}
	}

	void CommandsHandler::AddCommand( const res::ResourcePath& path, ResourceCommandWrapperSharedPtr& command )
	{
		m_resourceCommands[path] = command;
		m_cachedOptions = command->GetOptions();
	}

	void CommandsHandler::RemoveCommand( const res::ResourcePath& path )
	{
		m_resourceCommands.Remove( path );
	}


	namespace helper
	{
		template< typename T >
		RED_INLINE red::UniquePtr< T > CreateImpl()
		{
			red::DynArray< const rtti::ClassType* > types;
			rtti::ITypeSystem::GetInstance().EnumClasses(ClassID<T>(), types);
			if (!types.Empty())
			{
				const auto* classType = types[0];
				return red::UniquePtr< T >(classType->CreateObject<T>());
			}
			return nullptr;
		}
	}

	//////////////////////////////////////////////////////////////////////////
	// ResourceBackend
	//////////////////////////////////////////////////////////////////////////
	ResourceBackend::ResourceBackend( const ResourceBackendSetup& setup )
		: m_depot( setup.m_depot )
		, m_loader( setup.m_loader )
		, m_clipboard( red::CreateUniquePtr< ObjectClipboardWindows >() )
		, m_modificationListener( red::CreateUniquePtr< prv::ResModificationListener >( &m_fileModified, &m_fileUnmodified, &m_modifiedFiles ) )
		, m_ruidGenerator( red::CreateUniquePtr< RUIDGenerator >() )
		, m_envPresetsManager( red::CreateSharedPtr<EnvironmentPresetsManager>() )
		, m_editors{ red::PoolBackend() }
		, m_activeResourceEditorCreators{ red::PoolBackend() }
		, m_resourcesToReload{ red::PoolBackend() }
		, m_modifiedFiles{ red::PoolBackend() }
		, m_batchTool( nullptr )
		, m_requestRefreshResourceDatabase( false )
	{
		RED_FATAL_ASSERT( setup.m_depot != nullptr, "Resource depot is required to initialize resource backend." );
		RED_FATAL_ASSERT( setup.m_loader != nullptr, "Resource loader is required to initialize resource backend." );

		if ( m_loader->GetThrottler() )
		{
			// Slower loading, but hitching is reduced
			m_loader->GetThrottler()->SetLoadingSpeed( res::ThrottleLoadingSpeed::Stream );
		}

		res::MonitorRouter::GetInstance().RegisterModificationListener( m_modificationListener.Get() );

		if ( setup.m_viewportManager )
		{
			m_renderingWindowManager = red::CreateUniquePtr< RenderingWindowManager >( setup.m_viewportManager, setup.m_sinks );
		}

		m_commandsHandler = red::CreateUniquePtr< CommandsHandler >( this );

		m_serializableNotifier.SetCallback( [this]( const SerializableID& id, const rtti::AccessPath& path, const rtti::ValuePtr& oldValue, const rtti::ValuePtr& targetValue )
			{ OnNotifyPropertyChange( id, path ); } );

		class Facilitator : public IMessageLocationGotoFacilitator
		{
		public:
			Facilitator( ResourceBackend* resourceBackend ) : m_resourceBackend( resourceBackend ) {}

			virtual ~Facilitator() = default;

			virtual void Goto( const EditorObjectIDPath& path ) override
			{
				ResourceEditorPtr editor = m_resourceBackend->GetResourceEditorByPath( path.GetFirstChildID().GetResourcePath() );
				if ( editor )
				{
					editor->SelectObject( path );
				}
			}

			virtual void Goto( const String& weblink ) override
			{
				m_resourceBackend->OpenWebBrowser( weblink );
			}

		private:
			ResourceBackend* m_resourceBackend;
		};

		tools::IMessageReporter::SetReporter( &m_messageLog );
		m_messageLogFacilitator = red::CreateSharedPtr< Facilitator >( this );
		m_messageLog.SetFacilitator( m_messageLogFacilitator );

		m_shortcutBackend = red::CreateSharedPtr< ShortcutInteropBackend >();
		m_tweakDbInteropHelper = red::CreateUniquePtr< TweakDbInteropHelper >();

		m_prefabProxyReconciler = helper::CreateImpl<IPrefabProxyReconciler>();
		RED_FATAL_ASSERT(m_prefabProxyReconciler);

		ForceRefreshResourceDatabase();
		job::FlushCounter(m_resourceDatabaseCounter);

		// HACK: can't reach scene backend, need it to be exposed here
		BaseSceneRecorder* sceneRecorder = GetRttiSystem().FindClass( RED_NAME_CONSTEXPR_NOREG( "scnbSceneRecorder" ) )->CreateObject< BaseSceneRecorder >();
		sceneRecorder->SetResourceBackend( this );
		m_sceneRecorderBackend = red::SharedPtr< BaseSceneRecorder >( sceneRecorder );

		m_bugFlagsService = red::CreateSharedPtr< BugFlagsService >( this );
	}

	ResourceBackend::~ResourceBackend()
	{
		if ( m_modificationListener )
		{
			res::MonitorRouter::GetInstance().UnregisterModificationListener( m_modificationListener.Get() );
		}

		job::FlushCounter(m_resourceDatabaseCounter);
		if (m_resourceDatabase)
		{
			m_resourceDatabase->Shutdown();
		}

		// reset all the thumbnail generators to avoid memory leaks
		res::IThumbnailGenerator::ResetAllGenerators();
	}

	void ResourceBackend::AttachListener( IResourceBackendListener* listener )
	{
		red::alg::PushBackUnique( m_listeners, listener );
	}

	void ResourceBackend::DetachListener( IResourceBackendListener* listener )
	{
		m_listeners.Remove( listener );
	}

	void ResourceBackend::UpdateAndRender( Float timeDelta, const tick::Info& tickInfo )
	{
		// TODO: any global updates of the whole resource backend - reloading?
		{
			PC_SCOPE( ResourceBacked_ReloadDelayedResources );
			ReloadDelayedResources();
		}

		{
			PC_SCOPE( ResourceBackend_UpdateEditorCreators );
			UpdateEditorCreators( timeDelta );
		}

		// update and render rendering panels
		{
			PC_SCOPE( ResourceBackend_UpdateAndRender );
			if ( m_renderingWindowManager )
				m_renderingWindowManager->UpdateAndRender( timeDelta, tickInfo );
		}

		// tick registered functions
		{
			PC_SCOPE( ResourceBackend_TickFunctions );
			red::ScopedLock< TLock > lock( m_tickFunctionsLock );
			for ( auto& fun : m_tickFunctions.Values() )
			{
				fun( timeDelta, tickInfo );
			}
		}

		{
			PC_SCOPE(ResourceBackend_TickResourceDatabase);

			if (m_refreshResourceDatabaseFinished.Exchange(false))
			{
				m_resourceDatabase = std::move(m_pendingNewResourceDatabase);
				RED_FATAL_ASSERT(m_resourceDatabase);
				m_resourceDatabaseRefreshedNotifier.Notify();
			}

			if ( m_requestRefreshResourceDatabase )
			{
				const Bool result = TryRefreshResourceDataBase();
				m_requestRefreshResourceDatabase = !result;
			}
		}

		{
			PC_SCOPE( ResourceBackend_SceneRecorder );
			if ( m_sceneRecorderBackend )
			{
				m_sceneRecorderBackend->Update();
			}
		}
	}

	void ResourceBackend::ReloadDelayedResources()
	{
		for ( const auto& resourcePath : m_resourcesToReload )
		{
			// Give a best effort to reload the resource
			// The function will handle not-loaded resources correctly.
			ReloadResource( resourcePath );
		}
		m_resourcesToReload.Clear();

		Uint32 reloadedResources = 0u;
		for ( const auto& token : m_pendingReloadRequests )
		{
			if ( !token->IsLoaded() )
			{
				break;
			}

			FinishReloadingResource( token );
			++reloadedResources;
		}
		m_pendingReloadRequests.RemoveAt( 0u, reloadedResources );
	}

	void ResourceBackend::ForceRefreshResourceDatabase(Bool readonly /*= true*/)
	{
		RED_FATAL_ASSERT(m_resourceDatabaseCounter.Internal_IsZeroSnapshot());

		m_pendingNewResourceDatabase = helper::CreateImpl< resdb::IResourceDatabaseProvider >();
		RED_FATAL_ASSERT(m_pendingNewResourceDatabase);
		const auto resourceDatabasePath = m_pendingNewResourceDatabase->GetDefaultFilePath();

		if (readonly)
		{
			m_pendingNewResourceDatabase->SetReadOnly(true);
		}

		m_refreshResourceDatabaseFinished.SetValue(false);

		job::Counter loadingCounter{ job::Priority::Latent };
		m_pendingNewResourceDatabase->Initialise(*res::GetEditorDepot(), resourceDatabasePath, loadingCounter);
		job::Builder builder{ job::Priority::Latent };
		builder.DispatchWait(loadingCounter);
		builder.DispatchJob("NotifyLoadingFinished", [this](const job::RunContext&) {
			m_refreshResourceDatabaseFinished.SetValue(true);
		});
		m_resourceDatabaseCounter += builder.ExtractWaitCounter();
	}

	void ResourceBackend::UpdateResourceDatabaseSync()
	{
		// Flush any previous update
		job::FlushCounter(m_resourceDatabaseCounter);

		ForceRefreshResourceDatabase(false);

		// Flush current update
		job::FlushCounter(m_resourceDatabaseCounter);

		const Bool ready = m_refreshResourceDatabaseFinished.Exchange(false);
		RED_FATAL_ASSERT(ready);

		m_resourceDatabase = std::move(m_pendingNewResourceDatabase);
		RED_FATAL_ASSERT(m_resourceDatabase);
	}

	Bool ResourceBackend::TryRefreshResourceDataBase()
	{
		if (!m_resourceDatabaseCounter.Internal_IsZeroSnapshot())
		{
			return false;
		}

		ForceRefreshResourceDatabase();
		return true;
	}

	red::AbsolutePath ResourceBackend::GetResourceDatabaseFilePath() const
	{
		return m_resourceDatabase ? m_resourceDatabase->GetDefaultFilePath() : red::AbsolutePath();
	}

	void ResourceBackend::UpdateEditorCreators(Float timeDelta)
	{
		red::DynArray<ResourceEditorCreatorPtr> creatorsToRemove{ red::PoolBackend() };

		for( auto& helper : m_activeResourceEditorCreators )
		{
			if( helper->Update( timeDelta ) == true )
			{
				creatorsToRemove.PushBack( helper );
			}
		}

		for( auto& helper : creatorsToRemove )
		{
			m_activeResourceEditorCreators.Remove( helper );
		}
	}

	void ResourceBackend::ProcessInput( input::IInputContext& context )
	{
		// TODO: any global updates of the whole resource backend - sub-window system for consoles ?

		// dispatch to the rendering panels
		if ( m_renderingWindowManager )
			m_renderingWindowManager->ProcessInput( context );
	}

	void ResourceBackend::ProcessMessages( const red::DynArray<ExternalMessage>& messages )
	{
		if( m_renderingWindowManager )
			m_renderingWindowManager->ProcessMessages(messages);
	}

	//--------------

	void ResourceBackend::NotifyResourceReloaded(const res::ResourcePath& path, const THandle<CResource>& newResource)
	{
		// notify general public, those messages will be processed in various systems
		// this is used to force 2-nd and 3-rd order dependencies to refresh: ie. MeshComponent should recreate the rendering proxy when mesh changes
		res::MonitorUserDataPtr userData( RED_NEW( res::TMonitorUserData<THandle<CResource>> )(newResource));
		res::MonitorRouter::GetInstance().QueueChange(path, RED_NAME_CONSTEXPR("reload"), userData);

		// update streaming data
		if ( auto* streamingManager = GetStreamingResourceManager() )
		{
			streamingManager->RefreshStreamingData( path );
		}
	}

	Bool ResourceBackend::CreateResource( const res::ResourcePath& path, const String& optionString )
	{
		// lookup resource class for given extension
		const rtti::ClassType* resourceClass = res::prv::ResourceClassLookup::GetInstance().GetResourceClassForFile( path );
		if ( !resourceClass )
		{
			RED_LOG_ERROR( "Core: Unable to create '%hs' because it's using unknown extension", path.ToString().AsChar() );
			return nullptr;
		}

		// CHECK: request file - it will be loaded if it exists
		// TODO: causes issues when importing, need to fix that
		/*{
		auto token = GResourceLoader->IssueLoadingRequest( path );
		if ( token->WaitUntilLoaded() )
		{
		RED_LOG_ERROR( "Core: Unable to create '%hs' because it already exists"), path.ToString().AsChar() );
		return nullptr;
		}
		}*/

		// find resource factory for given extension (we are not allowed to create resources without a factory)
		auto* factory = IFactory::FindFactory( resourceClass );
		if ( nullptr == factory )
		{
			RED_LOG_ERROR( "Core: Unable to create '%hs' because there's no factory for resource '%hs'", path.ToString().AsChar(), resourceClass->GetName().AsChar() );
			return nullptr;
		}

		// create an empty resource
		THandle< CResource > res = resourceClass->CreateHandle< CResource >();
		if ( !res )
		{
			RED_LOG_ERROR( "Core: Unable to create '%hs' because resource '%hs' could not be created", path.ToString().AsChar(), resourceClass->GetName().AsChar() );
			return nullptr;
		}

		// use the resource factory to create a new resource
		THandle< FactoryOptions > options = factory->CreateOptions( path );
		if ( !factory->DoCreate( res, options ) )
		{
			RED_LOG_ERROR( "Core: Unable to create '%hs' because factory for resource '%hs' failed", path.ToString().AsChar(), resourceClass->GetName().AsChar() );
			return nullptr;
		}

		res::ResourceSavingOptions savingOptions;
		savingOptions.m_depot = m_depot;
		savingOptions.m_targetPath = path;
		return res::SaveResource( res, savingOptions );
	}

	Bool ResourceBackend::CreateResourceEditor( const res::ResourcePath& path, const String& editorName, const String& args )
	{
		auto existingEditor = FindExistingEditorForResource( path );
		if( existingEditor != nullptr )
		{
			existingEditor->SendActivateNotification();
			return true;
		}

		return CreateResourceEditorCreator( path, editorName, args );
	}

	ResourceEditorPtr ResourceBackend::FindExistingEditorForResource( const res::ResourcePath& path ) const
	{
		for ( const auto& editor : m_editors )
		{
			if ( editor->GetPath() == path )
				return editor;
		}
		return ResourceEditorPtr();
	}

	Bool ResourceBackend::CreateResourceEditorCreator( const res::ResourcePath& path, const String& editorName, const String& args )
	{
		// find matching resource editor class
		const rtti::ClassType* resourceEditorClass = FindResourceEditorClassByFileExtension( red::utils::ExtractExtension( path.ToStringView() ) );

		// no editor
		if ( resourceEditorClass == nullptr )
		{
			RED_LOG_ERROR( "Core: Failed to create resource editor for '%hs': unknown resource class", path.ToDebugString() );
			return false;
		}

		/// check currently loading resources
		for( auto& creator : m_activeResourceEditorCreators )
		{
			if( creator->GetResourcePath() == path )
			{
				return true;
			}
		}

		ResourceEditorCreatorPtr creator( RED_NEW( ResourceEditorCreator )( this, resourceEditorClass, path, editorName, args ) );
		m_activeResourceEditorCreators.PushBack( creator );
		return true;
	}

	ResourceEditorPtr ResourceBackend::GetResourceEditorByPath( const res::ResourcePath& path ) const
	{
		red::ScopedLock< TLock > lock(m_lock);

		// linear search
		for ( const auto& editor : m_editors )
		{
			if ( editor->IsResourceOpened( path ) )
			{
				return editor;
			}
		}

		// editor not found
		return ResourceEditorPtr();
	}

	ResourceEditorPtr ResourceBackend::GetResourceEditorByName( const String& name ) const
	{
		red::ScopedLock< TLock > lock(m_lock);

		// linear search
		for ( const auto& editor : m_editors )
		{
			if ( editor->GetName() == name )
			{
				return editor;
			}
		}

		// editor not found
		return ResourceEditorPtr();
	}

	void ResourceBackend::GetResourceEditors( red::DynArray< ResourceEditorPtr >& editors ) const
	{
		editors = m_editors;
	}

	void ResourceBackend::GetResourceEditorsByResourceType( const rtti::ClassType* resourceType, red::DynArray< ResourceEditorPtr >& editors ) const
	{
		red::ScopedLock< TLock > lock( m_lock );

		for ( const auto& editor : m_editors )
		{
			if ( editor->GetResource()->IsA( resourceType ) )
			{
				editors.PushBack( editor );
			}
		}
	}

	void ResourceBackend::GetModifiedFiles( DynArray< res::ResourcePath >& outModifiedFiles ) const
	{
		for ( const auto& editor : m_editors )
		{
			editor->GetModifiedResources( outModifiedFiles );
		}
	}

	void ResourceBackend::GetOpenedFiles( DynArray< res::ResourcePath >& outOpenedFiles ) const
	{
		for ( const auto& editor : m_editors )
		{
			editor->GetOpenedResources( outOpenedFiles );
		}
	}

	void ResourceBackend::CloseAllEditors()
	{
		TResourceEditors editors = m_editors;
		for ( const auto& editor : editors )
		{
			editor->Close();
		}

		m_lastNodeSelection.Clear();
		m_editors.Clear();

		NotifyAllEditorsClosed();
	}

	void ResourceBackend::PreCloseEditor( TResourceEditors::iterator iter )
	{
		ResourceEditorPtr editor = *iter;

		if ( m_lastNodeSelection.GetEditorName() == editor->GetName() )
		{
			m_lastNodeSelection.Clear();
		}

		Config::GetConfigSystem().Save();

		editor->GetModifiedResources( m_resourcesToReload );
		editor->SendCloseNotification();
	}

	void ResourceBackend::CloseEditor( TResourceEditors::iterator iter )
	{
		ResourceEditorPtr editor = *iter;

		m_editors.Remove( iter );

		NotifyEditorClosed( editor->GetName() );
		// The editor should be destroyed when exiting this function
	}

	void ResourceBackend::PreCloseEditorByName( const String& name )
	{
		auto iter = std::find_if( m_editors.Begin(), m_editors.End(),
			[ &name ] ( const ResourceEditorPtr& editor ) -> bool { return editor->GetName() == name; } );
		if ( iter != m_editors.End() )
		{
			PreCloseEditor( iter );
		}
	}

	void ResourceBackend::CloseEditorByName( const String& name )
	{
		auto iter = std::find_if( m_editors.Begin(), m_editors.End(),
			[&name]( const ResourceEditorPtr& editor ) -> bool { return editor->GetName() == name; } );
		if ( iter != m_editors.End() )
		{
			CloseEditor( iter );
		}
	}

	void ResourceBackend::CloseEditorByPath( const res::ResourcePath& path )
	{
		auto iter = std::find_if( m_editors.Begin(), m_editors.End(),
			[&path]( const ResourceEditorPtr& editor ) -> bool { return editor->GetPath() == path; } );
		if ( iter != m_editors.End() )
		{
			CloseEditor( iter );
		}
	}

	Bool ResourceBackend::SaveResourceAs( const res::ResourcePath& srcPath, const res::ResourcePath& dstPath, Bool allowOverride )
	{
		// resource already exists, abort
		if ( !allowOverride && m_depot->ResourceExists( dstPath ) )
			return false;

		// load the resource, if it's already loaded we will just get the reference
		auto res = m_loader->IssueLoadingRequest( srcPath )->WaitUntilLoaded();
		if ( !res )
			return false;

		// save the resource
		return SaveResourceAs( res, dstPath, allowOverride );
	}

	Bool ResourceBackend::SaveResourceAs( const THandle< CResource >& srcRes, const res::ResourcePath& dstPath, Bool allowOverride )
	{
		// resource already exists, abort
		if ( !allowOverride && m_depot->ResourceExists( dstPath ) )
		{
			RED_LOG( "Core: Resource already exists '%hs'", dstPath.ToDebugString() );
			return false;
		}

		res::ResourceSavingOptions options;
		options.m_depot = m_depot;
		options.m_targetPath = dstPath;

		return res::SaveResource( srcRes, options );
	}

	Bool ResourceBackend::SaveResourceCopy( const THandle<CResource>& srcRes, const red::AbsolutePath& dstPath, Bool allowOverride /* = false */ )
	{
		if ( !allowOverride && GFileManager->FileExist( dstPath ) )
		{
			RED_LOG( "Core: File already exists '%hs'", dstPath.AsChar() );
			return false;
		}

		if ( !srcRes || !dstPath.IsFilePath() )
		{
			RED_LOG_ERROR( "Core: No resource to save or invalid destination '%hs'", dstPath.AsChar() );
			return false;
		}

		Uint64 fileSize = 0;
		{
			red::SharedPtr< IFile > filePtr( GFileManager->CreateFileWriter( dstPath, FOF_AbsolutePath | FOF_SafeWrite ) );
			if ( !filePtr )
			{
				RED_LOG_ERROR( "Core: Cannot create file writer for '%hs'", dstPath.AsChar() );
				return false;
			}

			// create dependency saver
			const red::String fileExtension = red::utils::ExtractExtension( srcRes->GetPath().ToStringView() );
			auto saver = serialization::ISaver::CreateSaver( fileExtension.AsChar() );
			if ( !saver )
			{
				filePtr->HandleIOError( "Cannot create dependency saver, extension '%hs' is not recognized.", fileExtension.AsChar() );
				return false;
			}

			// save the file
			serialization::SavingContext context( srcRes );
			if ( !saver->SaveObjects( *filePtr, context ) )
			{
				filePtr->HandleIOError( "Failed to serialize objects into the file" );
				return false;
			}

			// get the size of the stored data
			fileSize = filePtr->GetSize();
		}

		// resource was saved, log it
		RED_LOG( "Core: File '%hs' was saved, %llu bytes", dstPath.AsChar(), fileSize );
		return true;
	}

	Bool ResourceBackend::IsResourceLoaded(const res::ResourcePath& path) const
	{
		// note: for the purpose of the editor we really don't care if the resource is loaded fully or just started
		return m_loader->IsResourceLoaded(path) || m_loader->IsResourceLoading(path);
	}

	Bool ResourceBackend::IsResourceModified(const res::ResourcePath& path) const
	{
		return false;
		//return m_loader->IsModified(path);
	}

	Bool ResourceBackend::IsResourceOpened( const res::ResourcePath & path ) const
	{
		for ( const auto& editor : m_editors )
		{
			if ( editor->IsResourceOpened( path ) )
			{
				return true;
			}
		}
		return false;
	}

	Bool ResourceBackend::IsResourceReadOnly( const res::ResourcePath & path ) const
	{
		return m_depot->ResourceIsReadOnly( path );
	}

	Bool ResourceBackend::ResourceExists( const res::ResourcePath& path ) const
	{
		return m_depot->ResourceExists( path );
	}

	namespace
	{
		Bool c_autoBackupFiltersReady = false;
		DynArray< String > c_autoBackupFilters{ red::PoolBackend() };

		Bool ShouldFileBeAutoBackedUp( const res::ResourcePath& resourcePath )
		{
			red::StringView resourcePathStr = resourcePath.ToStringView();

			if ( !c_autoBackupFiltersReady )                      // Assuming SaveResource will not be called from multiple threads, no locking here.
			{
				c_autoBackupFilters = Config::cvAutoBackupOnSaveFilesFilter.Get().Split( ";", false );
				c_autoBackupFiltersReady = true;
			}

			for ( const String& filter : c_autoBackupFilters )
			{
				if ( red::GlobMatch( resourcePathStr, filter ) )
				{
					return true;
				}
			}

			return false;
		}
	}

	Bool ResourceBackend::SaveResource( const res::ResourcePath& path, Bool updateThumbnail )
	{
		// Only allow saving of already loaded resources
		auto res = m_loader->IssueLoadingRequest( path )->WaitUntilLoaded();
		if ( !res )
		{
			return false;
		}

		// save the resource
		return SaveResource( res, updateThumbnail );
	}

	Bool ResourceBackend::SaveResource( const THandle< CResource >& res, Bool updateThumbnail )
	{
		if ( !res )
		{
			RED_LOG_ERROR( "Trying to save null resource!" );
			return false;
		}

		const res::ResourcePath& resourcePath = res->GetPath();

		// Handle auto backup.
		if ( !Config::cvAutoBackupOnSaveFilesFilter.Get().Empty() && ShouldFileBeAutoBackedUp( resourcePath ) )
		{
			res::ResourceEditorDepot* editorDepot = res::GetEditorDepot();
			RED_FATAL_ASSERT( editorDepot != nullptr, "This should never be called from outside editor" );

			red::AbsolutePath absolutePath = editorDepot->TranslateResourcePath( resourcePath );
			red::AbsolutePath absoluteBakPath = absolutePath.AddFilePath( ".bak" );

			Bool result = GFileManager->CopyFile( absolutePath, absoluteBakPath, true );
			if ( !result )
			{
				RED_LOG_ERROR( "Unable to create backup file when saving resource '%hs'", resourcePath.ToDebugString() );
			}
		}

		// Regenerate thumbnail on save
		// Do this regardless if the save itself succeeds
		if ( updateThumbnail && !res::UpdateThumbnailInResource( res, *m_envPresetsManager ) )
		{
			RED_LOG_ERROR( "Core: Failed to update the thumbnail in the resource for '%hs'", resourcePath.ToDebugString() );
		}

		SaveTags( res, resourcePath );
		SaveAnimationTags( res, resourcePath );

		res::ResourceSavingOptions options;
		options.m_depot = m_depot;
		const Bool result = res::SaveResource( res, options );
		if ( !result )
		{
			return false;
		}

		// Saving the thumbnail in the cache must happen after the resource is saved
		if ( updateThumbnail && !res::UpdateThumbnailInCache( res, CName::NONE(), m_thumbnailGenNotifier ) )
		{
			RED_LOG_ERROR( "Core: Failed to update the thumbnail in the cache for resource '%hs'", resourcePath.ToDebugString() );
		}

		// Update the GPU size value.
		m_resourcePathToGPUSize.Remove( resourcePath );
		m_resourceSaved.Notify( resourcePath.ToString() );

		return true;
	}

	Bool ResourceBackend::ReloadResource( const res::ResourcePath& path )
	{
		// note: this is not 100% race-condition free but it's very hard to do it as such in the editor.
		// to be perfectly sure that it is we would have to FLUSH ALL JOBS and make sure that are no pending resource.

		// TODO: we don't even have a way to flush all jobs ATM...

		// get the loaded resource, if it's not loaded we are done
		auto loadedRes = m_loader->TryAcquiringLoadedResource(path);
		if (!loadedRes)
		{
			// reload the resource if it's monitored, because something may want to use it
			if ( !res::MonitorRouter::GetInstance().IsResourceMonitored( path ) )
			{
				RED_LOG( "Core: Reloading file '%hs' skipped because file is not loaded nor monitored.", path.ToDebugString() );
				return true;
			}
		}
		else
		{
			// The previous instance of the resource was (probably?) modified so we need to make sure that any
			// associated objects interested in the modification state know that it has been reloaded
			loadedRes->UnmarkModified();

			// Load the deferred data buffers here to close all references to the open file handle of the resource
			// before we attempt to reload. If we don't do this then loading will succeed, but we will probably leak
			// the handles elsewhere.
			// TODO: Implement a way to abandon DDBs rather than having to load them for this situation.
			serialization::DeferredDataBufferLoader bufferLoader;
			bufferLoader.Run( loadedRes );
		}

		// log, in case we crash...
		RED_LOG("Core: Reloading file '%hs' started. Keep your fingers crossed.", path.ToDebugString());
		RED_LOG_FLUSH();

		// load new version of the file, from now on the new version will be returned when somebody requests the file
		const auto param = res::IssueLoadingRequestParameter{ path };

		m_pendingReloadRequests.PushBack( m_loader->IssueReloadingRequest( param ) );
		return true;
	}

	Bool ResourceBackend::FinishReloadingResource( const res::ResourceTokenHandle& token )
	{
		THandle< CResource > newResource = token->GetResource();
		const res::ResourcePath path = token->GetPath();

		if ( !newResource )
		{
			RED_LOG_ERROR( "Core: Reloading of file '%hs' failed. The new version cannot be loaded", path.ToDebugString() );
			return false;
		}

		// Remove the cached metadata for the reloaded file (so that it will be loaded afresh next time it's accessed)
		m_metadata.Remove( path );

		// log, in case we crash...
		RED_LOG( "Core: Reloading file '%hs' got new file loaded, notifying the universe...", path.ToDebugString() );
		RED_LOG_FLUSH();

		// notify the resource editors that new file has been loaded
		NotifyResourceReloaded( path, newResource );

		// we did it
		RED_LOG( "Core: Reloading file '%hs' finished", path.ToDebugString() );
		RED_LOG_FLUSH(); // flush in case we did not crash so we have a proof :)
		return true;
	}

	void ResourceBackend::FinishLoadingResource( const res::ResourcePath& path )
	{
		THandle< CResource > loadedRes = m_loader->TryAcquiringLoadedResource( path );
		if ( !loadedRes )
		{
			RED_LOG( "Core: Loading deferred data buffers of file '%hs' skipped because file is not loaded.", path.ToDebugString() );
			return;
		}
		else
		{
			serialization::DeferredDataBufferLoader bufferLoader;
			bufferLoader.Run( loadedRes );
		}
	}

	RenderingWindowManager* ResourceBackend::GetRenderingWindowManager() const
	{
		RED_FATAL_ASSERT( m_renderingWindowManager != nullptr );
		return m_renderingWindowManager.Get();
	}

	res::IBackendResourceDepot* ResourceBackend::GetResourceDepot() const
	{
		return m_depot;
	}

	res::ResourceLoader* ResourceBackend::GetResourceLoader() const
	{
		return m_loader;
	}

	IObjectClipboard* ResourceBackend::GetClipboard() const
	{
		return m_clipboard.Get();
	}

	Bool ResourceBackend::GenerateIcon(const res::ResourcePath& resourcePath, const red::AbsolutePath& outPath)
	{
		if (!resourcePath.IsValid())
		{
			m_thumbnailGenNotifier.Notify(String("Error: Invalid resource path"));
			return false;
		}

		const red::StringView extension = ::red::utils::GetExtension(resourcePath.ToStringView());
		if (extension == "link")
		{
			m_thumbnailGenNotifier.Notify(String("Error: Not a resource"));
			return false;
		}

		TResRef< CResource > resRef(resourcePath);
		THandle< CResource > resource = resRef.Get();
		if (!resource)
		{
			RED_LOG_ERROR("Failed to open resource '%hs' for icon generation", resourcePath.ToDebugString());
			return false;
		}

		const auto filename = red::paths::ExtractFileStem(resource->GetPath().ToStringView());
		const auto appearanceName = CName::NONE().AsChar(); // Forces default appearance to be used.
		red::AbsolutePath outFile = outPath;
		outFile.AppendFilePath(filename + "_" + appearanceName + ".png");

		return res::GenerateIcon(resource, *m_envPresetsManager, outFile);
	}

	Bool ResourceBackend::GenerateThumbnail( const res::ResourcePath& resourcePath, Bool forceUpdate, CName prefferedAppearanceName )
	{
		if ( !resourcePath.IsValid() )
		{
			m_thumbnailGenNotifier.Notify( String( "Error: Invalid resource path" ) );
			return false;
		}

		{
			const red::StringView extension = ::red::utils::GetExtension( resourcePath.ToStringView() );
			if ( extension == "link" )
			{
				m_thumbnailGenNotifier.Notify( String( "Error: Not a resource" ) );
				return false;
			}
		}

		if ( !forceUpdate )
		{
			if ( res::IsThumbnailInCacheUpToDate( resourcePath, prefferedAppearanceName ) )
			{
				RED_LOG( "Thumbnail for %hs is up-to-date", resourcePath.ToDebugString() );
				m_thumbnailGenNotifier.Notify( resourcePath.ToString() );
				return true;
			}
		}

		if ( !res::CanGenerateThumbnail( resourcePath ) )
		{
			RED_LOG( "No thumbnail can be generated for %hs", resourcePath.ToDebugString() );
			m_thumbnailGenNotifier.Notify( String( "Error: No thumbnail possible" ) );
			// Even through no thumbnail can be generated, therefore we should return true, we return false
			// so that the default thumbnail gets used
			return false;
		}

		RED_LOG( "Updating thumbnail for %hs...", resourcePath.ToDebugString() );

		TResRef< CResource > resRef( resourcePath );
		THandle< CResource > resource = resRef.Get();
		if ( !resource )
		{
			RED_LOG_ERROR( "Failed to open resource '%hs' for thumbnail generation", resourcePath.ToDebugString() );
			return false;
		}

		if ( forceUpdate || ( !res::IsThumbnailInResourceUpToDate( resource ) && res::ShouldAutoGenerateThumbnail( resourcePath ) ) )
		{
			// NOTE: Storing the modified state here as if we fail to save the resource with an updated thumbnail we need
			// to revert its modified state back to what it was so that saving in editors works correctly. CYB-257581
			const bool wasModified = resource->IsModified();

			res::ResourceMetadata::ClearThumbnail( resource );

			if ( !res::UpdateThumbnailInResource( resource, *m_envPresetsManager, prefferedAppearanceName ) )
			{
				RED_LOG_ERROR( "Failed to update the thumbnail in the resource for '%hs'", resourcePath.ToDebugString() );
				if ( !wasModified )
				{
					resource->UnmarkModified();
				}
				return false;
			}

			res::ResourceSavingOptions options;
			options.m_depot = m_depot;
			if ( !res::SaveResource( resource, options ) )
			{
				RED_LOG_ERROR( "Failed to save the resource after generating thumbnails, thumbnail in resource '%hs' not updated!", resourcePath.ToDebugString() );
				if ( !wasModified )
				{
					resource->UnmarkModified();
				}
			}
		}

		if ( !res::UpdateThumbnailInCache( resource, prefferedAppearanceName, m_thumbnailGenNotifier ) )
		{
			RED_LOG_ERROR( "Failed to update the thumbnail in the cache for resource '%hs'", resourcePath.ToDebugString() );
			return false;
		}

		return true;
	}

	red::SharedPtr< EnvironmentPresetsManager > ResourceBackend::GetEnvironmentPresetsManager()
	{
		return m_envPresetsManager;
	}

	void ResourceBackend::FindDepotSubDirectories( const String& searchDepotPath, DynArray< String >& subDirectoryNames ) const
	{
		m_depot->FindDepotSubDirectories( searchDepotPath, subDirectoryNames );
	}

	void ResourceBackend::FindDepotFiles( const String& searchDepotPath, const AnsiChar* pattern, DynArray< String >& fileNames ) const
	{
		m_depot->FindDepotFiles( searchDepotPath, pattern, fileNames );
	}

	Bool ResourceBackend::RemoveDepotFile( const res::ResourcePath& depotPath )
	{
		CloseEditorByPath( depotPath );

		return m_depot->RemoveResource( depotPath );
	}

	//-------

	void ResourceBackend::SetLastNodeSelection( IResourceEditor* resourceEditor, const EditorObjectIDPath& selectedNode )
	{
		red::ScopedLock< TLock > lock( m_lock );
		const String& editorName = resourceEditor->GetName();
		m_lastNodeSelection = LastNodeSelection( editorName, selectedNode );
	}

	//-------

	void ResourceBackend::AddNewEditor(ResourceEditorPtr newEditor)
	{
		m_editors.PushBack(newEditor);

		NotifyEditorOpened( newEditor->GetName() );
	}

	void ResourceBackend::NotifyEditorMessage( const String& editorName, const String& message )
	{
		RED_LOG_INFO( "[Backend][NotifyEditorMessage] %s: %s", editorName.AsChar(), message.AsChar() );
		m_editorMessage.Notify( EditorMessage{ editorName, message } );
	}

	void ResourceBackend::NotifyNewEditorCreationFailReason( const String& resource, const String& reason )
	{
		// TODO this is temporary, as we don't have a proper way to notify the frontend about resource editor creation fail reason

		red::DynArray< String > data;
		data.PushBack( resource );
		data.PushBack( reason );

		m_newEditorCreationFailReason.Notify( data );
	}

	void ResourceBackend::RegisterForTick( const void* owner, TickFunction tickFunction )
	{
		red::ScopedLock< TLock > lock( m_tickFunctionsLock );
		m_tickFunctions[ owner ] = tickFunction;
	}

	void ResourceBackend::UnregisterForTick( const void* owner )
	{
		red::ScopedLock< TLock > lock( m_tickFunctionsLock );
		m_tickFunctions.Remove( owner );
	}

	void ResourceBackend::OnDataError( const red::String& dataErrorAction )
	{
		m_dataErrorNotifier.Notify( dataErrorAction );
	}

	void ResourceBackend::OpenWebBrowser( const red::String& link ) const
	{
		m_openWebBrowserNotifier.Notify( link );
	}

	void ResourceBackend::SaveTags( const THandle< CResource >& res, const res::ResourcePath& path )
	{
		// The metadata as it exists currently in the resource
		auto current = res::ResourceMetadata::Get( path, true );

		// The metadata as has been presented and (potentially) modified by the user
		Metadata* potential = m_metadata.FindPtr( path );

		if ( !potential )
			return;

		// Nothing has been added, so don't save anything
		if ( !current && potential->metadata->GetTags().Size() == 0 )
			return;

		res::ResourceMetadata::SetTags( res, potential->metadata->GetTags() );
	}

	void ResourceBackend::SaveAnimationTags( const THandle< CResource >& res, const res::ResourcePath& path )
	{
		// The metadata as it exists currently in the resource
		auto current = res::ResourceMetadata::Get( path, true );

		// The metadata as has been presented and (potentially) modified by the user
		Metadata* potential = m_metadata.FindPtr( path );

		if ( !potential )
			return;

		// Nothing has been added, so don't save anything
		if ( !current && !potential->metadata->HasAnimationMetadata() )
			return;

		res::ResourceMetadata::SetAnimationMetadata( res, potential->metadata->GetAnimationMetadata() );
	}

	ResourceBackend::Metadata* ResourceBackend::GetMetadata( const res::ResourcePath& path )
	{
		Metadata* metadata = m_metadata.FindPtr( path );

		if( !metadata )
		{
			auto actualMetadata = res::ResourceMetadata::Get( path, true );

			if( !actualMetadata )
				actualMetadata = CreateHandle< res::ResourceMetadata >();

			auto result = m_metadata.Insert( path, std::move( Metadata() ) );

			metadata = &result.Iterator().Value();

			metadata->metadata = actualMetadata;
			metadata->objectCollection->SetObject( metadata->metadata.Get() );
			metadata->objectCollection->Refresh();
		}

		return metadata;
	}

	void ResourceBackend::ClearMetadata(const res::ResourcePath& path)
	{
		m_metadata.Remove( path );
	}

	Uint32 ResourceBackend::ReadGPUSizeForResource( const res::ResourcePath& resPath )
	{
		// Caching currently disabled
		Uint32* gpuSizePtr = m_resourcePathToGPUSize.FindPtr( resPath );
		if ( nullptr == gpuSizePtr )
		{
			red::SharedPtr< serialization::IAsyncSource > asyncSource = m_depot->CreateResourceAsyncSource( resPath );
			if ( !asyncSource )
			{
				RED_LOG_ERROR( "Failed to create async source, when trying fetching GPU size from a resource." );
				return 0;
			}

			const Uint32 gpuSize = serialization::tools::GetGPUSize( *asyncSource.Get() );
			m_resourcePathToGPUSize.Insert( resPath, gpuSize );
			return gpuSize;
		}
		else
		{
			return *gpuSizePtr;
		}
	}

	void ResourceBackend::OnNotifyPropertyChange( const SerializableID& id, const rtti::AccessPath& path )
	{
		for ( auto metadata : m_metadata )
		{
			if( id == metadata.Value().metadata->GetID() )
			{
				if ( THandle< CResource > res = GResourceLoader->TryAcquiringLoadedResource( metadata.Key() ) )
				{
					res->MarkModified();
				}

				break;
			}
		}
	}

	void ResourceBackend::NotifyEditorOpened( const String& editorName )
	{
		for ( IResourceBackendListener* listener : m_listeners )
		{
			listener->OnEditorOpened( editorName );
		}
	}

	void ResourceBackend::NotifyEditorClosed( const String& editorName )
	{
		for ( IResourceBackendListener* listener : m_listeners )
		{
			listener->OnEditorClosed( editorName );
		}
	}

	void ResourceBackend::NotifyAllEditorsClosed()
	{
		for ( IResourceBackendListener* listener : m_listeners )
		{
			listener->OnAllEditorsClosed();
		}
	}

	/// wrapper for loaded resource object
	class Resource : public ::interop::utils::Object
	{
	public:
		Resource( THandle< CResource > resource )
			: ::interop::utils::Object("Resource")
			, m_resource( resource )
		{
			RegisterValueRO<String>( "_path", [this]() { return m_resource->GetPath().ToString(); } );
			RegisterValueRO<String>( "_class", [this]() { return m_resource->GetClass()->GetName().AsChar(); } );
			RegisterObject( "data", [this](::interop::IContext& ctx) { return interop::utils::CreateRTTIAccessFunc( ctx, m_resource.Get(), m_resource->GetClass(), true ); } );
		}

	private:
		THandle< CResource >	m_resource;
	};

	//-------

	Bool ShouldBeLoaded( const res::ResourcePath& path )
	{
		return path.ToStringView().EndsWith( consts::workspotExtension );
	}

	THandle< CResource > GetResource( const res::ResourcePath& path )
	{
		if ( THandle< CResource > res = GResourceLoader->TryAcquiringLoadedResource( path ) )
			return res;

		if ( ShouldBeLoaded( path ) )
		{
			if ( THandle< CResource > res = GResourceLoader->IssueLoadingRequest( path )->WaitUntilLoaded() )
				return res;
		}

		return nullptr;
	}

	void GetAdditionalMetadataInfo( const res::ResourcePath& path, red::DynArray< String >& outAdditionalInfo )
	{
		const red::StringView strPath = path.ToStringView();
		if ( strPath.EndsWith( consts::entityExtension ) || strPath.EndsWith( consts::prefabExtension ) )
		{
			if ( THandle< res::ResourceMetadata > metadata = res::ResourceMetadata::Get( path, true ) )
			{
				static red::StaticArray< CName, 4 > playStyles{ RED_NAME_CONSTEXPR( "netrunner" ), RED_NAME_CONSTEXPR( "techie" ), RED_NAME_CONSTEXPR( "solo" ), RED_NAME_CONSTEXPR( "generic" ) };

				const red::DynArray<CName>& metadataTags = metadata->GetTags();
				const auto foundIt = std::find_first_of( metadataTags.Begin(), metadataTags.End(), playStyles.Begin(), playStyles.End() );
				if ( foundIt != metadataTags.End() )
				{
					outAdditionalInfo.PushBack( String::Printf( "Playstyle: %hs", ( *foundIt ).AsChar() ) );
				}
			}
		}
	}

	proxy::AdditionalFileInfo GatherAdditionalInfo( ResourceBackend* backend, const res::ResourcePath& path )
	{
		proxy::AdditionalFileInfo result;

		red::DynArray< String > additionalInfo{ red::PoolBackend() };

		if ( THandle< CResource > res = GetResource( path ) )
		{
			res->GetAdditionalInfo( additionalInfo );
			result.m_gpuSize = res->GetGPUSize();
		}
		else
		{
			additionalInfo.PushBack( "Not loaded" );
			result.m_gpuSize = backend->ReadGPUSizeForResource( path );
		}

		GetAdditionalMetadataInfo( path, additionalInfo );
		result.m_info = String::Join( additionalInfo, "," );

		red::DynArray<res::ResourcePath > backwardLinks;
		backend->GetResourceDatabase().GetBackwardLinks( path, backwardLinks );
		result.m_referenceCount = backwardLinks.Size();

		return result;
	}

	red::DynArray< proxy::AdditionalFileInfo > GatherAdditionalInfos( ResourceBackend* backend, const red::DynArray< String > paths )
	{
		red::DynArray< proxy::AdditionalFileInfo > result;
		result.Reserve( paths.Size() );

		for ( const String &path : paths )
		{
			const auto resourcePath = res::ResourcePath::Build( path );
			result.PushBack( std::move( GatherAdditionalInfo( backend, resourcePath ) ) );
		}

		return result;
	}

	/// wrapper for resource proxy
	class ResourceProxy : public ::interop::utils::Object
	{
	public:
		explicit ResourceProxy( ResourceBackend* backend, const res::ResourcePath& path )
			: ::interop::utils::Object( "ResourceProxy" )
			, m_backend( backend )
			, m_path( path )
		{
			RegisterObject( "metadata", [this]( interop::IContext& ) { return m_backend->GetMetadata( m_path )->objectCollection->GetRootInterop(); } );
			RegisterFunctor<void>( "clearMetadata", [this]( interop::IContext& ) { m_backend->ClearMetadata( m_path ); } );

			// get the actual path
			RegisterValueRO<String>( "path", [this]() { return m_path.ToString(); } );

			RegisterValueRO<Bool>( "exists", [this]() { return m_backend->GetResourceDepot()->ResourceExists( m_path ); } );

			RegisterValueRO< proxy::AdditionalFileInfo >( "additionalInfo", [this]()
			{
				return GatherAdditionalInfo( m_backend, m_path );
			} );

			// #tbd: cache this somewhere
			RegisterFunctor<String, String>("prefabNodeInstanceCount", [this]( interop::IContext& context, const String& resourcePathArg)
			{
				String ret = "prefabOwnersCount:0";

				Uint32 prefabOwnersCount = 0;

				if (!m_path.ToStringView().EndsWith(".prefab"))
				{
					context.ThrowException("Not a prefab %s", m_path.ToDebugString());
					return ret;
				}

				auto prefab = GResourceLoader->IssueLoadingRequest( m_path )->WaitUntilLoaded<world::Prefab>();
				if (!prefab)
				{
					context.ThrowException("Prefab failed to load: %s", m_path.ToDebugString());
					return ret;
				}

				red::DynArray<res::ResourcePath > usedResources;
				const auto resourcePath = res::ResourcePath::Build(resourcePathArg);
				for (const auto& it : prefab->GetNodes())
				{
					if (!it)
					{
						continue;
					}

					usedResources.Clear();
					it->GetUsedResourcePaths(usedResources);
					if (usedResources.Exist(resourcePath))
					{
						prefabOwnersCount += 1;
					}
				}

				ret = String::Printf("prefabNodeInstanceCount:%u instances of %s", prefabOwnersCount, red::paths::GetFileName(resourcePathArg ).Data() );

				return ret;
			});

			RegisterFunctor< Bool, String >( "setPrefabProxy", [this]( interop::IContext& ctx, const String& meshPathArg )
			{


				if( !m_path.ToStringView().EndsWith( ".prefab" ) )
				{
					ctx.ThrowException( "Not a prefab %s", m_path.ToDebugString() );
					return false;
				}

				if (!m_path.ToStringView().EndsWith(".prefab"))
				{
					ctx.ThrowException( "Not a prefab %s", m_path.ToDebugString() );
					return false;
				}

				res::IssueLoadingRequestParameter parameter;
				parameter.path = m_path;
				parameter.skipBuffers = false;
				parameter.skipPostLoad = false;
				parameter.postLoadFlags.skipUploadRenderData = true;

				auto prefab = GResourceLoader->IssueLoadingRequest( parameter )->WaitUntilLoaded< world::Prefab >();
				if( !prefab )
				{
					ctx.ThrowException( "Prefab cannot be loaded %s", m_path.ToStringView() );
				}
				res::ResourcePath meshResPath = res::ResourcePath::Build( meshPathArg.AsChar() );
				const TResAsyncRef< CMesh > resRef{meshResPath};

				if( !resRef.IsValid() )
				{
					ctx.ThrowException( "Mesh cannot be loaded %s", meshPathArg.AsChar() );
					return false;
				}

				// #tbd: could just check for existence, but let's see if we can load it
				res::LoadingOptions loadingOptions;
				loadingOptions.postLoadFlags.skipUploadRenderData = true;
				if( !resRef.IssueLoadingRequest( loadingOptions )->WaitUntilLoaded() )
				{
					ctx.ThrowException( "Mesh cannot be loaded %s", meshPathArg.AsChar() );
					return false;
				}

				prefab->SetProxyMesh( resRef );

				res::ResourceSavingOptions saveOptions;
				saveOptions.m_targetPath = m_path;
				return res::SaveResource( prefab, saveOptions );
			});

			RegisterFunctor< Bool, String >("setEntityProxy", [this](interop::IContext& ctx, const String& meshPathArg)
			{


				if (!m_path.ToStringView().EndsWith(".ent"))
				{
					ctx.ThrowException("Not a prefab %s", m_path.ToDebugString());
					return false;
				}


				THandle< CResource > resource{ GResourceLoader->TryAcquiringLoadedResource(m_path) };
				res::ResourceTokenHandle token{ nullptr };
				if (!resource)
				{
					token = GResourceLoader->IssueLoadingRequest(m_path);
					resource = token->WaitUntilLoaded();
				}
				if (!resource) {
					ctx.ThrowException("Entity cannot be loaded %s", m_path.ToStringView());
				}

				const auto entityTemplate{ Cast< ent::EntityTemplate >(resource) };
				if (!entityTemplate) { 
					ctx.ThrowException("Entity cannot be loaded %s", m_path.ToStringView());
				}

				res::ResourcePath meshResPath = res::ResourcePath::Build(meshPathArg.AsChar());
				entityTemplate->SetProxyMesh(meshResPath);

				res::ResourceSavingOptions saveOptions;
				saveOptions.m_targetPath = m_path;
				return res::SaveResource(entityTemplate, saveOptions);
				return true;
			});

			RegisterFunctor< Bool, String, String >("setAppearanceProxy", [this](interop::IContext& ctx, const String& appearanceName, const String& meshPathArg)
			{


				if (!m_path.ToStringView().EndsWith(".app"))
				{
					ctx.ThrowException("Not a prefab %s", m_path.ToDebugString());
					return false;
				}


				THandle< CResource > resource{ GResourceLoader->TryAcquiringLoadedResource(m_path) };
				res::ResourceTokenHandle token{ nullptr };
				if (!resource)
				{
					token = GResourceLoader->IssueLoadingRequest(m_path);
					resource = token->WaitUntilLoaded();
				}
				if (!resource) {
					ctx.ThrowException("Entity cannot be loaded %s", m_path.ToStringView());
				}

				const auto appearanceResource{ Cast< appearance::AppearanceResource >(resource) };
				if (!appearanceResource) {
					ctx.ThrowException("Entity cannot be loaded %s", m_path.ToStringView());
				}

				res::ResourcePath meshResPath = res::ResourcePath::Build(meshPathArg.AsChar());
				auto app = appearanceResource->GetAppearance(RED_NAME(appearanceName));
				if (app)
				{
					app->SetProxyMesh(meshResPath);
					res::ResourceSavingOptions saveOptions;
					saveOptions.m_targetPath = appearanceResource->GetPath();
					if (!res::SaveResource(appearanceResource, saveOptions))
					{
						return false;
					}
				}

				res::ResourceSavingOptions saveOptions;
				saveOptions.m_targetPath = m_path;
				return res::SaveResource(appearanceResource, saveOptions);
			});

			RegisterValueRO<red::DynArray<String>>( "references", [this]()
			{
				red::DynArray<res::ResourcePath > backwardLinks;
				m_backend->GetResourceDatabase().GetBackwardLinks(m_path, backwardLinks);

				red::DynArray< String > ret;
				ret.Reserve(backwardLinks.Size());
				for (const auto& it : backwardLinks)
				{
					ret.PushBack(it.ToDebugString());
				}
				return ret;
			});

			RegisterValueRO<red::DynArray<String>>("prefabOwners", [this]()
			{
				red::DynArray< res::ResourcePath > revDeps = GetStreamingResourceManager()->GetReverseDependencies(m_path);

				red::DynArray< String > ret;
				ret.Reserve(revDeps.Size());
				for (const auto& it : revDeps)
				{
					if (it.ToStringView().EndsWith(".prefab"))
					{
						ret.PushBack(it.ToDebugString());
					}
					else
					{
						RED_LOG_WARNING("TODO reverse dep of %s is not a prefab: %s", m_path.ToDebugString(), it.ToDebugString());
					}
				}

				// Because the streaming cache isn't reliable for nested prefabs and I don't know why for sure right this second
				red::DynArray<res::ResourcePath > backwardLinks;
				m_backend->GetResourceDatabase().GetBackwardLinks(m_path, backwardLinks);

				for (const auto& it : backwardLinks)
				{
					if (it.ToStringView().EndsWith(".prefab"))
					{
						ret.PushBack(it.ToDebugString());
					}
				}

				std::sort(ret.Begin(), ret.End());
				ret.Remove(std::unique(ret.Begin(), ret.End()), ret.End());

				return ret;
			});

			RegisterValue<res::ResourceMetadata::TFileTags>( "tags",
			[ this ]( interop::IContext& ctx, res::ResourceMetadata::TFileTags& outValue )
			{
				auto metadata = m_backend->GetMetadata( m_path );
				outValue = metadata->metadata->GetTags();
			},
			[ this ]( interop::IContext& ctx, const res::ResourceMetadata::TFileTags& value )
			{
				auto metadata = m_backend->GetMetadata( m_path );
				res::ResourceMetadata::TFileTags& tags = metadata->metadata->GetTags();
				tags = value;

				TResRef< CResource > resRef( m_path );
				if ( THandle< CResource > res = resRef.Get() )
				{
					res->MarkModified();

					Bool result = m_backend->SaveResource( res );
					if ( result )
					{
						m_backend->GetTagsChangedNotifier().Notify( m_path.ToString() );
					}
					else
					{
						ctx.ThrowException( "Failed to save tags. Please try to checkout resource first." );
					}
				}
			} );

			RegisterTypedKeyMap< CName >( "animationTags", [ this ]( interop::IContext& ctx, const CName& animationName )
			{
				auto* metadata = m_backend->GetMetadata( m_path );
				res::AnimationMetadata& animationMetadata = metadata->metadata->GetAnimationMetadata( animationName );

				return interop::utils::CreateRTTIAccessFunc( ctx, &animationMetadata.m_tags, GetTypeObject< red::TagList >(), true );
			} );


			RegisterValueRO<String>( "importMetadata", [this]()
			{
				String importPath;
				if ( THandle<res::ResourceMetadata> meta = res::ResourceMetadata::Get( m_path, true ) )
				{
					if ( const THandle<res::ResourceImportMetadata>& importMeta = meta->GetImportData() )
					{
						importPath = importMeta->GetIntermediatePath();
						if ( importPath.Empty() )
						{
							importPath = importMeta->GetAssetVersionControlPath();
							if ( importPath.Empty() )
							{
								importPath = importMeta->GetAssetPath();
							}
							else
							{
								// add checksum here
								red::StringBuilder<String> sb;
								sb.Append( "vc:" );
								sb.Append( importPath );
								sb.Append( String::Printf( "#%d", importMeta->GetAssetVersionControlRevision() ) );
								importPath = sb.ToString();
							}
						}
					}
				}
				return importPath;
			} );

			RegisterObject( "editor",
				[this](interop::IContext& ctx)
			{
				auto editor = m_backend->GetResourceEditorByPath( m_path );
				if (editor == nullptr)
				{
					ctx.ThrowException("Editor does not exist!");
					return interop::ObjectPtr();
				}
				return editor->CreateInterop();
			} );

			RegisterFunctor<Bool, String, String>( "open",
				[this]( interop::IContext& ctx, String editorName, const String& args )
				{
					auto result =  m_backend->CreateResourceEditor( m_path, editorName, args );
					if( !result )
					{
						ctx.ThrowException( "Resource is not supported by any editor!" );
						return false;
					}

					return true;
				} );

			RegisterFunctor<shared::ResourceCommandOutcome, CName, Bool, String>( "runCommand",
				[this]( interop::IContext& ctx, const CName& commandName, Bool reuseLastOptions, const String& param )
				{
					CommandsHandler& cmdHandler = m_backend->GetCommandHandler();
					ResourceCommandUniquePtr command = cmdHandler.GetFactory().CreateCommand( commandName, m_path, {} );
					command->SetOptionalParameter( param );
					return RunCommand( ctx, std::move( command ), reuseLastOptions );
				} );

			RegisterFunctor<shared::ResourceCommandOutcome, CName, Bool, red::DynArray< String >, String >( "runBatchCommand",
				[ this ]( interop::IContext& ctx, const CName& commandName, Bool reuseLastOptions, red::DynArray< String > fileSet, const String& param )
			{
				red::DynArray< res::ResourcePath > files;
				for ( const String& path : fileSet )
				{
					files.PushBack( res::ResourcePath::Build( path ) );
				}

				CommandsHandler& cmdHandler = m_backend->GetCommandHandler();
				ResourceCommandUniquePtr command = cmdHandler.GetFactory().CreateCommand( commandName, m_path, files );
				command->SetOptionalParameter( param );
				return RunCommand( ctx, std::move( command ), reuseLastOptions );
			} );

			RegisterFunctor<shared::ResourceCommandOutcome, String, Bool>( "import",
				[this]( interop::IContext& ctx, String absoluteImportPath, Bool reuseLastOptions )
				{
					CommandsHandler& cmdHandler = m_backend->GetCommandHandler();
					ResourceCommandUniquePtr command = cmdHandler.GetFactory().CreateImportCommand( m_path, red::AbsolutePath::CreateFilePath( absoluteImportPath ) );
					return RunCommand( ctx, std::move( command ), reuseLastOptions );
				} );

			RegisterFunctor<shared::ResourceCommandOutcome, String, String>( "importWithOptions",
				[this]( interop::IContext& ctx, const String& absoluteImportPath, const String& optionsString )
			{
				CommandsHandler& cmdHandler = m_backend->GetCommandHandler();
				ResourceCommandUniquePtr command = cmdHandler.GetFactory().CreateImportCommand( m_path, red::AbsolutePath::CreateFilePath( absoluteImportPath ) );
				return RunCommand( ctx, std::move( command ), false, optionsString );
			} );

			RegisterFunctor<shared::ResourceCommandOutcome>( "commitOp", [this]( interop::IContext& ctx )
			{
				return CommitCommand( ctx );
			} );

			RegisterFunctor<shared::ResourceCommandOutcome>( "dropOp", [this]( interop::IContext& ctx )
			{
				return AbortCommand();
			} );

			RegisterObject( "operation", m_backend->GetCommandHandler().GetCommand( m_path ).Get() );

			RegisterValue<Bool>( "isLoaded", [ this ]( interop::IContext& ctx, Bool& outValue )
			{
				outValue = m_backend->IsResourceLoaded( m_path );
			} );

			RegisterValue<Bool>( "isModified", [ this ]( interop::IContext& ctx, Bool& outValue )
			{
				outValue = m_backend->IsResourceModified( m_path );
			} );

			RegisterFunctor<Bool>( "save",
				[ this ]( interop::IContext& ctx )
			{
				return m_backend->SaveResource( m_path );
			} );

			RegisterFunctor<Bool>( "reload",
				[ this ]( interop::IContext& ctx )
			{
				return m_backend->ReloadResource( m_path );
			} );

			RegisterFunctor<void>( "finishLoading",
				[ this ]( interop::IContext& ctx )
			{
				m_backend->FinishLoadingResource( m_path );
			} );

			RegisterFunctor<Bool, String, String>( "saveAs",
				[this] ( interop::IContext& ctx, String path, String options )
			{
				res::ResourcePath destPath = res::ResourcePath::Build( path );
				return m_backend->SaveResourceAs( m_path, destPath, true );
			} );

			RegisterFunctor<Bool, Bool, CName>( "generateThumbnail",
				[ this ]( interop::IContext& ctx, Bool forceUpdate, CName prefferedAppearanceName )
			{
				return m_backend->GenerateThumbnail( m_path, forceUpdate, prefferedAppearanceName );
			} );

			RegisterFunctor<Bool, red::AbsolutePath>( "generateIcon",
				[this]( interop::IContext& ctx, red::AbsolutePath output )
			{
				return m_backend->GenerateIcon( m_path, output );
			});
		}

		virtual ~ResourceProxy() override final = default;

		shared::ResourceCommandOutcome RunCommand( interop::IContext& ctx, ResourceCommandUniquePtr command, Bool reuseLastOptions = false, const String& optionsString = String::EMPTY() )
		{
			THandle< ResourceCommandOptions > suggestedOptions = reuseLastOptions ? m_backend->GetCommandHandler().GetCachedOptions() : nullptr;
			THandle< ResourceCommandOptions > options = command->CreateOptions( suggestedOptions );
			if ( options == nullptr )
			{
				return shared::ResourceCommandOutcome( shared::CommandResult::Abort );
			}

			if ( !optionsString.Empty() )
			{
				red::CommandLine importCommandLine = red::CommandLine::ParseInnerString( optionsString );
				if ( !options->ParseFromCommandLine( importCommandLine ) )
				{
					RED_LOG_ERROR( "Import: Error parsing command line parameters in import command" );
					return shared::ResourceCommandOutcome( shared::CommandResult::Fail );
				}
			}

			auto commandWrapper = red::CreateSharedPtr< ResourceCommandWrapper >( std::move( command ), options );
			m_backend->GetCommandHandler().AddCommand( m_path, commandWrapper );

			if ( ( suggestedOptions && options->GetClass() == suggestedOptions->GetClass() ) || !options->NeedOptions() )
			{
				return CommitCommand( ctx );
			}
			return shared::ResourceCommandOutcome( shared::CommandResult::NeedOptions );
		}

		shared::ResourceCommandOutcome CommitCommand( interop::IContext& ctx )
		{
			ResourceCommandWrapperSharedPtr& command = m_backend->GetCommandHandler().GetCommand( m_path );
			RED_FATAL_ASSERT( command, "Cannot commit non-existing command!" );

			shared::ResourceCommandOutcome result;
			ResourceCommandResult commandResult = command->Execute();

			if ( commandResult.IsSuccessful() )
			{
				const ResourceCommandResult::ResourcesList& newResources = commandResult.GetResources();

				red::DynArray< res::ResourcePath > paths;
				for ( const auto& resource : newResources )
				{
					// Not every resource command can generate a new resource right away.
					// Might need to be dispatched to the techart-peon server for processing for an indeterminate amount of time
					if (!resource)
					{
						continue;
					}
					m_backend->NotifyResourceReloaded( resource->GetPath(), resource );
					paths.PushBack( resource->GetPath() );
				}

				if (commandResult.HasMessage())
				{
					m_backend->NotifyEditorMessage("", commandResult.GetSuccessMessage() );
				}
				result = shared::ResourceCommandOutcome( shared::CommandResult::Success, paths );
				result.m_message = commandResult.GetSuccessMessage();
			}
			else
			{
				const String& errorMessage = commandResult.GetErrorMessage();
				ctx.ThrowException( errorMessage.Empty() ? "Command execution failed." : errorMessage.AsChar() );
			}

			// clean processed command
			m_backend->GetCommandHandler().RemoveCommand( m_path );
			return result;
		}

		shared::ResourceCommandOutcome AbortCommand()
		{
			m_backend->GetCommandHandler().RemoveCommand( m_path );
			return shared::ResourceCommandOutcome( shared::CommandResult::Abort );
		}

	private:
		ResourceBackend* m_backend;
		res::ResourcePath m_path;
	};

	//-------

	namespace Helper
	{
		struct ImportExportInfo
		{
			ImportExportInfo()
				: m_fileFormats{ red::PoolBackend() }
			{}

			const rtti::ClassType* m_resourceClass;
			DynArray< CFileFormat >	m_fileFormats;
		};

		// TODO: this needs to go if we add attachable factories/importers in DLLs
		struct ResourceHelper
		{
		public:
			DynArray< const rtti::ClassType* >		m_factoryClasses;
			DynArray< ImportExportInfo >			m_importerClasses;
			DynArray< ImportExportInfo >			m_exporterClasses;

			ResourceHelper()
				: m_factoryClasses{ red::PoolBackend() }
				, m_importerClasses{ red::PoolBackend() }
				, m_exporterClasses{ red::PoolBackend() }
			{
				// factories
				{
					IFactory::EnumFactoryClasses( m_factoryClasses );
				}

				// importers
				{
					DynArray< const rtti::ClassType* > resourceClasses{ red::PoolBackend() };
					IImporter::EnumImportClasses( resourceClasses );

					m_importerClasses.Reserve( resourceClasses.Size() );
					for ( const auto* resClass : resourceClasses )
					{
						ImportExportInfo info;
						info.m_resourceClass = resClass;

						IImporter::EnumImportFormats( resClass, info.m_fileFormats );
						m_importerClasses.PushBack( info );
					}
				}

				// exporters
				{
					DynArray< const rtti::ClassType* > resourceClasses{ red::PoolBackend() };
					IExporter::EnumExportClasses( resourceClasses );

					m_importerClasses.Reserve( resourceClasses.Size() );
					for ( const auto* resClass : resourceClasses )
					{
						ImportExportInfo info;
						info.m_resourceClass = resClass;

						IExporter::EnumExportFormats( resClass, info.m_fileFormats );
						m_exporterClasses.PushBack( info );
					}
				}
			}

			static ResourceHelper& GetInstance()
			{
				static ResourceHelper theInstance;
				return theInstance;
			}
		};

		/// new interop proxy
		class FileFormatProxy : public ::interop::utils::Object
		{
		public:
			const CFileFormat* m_format;

			FileFormatProxy( const CFileFormat* format )
				: ::interop::utils::Object("FileFormatProxy")
				, m_format( format )
			{
				RegisterValueRO<String>( "ext", [this]() { return m_format->GetExtension(); } );
				RegisterValueRO<String>( "desc", [this]() { return m_format->GetDescription(); } );
			}
		};

		class ImportExportFileFormatProxy : public ::interop::utils::Object
		{
		public:
			const ImportExportInfo*	m_info;

			ImportExportFileFormatProxy( const ImportExportInfo* info )
				: ::interop::utils::Object("ImportExportFileFormatProxy")
				, m_info( info )
			{
				RegisterValueRO< String >( "class", [ this ]() { return m_info->m_resourceClass->GetName().AsChar(); } );
				RegisterValueRO< String >( "ext", [ this ]() { return m_info->m_resourceClass->GetDefaultObject< CResource >()->GetExtension(); } );
				RegisterValueRO< String >( "deprecatedExt", [ this ]() { return m_info->m_resourceClass->GetDefaultObject< CResource >()->GetDeprecatedExtension(); } );
				RegisterValueRO< String >( "name", [ this ]() { return m_info->m_resourceClass->GetDefaultObject< CResource >()->GetFriendlyDescription(); } );

				RegisterArray( "formats",
					m_info->m_fileFormats.Size(),
					[this](::interop::IContext& ctx, Uint32 index)
				{
					return interop::ObjectPtr( RED_NEW( FileFormatProxy )( &m_info->m_fileFormats[index] ) );
				}
				);
			}
		};

		/// new interop proxy
		class FactoryInfoProxy : public ::interop::utils::Object
		{
		public:
			const rtti::ClassType*	m_resourceClass;

			FactoryInfoProxy( const rtti::ClassType* resourceClass )
				: ::interop::utils::Object("FactoryInfoProxy")
				, m_resourceClass( resourceClass )
			{
				RegisterValueRO< String >( "class", [ this ]() { return m_resourceClass->GetName().AsChar(); } );
				RegisterValueRO< String >( "ext", [ this ]() { return m_resourceClass->GetDefaultObject< CResource >()->GetExtension(); } );
				RegisterValueRO< String >( "deprecatedExt", [ this ]() { return m_resourceClass->GetDefaultObject< CResource >()->GetDeprecatedExtension(); } );
				RegisterValueRO< String >( "name", [ this ]() { return m_resourceClass->GetDefaultObject< CResource >()->GetFriendlyDescription(); } );
			}
		};
	}

	void ResourceBackend::SetupInterop( ResourceBackend* backend, ::interop::utils::Object& op )
	{
		// resource map
		op.RegisterObject("files",
			/*GET*/[backend](interop::IContext& ctx)
		{
			return ::interop::ObjectPtr( RED_NEW( ::interop::utils::MapObject )(
				[backend](interop::IContext& ctx, const String& elementName)
			{
				// build resource path
				const auto path = res::ResourcePath::Build( elementName );
				if(path.IsValid() == false )
				{
					ctx.ThrowException("File path is invalid!");
					return ::interop::ObjectPtr();
				}

				return ::interop::ObjectPtr( RED_NEW( ResourceProxy )( backend, path ) );
			}));
		});

		op.RegisterFunctor< red::DynArray< proxy::AdditionalFileInfo >, red::DynArray< String > >(
			"additionalInfos",
			[ backend ]( ::interop::IContext& ctx, const red::DynArray< String > &resourcePaths )
		{
			return GatherAdditionalInfos( backend, resourcePaths );
		} );

		// editors map
		op.RegisterObject("editors",
			/*GET*/[backend](interop::IContext& ctx)
		{
			return ::interop::ObjectPtr(RED_NEW( ::interop::utils::MapObject )(
				[backend](interop::IContext& ctx, const String& elementName)
			{
				auto editor = backend->GetResourceEditorByName( elementName );
				if(editor == nullptr)
				{
					ctx.ThrowException("Element \"%hs\" does not exist in map!", elementName.AsChar());
					return ::interop::ObjectPtr();
				}
				return editor->CreateInterop();
			}));
		});

		op.RegisterValueRO< LastNodeSelection >( "lastNodeSelection", [backend]() { return backend->m_lastNodeSelection; } );

		// generic editor notification
		op.RegisterNotifier( "_editorMessage", backend->m_editorMessage );

		// informs about new editor
		op.RegisterNotifier( "_newEditorCreationSuccess", backend->m_newEditorCreationSuccess );
		op.RegisterNotifier( "_newEditorCreationFailed", backend->m_newEditorCreationFailed );
		op.RegisterNotifier( "_newEditorCreationFailReason", backend->m_newEditorCreationFailReason );

		// modification/unomdification of a file
		op.RegisterNotifier( "_onFileModified", backend->m_fileModified );
		op.RegisterNotifier( "_onResourceSaved", backend->m_resourceSaved );
		op.RegisterNotifier( "_onFileUnmodified", backend->m_fileUnmodified );
		op.RegisterNotifier( "_onError", backend->m_errorNotifier );

		op.RegisterNotifier( "_onDataError", backend->m_dataErrorNotifier );

		op.RegisterNotifier( "_onOpenWebBrowser", backend->m_openWebBrowserNotifier );

		// notification about new thumbnail for the resource
		op.RegisterNotifier( "_onThumbnailGenerated", backend->m_thumbnailGenNotifier );
		op.RegisterNotifier( "_onTagsChanged", backend->m_tagsChangedNotifier );

		op.RegisterNotifier("_onResourceDatabaseRefreshed", backend->m_resourceDatabaseRefreshedNotifier);

		op.RegisterNotifier("_onUpdateStreamingCacheSyncProgress", backend->m_updateStreamingCacheSyncProgress);

		op.RegisterNotifier("_onUpdateFindAllPrefabsToForceSaveProgress", backend->m_updateFindAllPrefabsToForceSaveProgress);

		// list of edited files
		op.RegisterValue<red::DynArray<String>>("edited", [backend](::interop::IContext& ctx, red::DynArray<String>& outValues)
		{
			red::DynArray< res::ResourcePath > openedPaths{ red::PoolBackend() };
			backend->GetOpenedFiles( openedPaths );
			outValues.Reserve( openedPaths.Size() );
			for ( const auto& path : openedPaths )
			{
				outValues.PushBack(path.ToString());
			}
		});

		// list of modified resources
		op.RegisterValue<red::DynArray<String>>("modified", [backend](::interop::IContext& ctx, red::DynArray<String>& outValues)
		{
			outValues.Reserve(backend->m_modifiedFiles.Size());
			for (const auto& path : backend->m_modifiedFiles)
			{
				outValues.PushBack(path);
			}
		});

		op.RegisterFunctor<String, String>( "editorForResource", [backend]( ::interop::IContext& ctx, const String& resourcePath ) -> String
		{
			const auto editor = backend->GetResourceEditorByPath( res::ResourcePath::Build( resourcePath ) );
			if ( editor )
			{
				return editor->GetName();
			}
			return String();
		} );

		op.RegisterFunctor<String, String>( "editorsForResourceType", [backend]( ::interop::IContext& ctx, const String& resourceTypeName ) -> String
		{
			const rtti::ClassType* resourceType = GetRttiSystem().FindClass( RED_NAME_NOREG( resourceTypeName.AsChar() ) );
			if ( resourceType != nullptr )
			{
				red::DynArray< ResourceEditorPtr > editors{ red::PoolBackend() };
				backend->GetResourceEditorsByResourceType( resourceType, editors );
				if ( !editors.Empty() )
				{
					red::DynArray< red::String > editorsDesc{ red::PoolBackend() };
					editorsDesc.Reserve( editors.Size() * 2 );
					for ( ResourceEditorPtr e : editors )
					{
						editorsDesc.PushBack( e->GetName() );
						editorsDesc.PushBack( e->GetResource()->GetPath().ToString() );
					}
					return red::String::Join( editorsDesc, ";" );
				}
			}
			return String::EMPTY();
		} );

		// close all editors
		op.RegisterFunctor<void>("closeAll",
			[backend](interop::IContext& ctx)
		{
			backend->CloseAllEditors();
		});

		// get list of resource extensions that can be created
		op.RegisterArray("factories", Helper::ResourceHelper::GetInstance().m_factoryClasses.Size(),
			[backend](::interop::IContext& ctx, const Uint32 index) -> ::interop::ObjectPtr
		{
			const auto& list = Helper::ResourceHelper::GetInstance();
			return ::interop::ObjectPtr( RED_NEW( Helper::FactoryInfoProxy )( list.m_factoryClasses[ index ] ));
		});

		// get list of resources extensions that can be imported
		op.RegisterArray("importers", Helper::ResourceHelper::GetInstance().m_importerClasses.Size(),
			[backend](::interop::IContext& ctx, const Uint32 index) -> ::interop::ObjectPtr
		{
			const auto& list = Helper::ResourceHelper::GetInstance();
			return ::interop::ObjectPtr( RED_NEW( Helper::ImportExportFileFormatProxy )( &list.m_importerClasses[ index ] ));
		});

		// get list of resources extensions that can be exported
		op.RegisterArray("exporters", Helper::ResourceHelper::GetInstance().m_exporterClasses.Size(),
			[backend](::interop::IContext& ctx, const Uint32 index) -> ::interop::ObjectPtr
		{
			const auto& list = Helper::ResourceHelper::GetInstance();
			return ::interop::ObjectPtr( RED_NEW( Helper::ImportExportFileFormatProxy )( &list.m_exporterClasses[ index ] ));
		});

		// Set the initial transaction counter value for undo/redo history to 1
		op.RegisterFunctor<void>( "startTransaction", [](::interop::IContext& ctx) { IEditActionHistory::InitTransaction(); } );

		op.RegisterFunctor<void>( "clearStreamingCache", [](::interop::IContext& ctx) {
			GetStreamingResourceManager()->ClearCache();
		} );

		op.RegisterFunctor<void>("updateResourceDatabaseSync", [backend](::interop::IContext& ctxt)
		{
			backend->UpdateResourceDatabaseSync();
		});

		op.RegisterFunctor<void>("refreshResourceDatabase", [backend](::interop::IContext& ctxt)
		{
			backend->m_requestRefreshResourceDatabase = true;
		});

		op.RegisterValueRO<String>("resourceDatabaseFilePath", [backend]
		{
			const String ret = backend->GetResourceDatabaseFilePath().AsString();
			return ret;
		});

		op.RegisterValueRO<Bool>( "ignoreValidationErrors", [] { return Config::cvValidationErrorsBypass.Get(); } );

		op.RegisterFunctor<void>("updateStreamingCacheSync", [backend](::interop::IContext& ctxt)
		{
			GetStreamingResourceManager()->ClearCache(); // make sure clean

			// Ideally get all, but some prefabs cause stackoverflow because they're too deep!
// 			auto resources = commandlet::ScanResourceDepotDirectories(*helper::SafeCastBackendResourceDepot(backend->m_depot), "", "*.world");
// 			resources.PushBack(commandlet::ScanResourceDepotDirectories(*helper::SafeCastBackendResourceDepot(backend->m_depot), "", "*.prefab"));
//			// Especially remove any autogenerated prefabs, which magically cause us to get a stackoverflow exception because
//			// the hierarchy is way too deep
// 			auto removeBegin = std::remove_if(resources.Begin(), resources.End(), [](const res::ResourcePath& it) {
// 				return it.ToStringView().Empty() || it.ToStringView().StartsWith('_');
// 			});
// 			resources.Remove(removeBegin, resources.End());

			const red::DynArray< String > patterns{ "*.world", "*prefab" };
			auto resources = commandlet::ScanResourceDepotDirectories( *backend->m_depot, "base\\", patterns );
			resources.PushBack( commandlet::ScanResourceDepotDirectories( *backend->m_depot, "gyms\\", patterns ) );

			// Especially remove any autogenerated prefabs, which magically cause us to get a stackoverflow exception because
			// the hierarchy is way too deep
			auto removeBegin = std::remove_if(resources.Begin(), resources.End(), [](const res::ResourcePath& it) {
				return it.ToStringView().Empty() || it.ToStringView().StartsWith('_');
			});
			resources.Remove(removeBegin, resources.End());

			for ( Uint32 i : resources.Indices() )
			{
				const res::ResourcePath& path = resources[i];
				backend->m_updateStreamingCacheSyncProgress.Notify(String::Printf("[%u/%u] %hs", i + 1, resources.Size(), path.ToDebugString()));
				GetStreamingResourceManager()->GetStreamingData(path);
			}
		});

		op.RegisterFunctor<tools::LastNodeSelection, String>( "getNodeEditorPathByGlobalIDPath", [backend]( ::interop::IContext& ctx, const String& globalIDPath )
		{
			// We prefer to find objects in the prefab editor to finding them in the world editor (consulted with users), if both are open at the same time.
			// Also the more "narrow" the find, the better, so we prefer shorter editor ID paths.
			// We are checking the editor type by testing names.
			// We can't use IsA<> since IResourceEditor is not an ISerializable and we cannot test for PrefabResourceEditor/WorldResourceEditor.

			tools::LastNodeSelection bestResult;
			Uint64 bestKey = std::numeric_limits<Uint64>::max();         // No editor will produce such large key.

			for ( ResourceEditorPtr editor : backend->m_editors )
			{
				const String& editorName = editor->GetEditorTypeName();
				EditorObjectIDPath editorIDPath = editor->GetNodeEditorPathByGlobalIDPath( globalIDPath );
				if ( !editorIDPath.IsEmpty() )
				{
					Bool isPrefabEditor = (editorName == "Prefab editor");
					Uint32 pathDepth = editorIDPath.GetDepth();
					Uint64 key = pathDepth + (static_cast<Uint64>(!isPrefabEditor) << 32);     // Looking for best editor taking editor type and path depth at the same time.
					if ( key < bestKey )
					{
						bestResult = tools::LastNodeSelection( editor->GetName(), editorIDPath );
						bestKey = key;
					}
				}
			}

			return bestResult;
		} );

		op.RegisterFunctor<Bool, CName, red::DynArray< String >>( "setupBatchTool", [backend]( interop::IContext& ctx, const CName& className, const red::DynArray< String >& filePaths )
		{
			const rtti::ClassType* classType = GetRttiSystem().FindClass( className );
			if ( classType && classType->IsA< CResource >() )
			{
				red::DynArray< res::ResourcePath > resourcePaths;
				for ( auto path : filePaths )
				{
					resourcePaths.PushBack( res::ResourcePath::Build( path.AsChar() ) );
				}

				backend->m_batchTool = red::CreateUniquePtr< BatchTool >( classType, resourcePaths, backend );
				return true;
			}
			return false;
		} );

		op.RegisterFunctor< red::DynArray<String> >( "getWorldEditorNames", [backend]( ::interop::IContext& ctx )
		{
			red::DynArray<String> worldEditorNames;

			for ( ResourceEditorPtr editor : backend->m_editors )
			{
				const String& editorName = editor->GetEditorTypeName();
				if ( editorName == "World editor" )
				{
					worldEditorNames.PushBack( editor->GetName() );
				}
			}

			return worldEditorNames;
		} );

		op.RegisterObject( "batchTool", [backend] ( interop::IContext& ctx )
		{
			return backend->m_batchTool ? backend->m_batchTool->CreateInterop() : interop::ObjectPtr();
		} );

		op.RegisterObject( "messageLog", backend->m_messageLog );
		op.RegisterObject( "shortcutManager", backend->m_shortcutBackend->CreateInterop() );
		op.RegisterObject( "tweakdb", *backend->m_tweakDbInteropHelper );

		op.RegisterFunctor<void, String>("flushDeferredDataBuffersToMemory", [backend](interop::IContext& ctx, const String& resourcePath)
		{
			// Make sure the DDBs file handles are closed.
			// Ultimately a no-op if the resource wasn't loaded, but checking if already loaded is technically a race condition.
			auto resource = GResourceLoader->IssueLoadingRequest(res::ResourcePath::Build(resourcePath))->WaitUntilLoaded();
			if (resource)
			{
				serialization::DeferredDataBufferLoader bufferLoader;
				bufferLoader.Run(resource);
			}
		});

		op.RegisterFunctor<Bool, String>("resourceHasAllImportsOnDisk", [backend](interop::IContext& ctx, const String& resourcePath)
		{
			const auto asyncSource = backend->GetResourceDepot()->CreateResourceAsyncSource(res::ResourcePath::Build(resourcePath));
			if (!asyncSource)
			{
				ctx.ThrowException("Failed to load %s", resourcePath.AsChar());
				return false;
			}

			red::DynArray<res::ResourcePath> importPaths;
			serialization::tools::CollectImports(*asyncSource, importPaths);
			for (const auto& it : importPaths)
			{
				if (!backend->GetResourceDepot()->ResourceExists(it))
				{
					return false;
				}
			}

			return true;
		});

		op.RegisterFunctor<void, String, String>("relinkPrefabProxyResource", [backend](interop::IContext& ctx, const String& resourcePath, const String& tempProxyResourcePathPrefix)
		{
			class PrefabProxyPathResolver : public serialization::PathResolver
			{
			public:
				explicit PrefabProxyPathResolver(const String& tempProxyResourcePathPrefix)
					: m_tempProxyResourcePathPrefix(tempProxyResourcePathPrefix)
				{}

				virtual res::ResourcePath ResolvePath(const res::ResourcePath& path) override
				{
					if (path.ToStringView().StartsWith(m_tempProxyResourcePathPrefix))
					{
						red::String prefabPath = RemoveFromResourcePath( path, m_tempProxyResourcePathPrefix );
						prefabPath = red::paths::ExtractParentPath( prefabPath );
						prefabPath.Resize( prefabPath.Length() - 1 );
						prefabPath += ".prefab";

						red::String dstResource = String::Printf( "base\\worlds\\03_night_city\\sectors\\_external\\proxy\\%u", prefabPath.CalcHash() );
						dstResource += red::paths::ExtractFileName( path.ToStringView() );

						return res::ResourcePath::Build( dstResource );
					}
					return path;
				}

				static red::String RemoveFromResourcePath(const res::ResourcePath& path, String remove)
				{
					String tmpPath = path.ToString();
					tmpPath.Replace(remove, "");
					return tmpPath;

				}

			private:
				String m_tempProxyResourcePathPrefix;
			};

			// Import paths won't be valid yet until resaved, so skip to avoid resource loading failure
			// Ideally would have done this BEFORE moving, but I don't want to generate more file modified events in the temp dir,
			// because it's already a guessing game what the events really means since they lose distinguishing information (e.g., new vs modified).
			auto param = res::IssueLoadingRequestParameter(res::ResourcePath::Build(resourcePath));
			param.skipImports = true;
			param.skipPostLoad = true;

			// RELOAD - proxy files were moved from temp folder, so what's in memory will not be the latest
			auto resource = GResourceLoader->IssueReloadingRequest(param)->WaitUntilLoaded();
			if (!resource)
			{
				ctx.ThrowException("Failed to load %s", resourcePath.AsChar());
				return;
			}

			PrefabProxyPathResolver pathResolver{ tempProxyResourcePathPrefix };
			auto* oldPathResolver = serialization::StructureMapper::SetPathResolver(&pathResolver);
			auto* oldPathResolverForPackageCompiler = red::PackageCompiler::Internal_SetPathResolver( &pathResolver );

			const Bool isSaved = res::SaveResource(resource);

			red::PackageCompiler::Internal_SetPathResolver( oldPathResolverForPackageCompiler );
			serialization::StructureMapper::SetPathResolver(oldPathResolver);

			if (!isSaved)
			{
				ctx.ThrowException("Failed to save %s", resourcePath.AsChar());
				return;
			}
		});

		op.RegisterFunctor<red::DynArray<String>, red::DynArray<String>>("startReconcilePrefabs", [backend](interop::IContext& ctx, const red::DynArray<String>& prefabPaths)
		{
			{
				red::DynArray<res::ResourcePath> whyNoRtti1;
				whyNoRtti1.Reserve(prefabPaths.Size());
				for (const auto& it : prefabPaths)
				{
					whyNoRtti1.PushBack(res::ResourcePath::Build(it));
				}
				backend->m_prefabProxyReconciler->ReconcilePrefabProxies(whyNoRtti1);
			}

			red::DynArray<String> reconciledPrefabs;
			{
				const auto tmpPaths = backend->m_prefabProxyReconciler->GetReconciledPrefabPaths();
				reconciledPrefabs.Reserve(tmpPaths.Size());
				for (const auto& it : tmpPaths)
				{
					reconciledPrefabs.PushBack(it.ToDebugString());
				}
			}
			return reconciledPrefabs;
		});

		op.RegisterFunctor<void>("finishReconcilePrefabs", [backend](interop::IContext& ctx)
		{
			backend->m_prefabProxyReconciler->FinishReconciledPrefabs();
		});

		op.RegisterFunctor<red::DynArray<String>, String, String>("collectDepotFilesRecursive", [backend](interop::IContext& ctxt, const String& depotPathDir, const String& pattern)
		{
			red::DynArray<String> paths;
			{
				const auto resPaths = commandlet::ScanResourceDepotDirectories(*backend->GetResourceDepot(), depotPathDir, pattern.AsChar());
				paths.Reserve(resPaths.Size());
				for (const auto& it : resPaths)
				{
					paths.PushBack(it.ToDebugString());
				}
			}
			return paths;
		});

		op.RegisterFunctor<red::DynArray<String>>("collectDepotPrefabProxyMeshFilesRecursive", [backend](interop::IContext& ctxt)
		{
			red::DynArray<String> paths;
			{
				auto resPaths = commandlet::ScanResourceDepotDirectories(*backend->GetResourceDepot(), "", "*.mesh");
				auto removeIt = std::remove_if(resPaths.Begin(), resPaths.End(), [](const res::ResourcePath& it) {

					if ( it.ToStringView().Find("\\proxy\\") == red::StringView::npos)
					{
						return true;
					}

					if (it.ToStringView().StartsWith("temp\\"))
					{
						return true;
					}

					return false;
				});

				resPaths.Remove(removeIt, resPaths.End());

				paths.Reserve(resPaths.Size());
				for (const auto& it : resPaths)
				{
					paths.PushBack(it.ToDebugString());
				}
			}
			return paths;
		});

		op.RegisterFunctor<red::DynArray<String>>("findAllPrefabsToForceSave", [backend](interop::IContext& ctxt)
		{
			red::DynArray<String> paths;
			{
				auto resPaths = commandlet::ScanResourceDepotDirectories(*backend->GetResourceDepot(), "base\\", "*.prefab");

				resPaths.Remove(std::remove_if(resPaths.Begin(), resPaths.End(), [](const res::ResourcePath& it) {
					return it.ToStringView().Find("\\_generated\\") != red::StringView::npos ||
						   it.ToStringView().Find("\\_external\\") != red::StringView::npos;
				}), resPaths.End());

				for (Uint32 i : resPaths.Indices())
				{
					const auto& it = resPaths[i];
					const auto progressText = String::Printf("[%u/%u] %hs", i + 1, resPaths.Size(), red::utils::ExtractFileName(it.ToStringView()).AsChar());
					backend->m_updateFindAllPrefabsToForceSaveProgress.Notify(progressText);
					auto prefab = GResourceLoader->IssueLoadingRequest(it)->WaitUntilLoaded<world::Prefab>();
					if (!prefab)
					{
						RED_LOG_ERROR("Failed to load prefab: %hs", it.ToDebugString());
						continue;
					}

					if (prefab->TempShouldBeSavedAgain())
					{
						paths.PushBack(it.ToString());
					}
				}
				return paths;
			}
		});

		op.RegisterFunctor<void, String>("forceSavePrefab", [backend](interop::IContext& ctxt, const String& prefabResourcePath)
		{
			auto prefab = GResourceLoader->IssueLoadingRequest(res::ResourcePath::Build(prefabResourcePath))->WaitUntilLoaded<world::Prefab>();
			if (!prefab)
			{
				ctxt.ThrowException("Failed to load %s as prefab", prefabResourcePath.AsChar());
				return;
			}

			prefab->MarkModified();

			if (!res::SaveResource(prefab))
			{
				ctxt.ThrowException("Failed to save %s", prefabResourcePath.AsChar());
				return;
			}
		});

		op.RegisterObject( "recorder", interop::ObjectPtr( backend->m_sceneRecorderBackend ) );

		op.RegisterObject( "bugFlagsService", interop::ObjectPtr( backend->m_bugFlagsService ) );

		op.RegisterFunctor< void, String >( "gotoLink", [ backend ]( ::interop::IContext& ctx, const String &location )
		{
			if ( backend->m_messageLogFacilitator )
			{
				auto locationPtr = tools::IMessageLocation::BuildFromString( location );
				if ( locationPtr )
				{
					locationPtr->Goto( backend->m_messageLogFacilitator.Get() );
				}
				else
				{
					tools::EditorObjectIDPath path;
					if ( tools::EditorObjectIDPath::FromString( location, path ) )
					{
						backend->m_messageLogFacilitator->Goto( path );
					}
				}
			}
		} );

	}

	//-----

} // interop
