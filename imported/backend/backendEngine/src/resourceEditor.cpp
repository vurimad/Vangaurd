/**
* Copyright (c) 2013-2020 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"
#include "resourceEditor.h"
#include "resourceBackend.h"
#include "previewPanel.h"
#include "editAction.h"
#include "resourceTool.h"

//
#include "../../../common/redReflection/include/resourceUtils.h"
#include "../../../common/redReflection/include/resourceMonitor.h"
#include "../../../common/inkCore/include/inkSystem.h"

RTTI_BEGIN_ABSTRACT_TYPE_IN_NAMESPACE( IResourceEditor, tools );
RTTI_END_TYPE();

namespace Config
{
	TConfigVar< Bool > cvAutoSaveEnabled( "Editor/AutoSave", "Enabled", false, eConsoleVarFlag_Save );
	TConfigVar< Bool > cvNewSceneTreeEnabled( "Editor/SceneTree", "Enabled", true, eConsoleVarFlag_Save );
	TConfigVar< Bool > cvEnableMultiViewport( "Editor/Experimental", "MultiViewport", false, eConsoleVarFlag_Save | eConsoleVarFlag_Developer );
}

namespace tools
{
	struct IResourceEditor::ToolWrapper
	{
		RED_USE_MEMORY_POOL( red::PoolBackend );
		DECLARE_INTEROP(ToolWrapper);

		IResourceEditor*						m_editor;
		THandle< IResourceTool >				m_tool;
		red::UniquePtr<ObjectCollection>		m_properties;
		red::DynArray< IResourceTool::Action >	m_actions;
	};

	IResourceEditor::IResourceEditor()
		: m_backend( nullptr )
		, m_previewPanels{ red::PoolBackend() }
		, m_tools{ red::PoolBackend() }
	{
		m_actionHistory = red::CreateUniquePtr< IEditActionHistory >( this );
		m_resourceView = red::CreateUniquePtr< ObjectCollection >( *m_actionHistory );
		m_selectionManager = red::CreateUniquePtr< SelectionManager >( *m_actionHistory );
	}

	IResourceEditor::~IResourceEditor() = default;

	ObjectCollection* IResourceEditor::GetSharedSelection( CName id /*= consts::selection */ )
	{
		return m_selectionManager->GetSharedSelection( id );
	}

	Bool IResourceEditor::SelectObject( const EditorObjectIDPath& path )
	{
		if ( m_previewPanels.Empty() )
			return false;

		m_previewPanels[ 0 ]->SelectObject( path );

		return true;
	}

	void IResourceEditor::SetInitialParameters( const ResourceEditorParameters& params )
	{
		m_name = params.m_name;
		m_path = params.m_path;
		m_backend = params.m_backend;
		m_args = params.m_args;
	}

	Bool IResourceEditor::IsModified() const
	{
		red::DynArray< res::ResourcePath > resources{ red::PoolBackend() };
		GetModifiedResources( resources );
		return !resources.Empty();
	}

	const res::ResourcePath& IResourceEditor::GetPath() const
	{
		return m_path;
	}

	void IResourceEditor::GetModifiedResources( red::DynArray< res::ResourcePath >& resourcePaths ) const
	{
		const auto resource = GetResource();
		if ( resource->IsModified() )
		{
			resourcePaths.PushBack( GetPath() );
		}
	}

	void IResourceEditor::GetModifiedResources( red::DynArray< THandle<CResource> >& resources ) const
	{
		const auto resource = GetResource();
		if ( resource->IsModified() )
		{
			resources.PushBack( resource );
		}
	}

	void IResourceEditor::GetOpenedResources( red::DynArray< res::ResourcePath >& resourcePaths ) const
	{
		resourcePaths.PushBack( GetPath() );
	}

	void IResourceEditor::GetOpenedResources( red::DynArray< THandle<CResource> >& resources ) const
	{
		resources.PushBack( GetResource() );
	}

	bool IResourceEditor::IsResourceOpened( const res::ResourcePath& path ) const
	{
		red::DynArray<res::ResourcePath> openedResources{ red::PoolBackend() };
		GetOpenedResources( openedResources );
		return openedResources.Exist( path );
	}

	bool IResourceEditor::CanEditResource( const res::ResourcePath& path ) const
	{
#ifdef RED_ONLY_ALLOW_EDITING_IN_ONE_EDITOR
		// We can only edit a resource if we are the editor already or there is no existing editor
		const IResourceEditor* ptr = m_backend->GetResourceEditorByPath( path ).Get();
		return ptr == this || ptr == nullptr;
#else // !RED_ONLY_ALLOW_EDITING_IN_ONE_EDITOR
		// For now we will always allow editing as it simplifies the logic significantly
		// If in future we find a need to restrict this we can re-enable this feature
		return true;
#endif
	}

	////----

	PreviewPanelPtr IResourceEditor::GetPreviewPanel(const Uint32 panelId) const
	{
		for (const auto& ptr : m_previewPanels)
		{
			if (ptr->GetID() == panelId)
			{
				return ptr;
			}
		}

		return nullptr;
	}

	void IResourceEditor::SendPreviewMessage(const String& message, const void* userData)
	{
		for (const auto& ptr : m_previewPanels)
		{
			ptr->OnEditorMessage(message, userData);
		}
	}

	void IResourceEditor::OnPreviewPanelRequest(const String& type, RenderingWindowPtr renderingWindowPtr, PreviewPanelPtr& outPanel)
	{
		// by default we don't have any preview
	}

	Bool IResourceEditor::OnOpenResourceReloaded(const THandle<CResource>& newResource)
	{
		if ( HasReloadSupport() )
		{
			SendPreviewMessage( "refresh" );

			m_resourceView->SetObject( newResource.Get(), false, GetResourceUserContext() );

			m_actionHistory->Clear();

			return true;
		}
		else
		{
			const String reason = "Opened resource has been reloaded and the editor does not support that. It will be closed now to prevent data loss.";
			m_closeNotifier.Notify( reason );

			return false;
		}
	}

	Uint32 IResourceEditor::CreatePreviewPanel(const String& type, Uint64 parentWindowHandle)
	{
		RED_LOG_INFO("Got request to create preview window '%hs' on parent handle 0x%016llx", type.AsChar(), parentWindowHandle);

		// create the rendering window
		auto renderingWindowPtr = m_backend->GetRenderingWindowManager()->RequestPreviewWindow(parentWindowHandle, m_editorTypeName);
		if (!renderingWindowPtr)
		{
			RED_LOG_ERROR("Failed to create rendering window");
			return 0;
		}

		const ViewportID viewportId = renderingWindowPtr->GetViewportID();
		if ( auto viewportWrapper = GetInkSystem().GetViewportWrapper( viewportId ).Lock() )
		{
			viewportWrapper->SpawnLayers_Editor( RequestUIState() );
		}

		// ask the resource editor to create the actual preview window
		PreviewPanelPtr panel;
		OnPreviewPanelRequest(type, renderingWindowPtr, panel);
		if (!panel)
		{
			RED_LOG_ERROR("Failed to create requested preview panel type '%hs'", type.AsChar());
			return 0;
		}

		if (panel->Initialize() == false)
		{
			RED_LOG_ERROR("Failed to initialize requested preview panel type '%hs'", type.AsChar());
			return 0;
		}

		// keep around until destroyed
		m_previewPanels.PushBack(panel);
		NotifyPreviewPanelCreated( panel->GetID() );

		return panel->GetID();
	}

	Bool IResourceEditor::DestroyPreviewPanel(const Uint32 panelId)
	{
		for (const auto& ptr : m_previewPanels)
		{
			if (ptr->GetID() == panelId)
			{
				m_previewPanels.Remove(ptr);
				RED_LOG_INFO("Removed preview panel ID %d", panelId);

				NotifyPreviewPanelDestroyed( panelId );
				return true;
			}
		}
		return false;
	}

	void IResourceEditor::DestroyAllPreviewPanels()
	{
		m_previewPanels.Clear();
		NotifyAllPreviewPanelsDestroyed();
	}

	////----

	void IResourceEditor::DiscardModifiedResources()
	{
		red::DynArray< THandle<CResource> > modifiedResources{ red::PoolBackend() };
		GetModifiedResources( modifiedResources );
		for ( const auto& resource : modifiedResources )
		{
			resource->UnmarkModified();
		}
	}

	void IResourceEditor::NotifyEditorMessage( const red::String& message )
	{
		if ( m_backend )
		{
			m_backend->NotifyEditorMessage( m_name, message );
		}
	}

	Bool IResourceEditor::QueueEventToEntities( const THandle< red::Event >& evt, const res::ResourcePath& entTemplate, EntityMatchType type /*= EntityMatchType::Selected */ ) const
	{
		return OnQueueEventToEntities( evt, entTemplate, type );
	}

	void IResourceEditor::AttachListener( IResourceEditorListener* listener )
	{
		red::alg::PushBackUnique( m_listeners, listener );
	}

	void IResourceEditor::DetachListener( IResourceEditorListener* listener )
	{
		m_listeners.Remove( listener );
	}

	void IResourceEditor::NotifyPreviewPanelCreated( Uint32 panelID )
	{
		for ( IResourceEditorListener* listener : m_listeners )
		{
			listener->OnPreviewPanelCreated( m_name, panelID );
		}
	}

	void IResourceEditor::NotifyPreviewPanelDestroyed( Uint32 panelID )
	{
		for ( IResourceEditorListener* listener : m_listeners )
		{
			listener->OnPreviewPanelDestroyed( m_name, panelID );
		}
	}

	void IResourceEditor::NotifyAllPreviewPanelsDestroyed()
	{
		for ( IResourceEditorListener* listener : m_listeners )
		{
			listener->OnAllPreviewPanelsDestroyed( m_name );
		}
	}

	void IResourceEditor::NotifyBugFlagAdded( const String& cameraLocationStr, const Vector3& flagPosition )
	{
		for ( IResourceEditorListener* listener : m_listeners )
		{
			listener->OnBugFlagAdded( m_name, GetPath(), cameraLocationStr, flagPosition );
		}
	}

	void IResourceEditor::Close()
	{
		if ( m_backend )
		{
			m_backend->NotifyEditorMessage( m_name, "Started executing OnPreClose" );
		}

		OnPreClose();

		if ( m_backend )
		{
			m_backend->NotifyEditorMessage( m_name, "Finished executing OnPreClose" );
		}

		// delete all tools
		m_tools.Clear();

		if ( m_backend )
		{
			m_backend->PreCloseEditorByName( GetName() );
		}

		// Clear the modified flag on all of our loaded resources
		// At this point all resources should be saved or we are safe to discard them
		if ( m_backend )
		{
			m_backend->NotifyEditorMessage( m_name, "Started discarding modified resources" );
		}

		DiscardModifiedResources();

		if ( m_backend )
		{
			m_backend->NotifyEditorMessage( m_name, "Finished discarding modified resources" );
		}

		// Close all preview panels now before the actual editor object is destroyed
		// This should be the only thing required for nice cleanup
		DestroyAllPreviewPanels();

		if ( m_backend )
		{
			m_backend->CloseEditorByName( GetName() );
			// NOTE: This is now an invalid object, it was destroyed by the backend
		}
	}

	void IResourceEditor::OnPreClose()
	{}

	Bool IResourceEditor::CanSave() const
	{
		// we can only save if there's a core backend
		if ( !m_backend )
			return false;

		red::DynArray< res::ResourcePath > modifiedResourcePaths{ red::PoolBackend() };
		GetModifiedResources( modifiedResourcePaths );
		if ( modifiedResourcePaths.Empty() )
			return false; // nothing to save

		for ( const auto& resourcePath : modifiedResourcePaths )
		{
			if ( m_backend->IsResourceReadOnly( resourcePath ) )
			{
				return false;
			}
		}

		return true;
	}

	Bool IResourceEditor::OnResourcePreSave( const THandle< CResource >& resource )
	{
		RED_FATAL_ASSERT( resource );
		return true;
	}

	Bool IResourceEditor::Save( red::DynArray< res::ResourcePath >& successfullySaved, red::DynArray< res::ResourcePath >& failedToSave )
	{
		// we can only save if there's a core backend
		if ( !m_backend )
			return false;

		bool result = true;

		red::DynArray< THandle<CResource> > modifiedResources{ red::PoolBackend() };
		GetModifiedResources( modifiedResources );
		red::alg::RemoveDuplicates( modifiedResources );

		for ( const auto& resource : modifiedResources )
		{
			if ( !OnResourcePreSave( resource ) || !m_backend->SaveResource( resource ) )
			{
				// We need to try and save all of the resources just in case
				// one resource out of many is not writable.
				result = false;
				failedToSave.PushBack( resource->GetPath() );
			}
			else
			{
				successfullySaved.PushBack( resource->GetPath() );
			}
		}

		return result;
	}

	Bool IResourceEditor::AutoSave()
	{
		// we can only save if there's a core backend
		if ( !m_backend )
			return false;

		if ( !IsAutoSaveEnabled() )
			return false;

		Bool success = true;

		red::DynArray< THandle<CResource> > modifiedResources{ red::PoolBackend() };
		GetModifiedResources( modifiedResources );
		red::alg::RemoveDuplicates( modifiedResources );

		for ( const auto& resource : modifiedResources )
		{
			const red::AbsolutePath backupFilePath = res::GetBackupPath( resource->GetPath() );
			if ( !m_backend->SaveResourceCopy( resource, backupFilePath, true ) )
			{
				RED_LOG( "Saving backup file %hs for resource %hs failed.",
					backupFilePath.AsChar(), resource->GetPath().ToString().AsChar() );
				success = false;
			}
		}

		return success;
	}

	Bool IResourceEditor::Reload()
	{
		if ( !m_backend )
			return false;

		bool success = true;

		red::DynArray< res::ResourcePath > openedResourcePaths{ red::PoolBackend() };
		GetOpenedResources( openedResourcePaths );
		for ( const auto& resourcePath : openedResourcePaths )
		{
			if ( !m_backend->ReloadResource( resourcePath ) )
			{
				success = false;
			}
		}

		return success;
	}

	void IResourceEditor::OnActionExecute( const IEditAction& action )
	{
		if ( action.IsModification() )
		{
			// action was just executed on resource, make sure it's marked as modified
			GetResource()->MarkModified();
		}
	}

	EditorObjectIDPath IResourceEditor::GetNodeEditorPathByGlobalIDPath( const String& globalIDPath ) const
	{
		return EditorObjectIDPath();
	}

	void IResourceEditor::RefreshTools()
	{
		for (auto& tool : m_tools)
			tool.m_tool->Refresh();
	}

	Bool IResourceEditor::IsAutoSaveEnabled() const
	{
		return Config::cvAutoSaveEnabled.Get();
	}

	void IResourceEditor::CreateTools()
	{
		red::DynArray< const rtti::ClassType* > toolClasses{ red::PoolBackend() };
		GetRttiSystem().EnumClasses(ClassID< IResourceTool >(), toolClasses);

		static IEditActionHistory nullHistory(nullptr);

		const THandle< CResource > resource = GetResource();

		for (const auto* toolClass : toolClasses)
		{
			const auto* defTool = toolClass->GetDefaultObject< IResourceTool >();
			if (defTool && defTool->SupportsResource(*resource))
			{
				ToolWrapper wrapper;
				wrapper.m_editor = this;
				wrapper.m_tool = toolClass->CreateHandle< IResourceTool >();
				if (!wrapper.m_tool->Initialize(this))
				{
					RED_LOG_WARNING("Editor: Failed to initialize resource tool '%hs' for resource '%hs'", toolClass->GetName().AsChar(), GetPath().ToString().AsChar());
					continue;
				}

				wrapper.m_tool->Bind(resource);
				wrapper.m_tool->GetActions(wrapper.m_actions);
				if (wrapper.m_actions.Empty())
				{
					RED_LOG_WARNING("Editor: Tool '%hs' for resource '%hs' does not expose any actions", toolClass->GetName().AsChar(), GetPath().ToString().AsChar());
				}

				wrapper.m_properties = red::CreateUniquePtr< ObjectCollection >(nullHistory); // tool properties are not causing undo operations
				wrapper.m_properties->SetObject(wrapper.m_tool.Get());

				m_tools.PushBack(std::move(wrapper));
			}
		}
	}

	void IResourceEditor::ToolWrapper::SetupInterop(ToolWrapper* ptr, ::interop::utils::Object& op)
	{
		// properties of the root object
		op.RegisterObject("data", ptr->m_properties->GetRootInterop());

		// tool wrapper tile
		op.RegisterValueRO<String>("title", String(ptr->m_tool->GetTitle()) );

		// action data and names
		op.RegisterArray("actions", ptr->m_actions.Size(), [ptr](interop::IContext& ctx, Uint32 index)
		{
			return ptr->m_actions[index].CreateInterop();
		});

		// actions themselves
		for (auto& a : ptr->m_actions)
		{
			const auto name = a.m_name;
			op.RegisterFunctor<Bool>(name.AsChar(), [ptr, name](interop::IContext& ctx)
			{
				auto action = ptr->m_tool->PerformAction(ctx, name);
				if (!action)
					return ctx.ThrowException("Unable to execute action");

				if (!ptr->m_editor->GetAccessHistory().Execute( std::move( action ) ))
					return ctx.ThrowException("Action failed to execute");

				return true;
			});
		}
	}

	void IResourceEditor::NotifyOpenedResourcesChanged() const
	{
		m_openedResourcesChanged.Notify();
	}

	void IResourceEditor::ExposeSelectionManagerInterop( ::interop::utils::Object& op )
	{
		return m_selectionManager->ExposeInterop( op );
	}

	void IResourceEditor::OnGainedFocus()
	{
		for ( auto &panel : m_previewPanels )
		{
			panel->OnGainFocus();
		}
	}

	void IResourceEditor::OnLostFocus()
	{
		for ( auto &panel : m_previewPanels )
		{
			panel->OnLoseFocus();
		}
	}

	void IResourceEditor::SetupInterop( IResourceEditor* resEditor, ::interop::utils::Object& op )
	{
		// each resource editor has a name, this allows to retrieve it
		op.RegisterValue<String>( "name",
			/*GET*/	[ resEditor ]( interop::IContext& ctx, String& outValue ) { outValue = resEditor->GetName(); } );

		// path to the resource being edited
		op.RegisterValue<String>( "_path",
			/*GET*/	[ resEditor ]( interop::IContext& ctx, String& outValue ) { outValue = resEditor->GetResource()->GetPath().ToString(); } );

		// name of the class of the resource being edited
		op.RegisterValue<String>( "_class",
			/*GET*/	[ resEditor ]( interop::IContext& ctx, String& outValue ) { outValue = String( resEditor->GetResource()->GetClass()->GetName().AsChar() ); } );

		// access the raw, unwrapped resource data
		op.RegisterObject( "data", resEditor->m_resourceView->GetRootInterop() );

		// register undo redo
		op.RegisterObject( "actions", resEditor->GetAccessHistory() );

		// list of modified files
		op.RegisterValue<red::DynArray<String>>( "modifiedResources", [ resEditor ]( ::interop::IContext& ctx, red::DynArray<String>& outValues )
		{
			red::DynArray< res::ResourcePath > resources{ red::PoolBackend() };
			resEditor->GetModifiedResources( resources );
			red::alg::RemoveDuplicates( resources );
			outValues.Reserve( resources.Size() );
			for ( const auto& path : resources )
			{
				outValues.PushBack( path.ToString() );
			}
		} );

		// list of opened files
		op.RegisterNotifier( "openedResourceChanged", resEditor->m_openedResourcesChanged );
		op.RegisterValue<red::DynArray<String>>( "openedResources", [ resEditor ]( ::interop::IContext& ctx, red::DynArray<String>& outValues )
		{
			red::DynArray< res::ResourcePath > resources{ red::PoolBackend() };
			resEditor->GetOpenedResources( resources );
			red::alg::RemoveDuplicates( resources );
			outValues.Reserve( resources.Size() );
			for ( const auto& path : resources )
			{
				outValues.PushBack( path.ToString() );
			}
		} );

		op.RegisterFunctor<Bool, String>( "isEditingResource", [ resEditor ]( interop::IContext& ctx, String resourcePath )
		{
			return resEditor->IsResourceOpened( res::ResourcePath::Build( resourcePath ) );
		} );

		// is the resource modified ?
		op.RegisterFunctor<Bool>( "isModified", [ resEditor ]( interop::IContext& ctx )
		{
			return resEditor->IsModified();
		} );

		// close the editor
		op.RegisterFunctor<Bool>( "close", [ resEditor ]( interop::IContext& ctx )
		{
			resEditor->Close();
			return true;
		} );

		op.RegisterFunctor<Bool>( "canSave",
			[ resEditor ]( ::interop::IContext& ctx )
		{
			return resEditor->CanSave();
		} );

		// save the resource
		op.RegisterFunctor<String>( "save", [ resEditor ]( interop::IContext& ctx )
		{
			// TODO: this is a first approach to send more data to the user, it would be nice to send something typed and handle it accordingly in the editor
			// but some things need to be prepared beforehand:
			// - stop invoking actions in the global sink to be able to strong type the response where it's expected
			// - appropriate interop types should be created in interopInterchange and should have some logic to be able to connect with action popup ( maybe ToString at first )
			// - food for thoughts, extend interop::IContext so that it can take positive/neutral messages instead of just exceptions

			red::DynArray< res::ResourcePath > saved{ red::PoolBackend() };
			red::DynArray< res::ResourcePath > failed{ red::PoolBackend() };
			Bool result = resEditor->Save( saved, failed );

			red::StringBuilder< String > builder;

			if ( !saved.Empty() )
			{
				tools::Message logMessage( MessageSeverity::Success, std::move( RED_LOCATION( resEditor->GetPath() ) ) );
				logMessage << RED_NAME_CONSTEXPR( "Save" );
				logMessage << "successful:";

				builder.Append( "Saved:\n" );

				for ( const res::ResourcePath& path : saved )
				{
					// New messagelog implementation
					logMessage << path;

					// Possibly deprecated notification to editor frontend
					String resourcePath = path.ToString();
					red::AbsolutePath path = red::AbsolutePath::ParseFilePath( resourcePath );
					String name = red::paths::ExtractFileName( path );

					builder.Append( name );
					builder.Append( " " );
					builder.Append( resourcePath );
					builder.Append( "\n" );
				}

				IMessageReporter::ReportSafe( logMessage );
			}

			if ( !failed.Empty() )
			{
				tools::Message logMessage( MessageSeverity::Error, std::move( RED_LOCATION( resEditor->GetPath() ) ) );
				logMessage << "The following resources failed to";
				logMessage << RED_NAME_CONSTEXPR( "Save" );
				logMessage << ":";

				builder.Append( "Failed to save:\n" );

				for ( Uint32 index : failed.Indices() )
				{
					// New messagelog implementation
					logMessage << failed[ index ];

					// Possibly deprecated notification to editor frontend
					String resourcePath = failed[ index ].ToString();

					red::AbsolutePath path = red::AbsolutePath::ParseFilePath( resourcePath );
					String name = red::paths::ExtractFileName( path );

					builder.Append( name );
					builder.Append( " " );
					builder.Append( resourcePath );

					if ( index != saved.Size() - 1 )
						builder.Append( "\n" );
				}

				IMessageReporter::ReportSafe( logMessage );
			}
			else if ( !result )
			{
				// No resources reported as failing to save, but something went wrong nevertheless
				RED_REPORT_ERROR( RED_LOCATION( resEditor->GetPath() ), "% failed", RED_NAME_CONSTEXPR( "Save" ) );
			}

			if ( result && saved.Empty() && failed.Empty() )
			{
				RED_REPORT_INFO( RED_LOCATION( resEditor->GetPath() ), "Nothing to %", RED_NAME_CONSTEXPR( "Save" ) );
			}

			return builder.ToString();
		} );

		op.RegisterFunctor<void>( "autoSave", [ resEditor ]( interop::IContext& ctx )
		{
			resEditor->AutoSave();
		} );

		op.RegisterFunctor<void, String>( "setEditorTypeName", [ resEditor ]( interop::IContext& ctx, String editorTypeName )
		{
			resEditor->m_editorTypeName = editorTypeName;
		} );

		// reload the resource, will loose all changes
		op.RegisterFunctor<Bool>( "reload", [ resEditor ]( interop::IContext& ctx )
		{
			return resEditor->Reload();
		} );

		op.RegisterFunctor<void>( "gained_focus", [ resEditor ]( interop::IContext& ctx )
		{
			resEditor->OnGainedFocus();
		} );

		op.RegisterFunctor<void>( "lost_focus", [ resEditor ]( interop::IContext& ctx )
		{
			resEditor->OnLostFocus();
		} );

		// tools
		op.RegisterObject("commands", [resEditor](interop::IContext& ctx)
		{
			auto* ret = RED_NEW( interop::utils::Object )("ActionList");

			for (auto& info : resEditor->m_tools)
				ret->RegisterObject(info.m_tool->GetName().AsChar(), info.CreateInterop());

			return interop::ObjectPtr(ret);
		});

		// preview panels map
		op.RegisterObject("previews",
			/*GET*/[resEditor](interop::IContext& ctx)
		{
			return ::interop::ObjectPtr(RED_NEW( ::interop::utils::MapObject )(
				[resEditor](interop::IContext& ctx, const String& elementName)
			{
				Uint32 panelId = 0;
				if (!FromString(elementName, panelId))
				{
					ctx.ThrowException("Invalid index!");
					return ::interop::ObjectPtr();
				}

				auto preview = resEditor->GetPreviewPanel(panelId);
				if (preview == nullptr)
				{
					ctx.ThrowException("Preview panel with that ID does not exist!");
					return ::interop::ObjectPtr();
				}

				return preview->CreateInterop();
			}));
		});

		// "preview" fallback - return the first preview window
		if (!resEditor->m_previewPanels.Empty())
		{
			op.RegisterObject("preview",
				/*GET*/[resEditor](interop::IContext& ctx)
			{
				return resEditor->m_previewPanels[0]->CreateInterop();
			});
		}

		// function to create the preview panels
		op.RegisterFunctor<Uint32, String, String>("createPreviewPanel", [resEditor](interop::IContext& ctx, const String& parentWindowHandleStr, const String& panelType) -> Uint32
		{
			Uint64 parentWindowHandle = 0;
			if ( !FromString( parentWindowHandleStr, parentWindowHandle ) )
			{
				ctx.ThrowException( "Invalid parentWindowHandle!" );
				return 0;
			}

			Uint32 panelID = resEditor->CreatePreviewPanel( panelType, parentWindowHandle );
			if ( panelID == 0 )
			{
				ctx.ThrowException( "Failed to create preview panel in the backend!" );
				return 0;
			}

			return panelID;
		});

		// function to destroy preview panels
		op.RegisterFunctor<void, Uint32>( "destroyPreviewPanel", [resEditor] ( interop::IContext& ctx, Uint32 panelID )
		{
			if ( !resEditor->DestroyPreviewPanel( panelID ) )
			{
				ctx.ThrowException( "Could not destroy preview panel" );
			}
		} );

		// external events
		op.RegisterNotifier( "_onClose", resEditor->m_closeNotifier );
		op.RegisterNotifier( "_onActivate", resEditor->m_activateNotifier );

		// window focus
		op.RegisterNotifier( "_windowFocusRequestChanged", resEditor->m_windowFocusRequestNotifier );
		op.RegisterFunctor<void>( "focusWindow", [resEditor]( ::interop::IContext& ctx )
		{
			resEditor->SendWindowFocusRequestNotification();
		} );

		resEditor->ExposeSelectionManagerInterop( op );
	}

	String ConstructResourceRelatedConfigGroupName(const String& group, const IResourceEditor& editor )
	{
		return String::Printf( "editor/%hs/%hs/0x%016llX", group.AsChar(), editor.GetClass()->GetName().AsChar(), editor.GetPath().GetHash() );
	}

	String ConstructEditorRelatedConfigGroupName(const String& group, const IResourceEditor& editor )
	{
		return String::Printf( "editor/%hs/%hs", group.AsChar(), editor.GetClass()->GetName().AsChar() );
	}

	String ConstructGlobalEditorConfigGroupName(const String& group)
	{
		return String::Printf("editor/%hs", group.AsChar());
	}

	//////////////////////////////////////////////////////////////////////////

	ResourceEditorCreator::ResourceEditorCreator( ResourceBackend* backend, const rtti::ClassType* editorClassType, const res::ResourcePath& path, const String& editorName, const String& args )
		: m_editorName( editorName )
		, m_resourcePath( path )
		, m_editorClassType( editorClassType )
		, m_resourceLoadingToken( nullptr )
		, m_backend( backend )
		, m_args( args )
	{
		// start loading
		if ( m_backend != nullptr && m_resourcePath.IsValid() )
		{
			m_backend->NotifyEditorMessage( m_editorName, "Started loading requested resource and possibly updating streaming cache" );

			m_resourceLoadingToken = m_backend->GetResourceLoader()->IssueLoadingRequest( m_resourcePath );
		}
	}

	ResourceEditorCreator::~ResourceEditorCreator()
	{
	}

	Bool ResourceEditorCreator::Update( Float timeDelta )
	{
		if( m_resourceLoadingToken != nullptr )
		{
			if( m_resourceLoadingToken->IsLoaded() )
			{
				m_backend->NotifyEditorMessage( m_editorName, "Started creating new editor for loaded resource" );

				// create new editor for loaded resource
				auto editorPtr = ResourceEditorPtr( m_editorClassType->CreateObject< IResourceEditor >() );
				ResourceEditorWeakPtr editorWeakPtr = editorPtr;

				ResourceEditorParameters editorParams;
				editorParams.m_name = m_editorName;
				editorParams.m_path = m_resourcePath;
				editorParams.m_backend = m_backend;
				editorParams.m_args = m_args;

				// set all pseudo-constructor parameters in editor
				editorPtr->SetInitialParameters( editorParams );

				auto resource = m_resourceLoadingToken->GetResource();
				//RED_FATAL_ASSERT( m_resourcePath == resource->GetPath() );
				// TEMP: Code here to detect intermittent crashes in functional tests on the build machine
				if ( m_resourcePath != resource->GetPath() )
				{
					RED_FATAL( "Expected resource paths to match! %016llX %hs != %016llX %hs",
						m_resourcePath.GetHash(), m_resourcePath.ToDebugString(),
						resource->GetPath().GetHash(), resource->GetPath().ToDebugString() );
				}

				// initialize monitoring
				editorPtr->m_resourceMonitor = red::CreateUniquePtr< res::Monitor >();
				editorPtr->m_resourceMonitor->Bind(resource->GetPath());
				editorPtr->m_resourceMonitor->SetCallback([editorWeakPtr](const res::ResourcePath& path, const CName& message, const res::MonitorUserDataPtr& userData)
				{
					// react to resource reload
					if (message == RED_NAME_CONSTEXPR_NOREG( "reload" ) )
					{
						auto newResource = res::GetMonitorUserData<THandle<CResource>>(userData);
						ResourceEditorPtr editorPtr = editorWeakPtr.Lock();
						if ( editorPtr )
						{
							editorPtr->OnOpenResourceReloaded(newResource);
						}
					}
				});

				// initialize editor
				m_backend->NotifyEditorMessage( m_editorName, "Started initializing new editor" );

				if( !editorPtr->Initialize( resource ) )
				{
					m_backend->NotifyNewEditorCreationFailed( m_editorName );
					return true;
				}

				m_backend->NotifyEditorMessage( m_editorName, "Finished initializing new editor" );

				editorPtr->m_resourceView->SetObject( resource.Get(), false, editorPtr->GetResourceUserContext() );

				// create pipeline tools for the resource
				editorPtr->CreateTools();

				// add created editor to backend and inform toolkit about new editor
				m_backend->AddNewEditor( editorPtr );
				m_backend->NotifyNewEditorCreationSuccess( m_editorName );
				RED_LOG( "Core: Create resource editor for '%hs', name = '%hs'", m_resourcePath.ToString().AsChar(), m_editorName.AsChar() );

				m_backend->NotifyEditorMessage( m_editorName, "Finished creating new editor for loaded resource" );

				return true;
			}
			else if( m_resourceLoadingToken->HasFailed() )
			{
				m_backend->NotifyNewEditorCreationFailed( m_editorName );
				return true;
			}
		}

		return false;
	}

} // tools
