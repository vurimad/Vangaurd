/**
* Copyright (c) 2015-2020 CD Projekt Red. All Rights Reserved.
*/

#pragma once

#include "../../../common/engine/include/externalMessage.h"
#include "../../../common/renderData/include/renderViewportWindowInterface.h"
#include "../../../common/interop/include/interopUtilsManual.h"

#include "../../../common/redReflection/include/lastNodeSelection.h"
#include "../../../common/redReflection/include/serializableValueNotifier.h"

#include "session.h"
#include "resourceCommandWrapper.h"
#include "resourceCommandFactory.h"

#include "resourceEditorFwd.h"
#include "MessageLog.h"

namespace input
{
	class IInputContext;
}

namespace rend
{
	class EngineViewportManager;
}

namespace res
{
	class IBackendResourceDepot;
	class ResourceMetadata;
}

namespace tick
{
	struct Info;
}

namespace resdb
{
	class IResourceDatabaseProvider;
}

namespace tools
{
	namespace prv
	{
		class ResModificationListener;
	}

	class RUIDGenerator;
	class RenderingWindowManager;
	class EnvironmentPresetsManager;
	class IObjectClipboard;
	class BatchTool;
	class ShortcutInteropBackend;
	class TweakDbInteropHelper;
	class IPrefabProxyReconciler;
	class BaseSceneRecorder;
	class JiraCreateIssueBody;
	class BugFlagsSystem;

	struct ResourceBackendSetup
	{
		ResourceBackendSetup()
			: m_depot( nullptr )
			, m_loader( nullptr )
			, m_viewportManager( nullptr )
			, m_sinks{ red::PoolBackend() }
		{}

		::res::IBackendResourceDepot* m_depot;
		::res::ResourceLoader* m_loader;
		::rend::EngineViewportManager* m_viewportManager;
		MessageSinks m_sinks;
	};

	class ResourceBackend;
	class CommandsHandler : red::NonCopyable
	{
		RED_USE_MEMORY_POOL( red::PoolBackend );

	public:
		CommandsHandler( ResourceBackend* backend );

		ResourceCommandFactory& GetFactory() { return *m_commandFactory; }

		const THandle< ResourceCommandOptions >& GetCachedOptions() const { return m_cachedOptions; }

		ResourceCommandWrapperSharedPtr GetCommand( const res::ResourcePath& path );
		void AddCommand( const res::ResourcePath& path, ResourceCommandWrapperSharedPtr& command );
		void RemoveCommand( const res::ResourcePath& path );

	private:
		typedef red::HashMap< res::ResourcePath, ResourceCommandWrapperSharedPtr > TResourceCommands;
		TResourceCommands m_resourceCommands;

		red::UniquePtr< ResourceCommandFactory > m_commandFactory;
		THandle< ResourceCommandOptions > m_cachedOptions;
	};

	class BACKEND_ENGINE_API IResourceBackendListener
	{
		RED_USE_MEMORY_POOL( red::PoolBackend );

	public:
		virtual ~IResourceBackendListener() {}

		virtual void OnEditorOpened( const String& editorName ) = 0;
		virtual void OnEditorClosed( const String& editorName ) = 0;
		virtual void OnAllEditorsClosed() = 0;
	};

	/// Resource backend for the editor
	class BACKEND_ENGINE_API ResourceBackend
	{
		DECLARE_INTEROP( ResourceBackend );
		RED_USE_MEMORY_POOL( red::PoolBackend );

	public:

		typedef std::function< void( Float, const tick::Info& ) > TickFunction;

		explicit ResourceBackend( const ResourceBackendSetup& setup );
		virtual ~ResourceBackend();

		void AttachListener( IResourceBackendListener* listener );
		void DetachListener( IResourceBackendListener* listener );

		/// Update the opened resource editors and preview panels
		void UpdateAndRender( Float timeDelta, const tick::Info& tickInfo );

		/// Process input (dispatched to the right editor)
		void ProcessInput( input::IInputContext& context );

		/// Process messages from the engine
		void ProcessMessages( const red::DynArray<ExternalMessage>& messages );

		/// Create new resource, will create new resource, does not replace existing resources
		Bool CreateResource( const res::ResourcePath& path, const String& options );

		// Get resource editor that has the given resource open
		// Returns nullptr if there is no resource editor that has the resource open
		ResourceEditorPtr GetResourceEditorByPath( const res::ResourcePath& path ) const;

		// Get resource editor by name
		ResourceEditorPtr GetResourceEditorByName( const String& name ) const;

		// Get all resource editors
		void GetResourceEditors( red::DynArray< ResourceEditorPtr >& editors ) const;

		// Get all resource editors for specified resource type
		void GetResourceEditorsByResourceType( const rtti::ClassType* resourceType, red::DynArray< ResourceEditorPtr >& editors ) const;

		// Create resource editor for given resource, returns false if it couldn't find an editor for the resource
		Bool CreateResourceEditor( const res::ResourcePath& path, const String& editorName, const String& args = String::EMPTY() );

		// Get list of modified files
		void GetModifiedFiles( red::DynArray< res::ResourcePath >& outModifiedFiles ) const;

		// Get list of opened files (files with existing editors)
		void GetOpenedFiles( red::DynArray< res::ResourcePath >& outOpenedFiles ) const;

		// Check if the resource is loaded
		Bool IsResourceLoaded( const res::ResourcePath& path ) const;

		// Check if the resource is marked as modified
		Bool IsResourceModified( const res::ResourcePath& path ) const;

		// Check if the resource is opened for editing
		Bool IsResourceOpened( const res::ResourcePath& path ) const;

		// Check if the resource is read only in the depot
		Bool IsResourceReadOnly( const res::ResourcePath& path ) const;

		// Check if a resource exists
		Bool ResourceExists( const res::ResourcePath& path ) const;

		/// Save specified file under a different path
		Bool SaveResourceAs( const res::ResourcePath& path, const res::ResourcePath& dstPath, Bool allowOverride = false );

		/// Save specified resource
		Bool SaveResourceAs( const THandle< CResource >& srcRes, const res::ResourcePath& dstPath, Bool allowOverride = false );

		/// Save resource copy - use only if you really need to save resource copy outside of the depot (ie backup files)
		Bool SaveResourceCopy( const THandle< CResource >& srcRes, const red::AbsolutePath& dstPath, Bool allowOverride = false );

		/// Save specified opened resource file
		Bool SaveResource( const res::ResourcePath& path, Bool updateThumbnail = true );

		/// Save specified direct file
		Bool SaveResource( const THandle< CResource >& res, Bool updateThumbnail = true );

		/// Reload resource file, does nothing if file is not loaded (obviously)
		/// When the file is already loaded a new version is loaded form the disk and all interested parties are notified
		/// NOTE: it's all manual work to connect the "reload" even into the appropiate places in the engine and the editor, sorry, no free lunch
		/// Calling this method clear the "modified" status of the file
		Bool ReloadResource(const res::ResourcePath& path);

		// Second stage of reloading resoures, notify all interested parties after the resource has been reloaded
		Bool FinishReloadingResource( const res::ResourceTokenHandle& token );

		// Loads all deferred data buffers which should release the async handle to the resource file
		void FinishLoadingResource( const res::ResourcePath& path );

		/// Close all resource editors
		void CloseAllEditors();

		/// Prepare for closing specific editor
		void PreCloseEditorByName( const String& name );

		/// Close specific editor
		void CloseEditorByName( const String& name );

		/// Close specific editor
		void CloseEditorByPath( const res::ResourcePath& path );

		/// Very fancy - scan depot path to discover sub directories
		/// NOTE: requires direct file access, will not work with non-native data sources (packages)
		void FindDepotSubDirectories( const String& searchDepotPath, red::DynArray< String >& subDirectoryNames ) const;

		/// Very fancy - scan depot path to discover files
		/// NOTE: requires direct file access, will not work with non-native data sources (packages)
		void FindDepotFiles( const String& searchDepotPath, const AnsiChar* pattern, red::DynArray< String >& fileNames ) const;

		/// Remove depot file
		Bool RemoveDepotFile( const res::ResourcePath& depotPath );

		/// Get the rendering window manager
		RenderingWindowManager* GetRenderingWindowManager() const;

		// Get the resource depot associated with this backend
		res::IBackendResourceDepot* GetResourceDepot() const;

		// Get the resource loader associated with this backend
		res::ResourceLoader* GetResourceLoader() const;

		/// Get the backend clipboard (shared between everything)
		IObjectClipboard* GetClipboard() const;

		// Create/recreate icons for a resource
		Bool GenerateIcon(const res::ResourcePath& resourcePath, const red::AbsolutePath& output);

		// Create/recreate thumbnail for a resource
		Bool GenerateThumbnail( const res::ResourcePath& resourcePath, Bool forceUpdate, CName prefferedAppearanceName = CName::NONE() );

		// Get thumbnail generation notifier
		interop::InteropNotifier& GetThumbnailNotifier() { return m_thumbnailGenNotifier; }

		// Get body parts tags changed notifier
		interop::InteropNotifier& GetTagsChangedNotifier() { return m_tagsChangedNotifier; }

		/// Get RUID generator. Can create local instance, but faster if can reuse this one.
		RUIDGenerator& GetRUIDGenerator() const { return *m_ruidGenerator; }

		CommandsHandler& GetCommandHandler() const { return *m_commandsHandler; }

		resdb::IResourceDatabaseProvider& GetResourceDatabase() const { return *m_resourceDatabase; }

		red::SharedPtr< ShortcutInteropBackend > GetShortcutBackend() const { return m_shortcutBackend; }

		/// Add new editor to container and refresh settings in session object
		void AddNewEditor( ResourceEditorPtr newEditor );

		void NotifyEditorMessage( const String& editorName, const String& message );

		/// Inform toolkit about new editor creation process
		RED_INLINE void NotifyNewEditorCreationSuccess( const String& editorName ) { m_newEditorCreationSuccess.Notify( editorName ); }
		RED_INLINE void NotifyNewEditorCreationFailed(const String& editorName ) { m_newEditorCreationFailed.Notify( editorName ); }
		void NotifyNewEditorCreationFailReason( const String& resource, const String& reason );

		String GetLastError() { return m_lastErrorString; }
		void SetLastError( const String& err ) { m_lastErrorString = err; }

		/// Set last node selection, we track last selection of any node in all open editors.
		/// It is used to optimize some cross editor communication.
		/// For example this is used by global node ID picker ("red/green arrow" button in ValueBoxNodeRef) to get node global ID.
		void SetLastNodeSelection( IResourceEditor* resourceEditor, const EditorObjectIDPath& selectedNode );

		red::SharedPtr< EnvironmentPresetsManager > GetEnvironmentPresetsManager();


		// notify everybody that the resource has been changed/updated
		// used in case of reimport/reload
		void NotifyResourceReloaded( const res::ResourcePath& path, const THandle<CResource>& newResource );

		// register/unregister for tick
		void RegisterForTick( const void* owner, TickFunction tickFunction );
		void UnregisterForTick( const void* owner );

		void OnDataError( const red::String& dataErrorAction );

		void OpenWebBrowser( const red::String& link ) const;

		struct Metadata
		{
			THandle< res::ResourceMetadata > metadata;
			red::UniquePtr< IEditActionHistory > actionHistory;
			red::UniquePtr< ObjectCollection > objectCollection;

			Metadata()
			{
				actionHistory = red::CreateUniquePtr< IEditActionHistory >( nullptr );
				objectCollection = red::CreateUniquePtr< ObjectCollection >( *actionHistory.Get() );
			}

			Metadata( Metadata&& ) = default;
			Metadata& operator=( Metadata&& ) = default;
		};

		Metadata* GetMetadata( const res::ResourcePath& path );
		void ClearMetadata( const res::ResourcePath& path );

		MessageLog& GetMessageLog() { return m_messageLog; }
		const MessageLog& GetMessageLog() const { return m_messageLog; }

		red::SharedPtr< IMessageLocationGotoFacilitator > GetMessageLogFacilitator() { return m_messageLogFacilitator; }

		// Read the resource's file header to get the GPU size of that resource
		Uint32 ReadGPUSizeForResource( const res::ResourcePath& resPath );

	private:
		// internal helper function for creation resource editor
		ResourceEditorPtr FindExistingEditorForResource( const res::ResourcePath& path ) const;
		Bool CreateResourceEditorCreator( const res::ResourcePath& path, const String& editorName, const String& args );
		void UpdateEditorCreators( Float timeDelta );
		void ReloadDelayedResources();
		void ForceRefreshResourceDatabase(Bool readonly = true);
		void UpdateResourceDatabaseSync();
		Bool TryRefreshResourceDataBase();
		red::AbsolutePath GetResourceDatabaseFilePath() const;

		typedef red::DynArray< ResourceEditorPtr >	TResourceEditors;
		typedef red::Map< const void*, TickFunction > TTickFunctions;

		void PreCloseEditor( TResourceEditors::iterator iter );
		void CloseEditor( TResourceEditors::iterator iter );

		void SaveTags( const THandle< CResource >& res, const res::ResourcePath& path );
		void SaveAnimationTags( const THandle< CResource >& res, const res::ResourcePath& path );

		void OnNotifyPropertyChange( const SerializableID& id, const rtti::AccessPath& path );

		// IResourceBackendListener events
		void NotifyEditorOpened( const String& editorName );
		void NotifyEditorClosed( const String& editorName );
		void NotifyAllEditorsClosed();

		typedef red::Mutex TLock;
		mutable TLock m_lock;

		TResourceEditors m_editors;							/// opened resource editors

		red::DynArray< IResourceBackendListener* > m_listeners;

		red::SharedPtr< ShortcutInteropBackend > m_shortcutBackend;

		typedef red::DynArray< ResourceEditorCreatorPtr > TEditorCreators;
		TEditorCreators m_activeResourceEditorCreators;		/// active editor creators

		::res::IBackendResourceDepot* m_depot;						// low level interface to the resource depot
		::res::ResourceLoader* m_loader;					// low level resource loader

		// List of resources to reload after a delay
		// This is needed as reloading the resources in the CloseEditor method cannot be done so they
		// have to be delayed until outside of the interop command
		red::DynArray< res::ResourcePath > m_resourcesToReload;
		red::DynArray< res::ResourceTokenHandle > m_pendingReloadRequests;

		typedef red::UniquePtr< RenderingWindowManager > RenderingWindowManagerPtr;
		RenderingWindowManagerPtr m_renderingWindowManager;

		red::UniquePtr< IObjectClipboard >	m_clipboard;					// low level clipboard support

		interop::InteropNotifier m_editorMessage;				// inform toolkit about generic message
		interop::InteropNotifier m_newEditorCreationSuccess;	// inform toolkit about success of creation new editor
		interop::InteropNotifier m_newEditorCreationFailed;		// inform toolkit about failure of creation new editor
		interop::InteropNotifier m_newEditorCreationFailReason;	// inform toolkit why editor creation failed

		interop::InteropNotifier m_fileModified;				// inform toolkit that a file was modified
		interop::InteropNotifier m_fileUnmodified;				// inform toolkit that a file was
		interop::InteropNotifier m_errorNotifier;
		interop::InteropNotifier m_resourceSaved;

		interop::InteropNotifier m_dataErrorNotifier;			// inform toolkit about data error

		interop::InteropNotifier m_openWebBrowserNotifier;

		SerializableValueNotifier m_serializableNotifier;

		red::HashSet< String > m_modifiedFiles;					// list of modified files, can be queried

		typedef red::UniquePtr< prv::ResModificationListener > ResModificationListenerPtr;
		ResModificationListenerPtr m_modificationListener;

		interop::InteropNotifier m_thumbnailGenNotifier;
		interop::InteropNotifier m_tagsChangedNotifier;

		red::UniquePtr< RUIDGenerator > m_ruidGenerator;

		String m_lastErrorString;
		red::SharedPtr< EnvironmentPresetsManager > m_envPresetsManager;

		LastNodeSelection m_lastNodeSelection;          // Use to track the last selection of node that happened in any of the editors.
		red::UniquePtr< CommandsHandler > m_commandsHandler;

		red::UniquePtr< BatchTool > m_batchTool;

		red::SharedPtr< BaseSceneRecorder > m_sceneRecorderBackend;
		red::SharedPtr< class BugFlagsService > m_bugFlagsService;

		TLock m_tickFunctionsLock;
		TTickFunctions m_tickFunctions;

		using MetadataMap = red::Map< res::ResourcePath, Metadata >;
		MetadataMap m_metadata;

		MessageLog m_messageLog;
		red::SharedPtr< IMessageLocationGotoFacilitator > m_messageLogFacilitator;
		red::UniquePtr< TweakDbInteropHelper > m_tweakDbInteropHelper;
		red::UniquePtr< IPrefabProxyReconciler > m_prefabProxyReconciler;

		// NOTE: If you need to use this database from a commandlet then go use resdb::Database directly
		// and *DO NOT* use it through this interface. This is a wrapper *ONLY* for Asset Browser
		red::UniquePtr< resdb::IResourceDatabaseProvider > m_resourceDatabase;
		red::UniquePtr< resdb::IResourceDatabaseProvider > m_pendingNewResourceDatabase;
		job::Counter m_resourceDatabaseCounter;
		interop::InteropNotifier m_resourceDatabaseRefreshedNotifier;
		interop::InteropNotifier m_updateStreamingCacheSyncProgress;
		interop::InteropNotifier m_updateFindAllPrefabsToForceSaveProgress;

		Bool m_requestRefreshResourceDatabase;
		red::Atomic<Bool> m_refreshResourceDatabaseFinished;
		red::Map< res::ResourcePath, Uint32 > m_resourcePathToGPUSize;
	};
} // tools
