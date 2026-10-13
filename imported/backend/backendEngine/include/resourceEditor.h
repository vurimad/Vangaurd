/**
* Copyright (c) 2015-2020 CD Projekt Red. All Rights Reserved.
*/

#pragma once

#include "backendMemory.h"
#include "../../../common/interop/include/interopUtilsManual.h"
#include "../../../common/redReflection/include/editorObjectIdPath.h"
#include "selectionManager.h"

namespace red
{
	class Event;
}

namespace res
{
	class Monitor;
}

namespace tools
{
	class IEditAction;
	class IEditActionHistory;

	class PreviewPanel;
	typedef red::SharedPtr<PreviewPanel> PreviewPanelPtr;

	class RenderingWindow;
	typedef red::SharedPtr<RenderingWindow> RenderingWindowPtr;

	struct ResourcePreviewInitContext;
	class ResourceBackend;
	class IResourceEditMode;

	class ObjectCollection;

	class ResourceEditorCreator;

	struct ResourceEditorParameters
	{
		red::String			m_name;
		res::ResourcePath	m_path;
		ResourceBackend*	m_backend;
		red::String			m_args;

		RED_INLINE ResourceEditorParameters() : m_backend( nullptr ) { }
		RED_INLINE ResourceEditorParameters( const red::String& name, const res::ResourcePath& path, ResourceBackend* backend, const String& args = String::EMPTY() )
			: m_name( name ), m_path( path ), m_backend( backend ), m_args( args ) { }
	};

	enum class EntityMatchType : Uint8
	{
		None,
		Selected,
		All,
	};

	class BACKEND_ENGINE_API IResourceEditorListener
	{
		RED_USE_MEMORY_POOL( red::PoolBackend );

	public:
		virtual ~IResourceEditorListener() {}

		virtual void OnPreviewPanelCreated( const String& editorName, Uint32 panelID ) {}
		virtual void OnPreviewPanelDestroyed( const String& editorName, Uint32 panelID ) {}
		virtual void OnAllPreviewPanelsDestroyed( const String& editorName ) {}
		virtual void OnBugFlagAdded( const String& editorName, const res::ResourcePath& resourcePath, const String& cameraLocationStr, const Vector3& flagPosition ) {}
	};

	/// Base class for all resource editor functionalities
	class BACKEND_ENGINE_API IResourceEditor : public red::EnableSharedFromThis< IResourceEditor >
	{
		RED_USE_POLYMORPHIC_MEMORY_POOL( red::PoolBackend );
		RTTI_DECLARE_POLYMORPHIC_TYPE( IResourceEditor );
		DECLARE_INTEROP( IResourceEditor );

		// Need to remove this friend ASAP do not rely on it
		friend class ResourceEditorCreator;

	public:
		IResourceEditor();
		virtual ~IResourceEditor();

		/// Get and set the resource editor interop ID
		RED_FORCE_INLINE const String& GetName() const { return m_name; }

		RED_FORCE_INLINE const String& GetEditorTypeName() const { return m_editorTypeName; }

		/// Get additional initialization args
		RED_FORCE_INLINE const String& GetArgs() const { return m_args; }

		/// Get resource backend
		RED_FORCE_INLINE ResourceBackend* GetBackend() const { return m_backend; }

		/// Get action history - direct access to undo/redo
		RED_FORCE_INLINE IEditActionHistory& GetAccessHistory() { return *m_actionHistory; }

		/// Get action history - direct access to undo/redo
		RED_FORCE_INLINE const IEditActionHistory& GetAccessHistory() const { return *m_actionHistory; }

		virtual CName RequestUIState() const { return RED_NAME_CONSTEXPR("inkGameState"); }

		/// Get one object collection to handle global selection that is displayed in main property grid in the editor frontend
		ObjectCollection* GetSharedSelection( CName id = consts::selection );

		/// Get all preview panels created in this editor
		RED_FORCE_INLINE const red::DynArray<PreviewPanelPtr>& GetPreviewPanels() const { return m_previewPanels; }

		Bool SelectObject( const EditorObjectIDPath& path );

		/// Set editor's initial parameters
		void SetInitialParameters( const ResourceEditorParameters& params );

		/// Initialize editor, may fail
		virtual Bool Initialize( const THandle< CResource >& resource ) = 0;

		/// Is the given class supported by the editor?
		virtual const Bool SupportsResourceClass( const rtti::ClassType* resourceClass ) const = 0;

		Bool QueueEventToEntities( const THandle< red::Event >& evt, const res::ResourcePath& entTemplate, EntityMatchType type = EntityMatchType::Selected ) const;

		void AttachListener( IResourceEditorListener* listener );
		void DetachListener( IResourceEditorListener* listener );

		void NotifyPreviewPanelCreated( Uint32 panelID );
		void NotifyPreviewPanelDestroyed( Uint32 panelID );
		void NotifyAllPreviewPanelsDestroyed();
		void NotifyBugFlagAdded( const String& cameraLocationStr, const Vector3& flagPosition );

		//-----

		/// Close this editor without saving the content
		void Close();
		virtual void OnPreClose();

		/// Is the resource modified ?
		virtual Bool IsModified() const;

		// Checks various conditions and returns true if a subsequent call to Save() would succeed
		// If it returns false then a save can still be done but the Save is not guaranteed to succeed
		// This makes sure that all of the files are writable, and will return false if at least one is not
		virtual Bool CanSave() const;

		// Called when a specific resource is about to be saved.
		// Returning 'false' will abort the process.
		virtual Bool OnResourcePreSave( const THandle< CResource >& resource );

		// Save all of the modified resources in the editor
		// Returns true when all have saved successfully, false when there was an error
		// Clears the modified flag on saved resources
		// Detailed save results can be represented by 'successfullySaved' and 'failedToSave'
		virtual Bool Save( red::DynArray< res::ResourcePath >& successfullySaved = red::DynArray< res::ResourcePath >(),
						   red::DynArray< res::ResourcePath >& failedToSave = red::DynArray< res::ResourcePath >() );

		// Saves a copy of all of the modified resources to another location on disk
		virtual Bool AutoSave();

		// Reloads all of the edited resources in this editor
		virtual Bool Reload();

		//-----
		// Primary Resource

		/// Get the resource being edited by this editor
		virtual const THandle< CResource > GetResource() const = 0;

        template< typename T >
        const THandle<T> GetResource() const;

		/// Get the path of the edited resource
		const res::ResourcePath& GetPath() const;

		//-----
		// Other edited resources

		// Gets all of the modified resources in the editor
		virtual void GetModifiedResources( red::DynArray< res::ResourcePath >& resourcePaths ) const;
		virtual void GetModifiedResources( red::DynArray< THandle<CResource> >& resources ) const;

		// Gets all of the opened resources in the editor - these may not be modified yet but are opened for potential editing
		virtual void GetOpenedResources( red::DynArray< res::ResourcePath >& resourcePaths ) const;
		virtual void GetOpenedResources( red::DynArray< THandle<CResource> >& resources ) const;

		// Checks if the resource is being edited by this editor
		bool IsResourceOpened( const res::ResourcePath& path ) const;

		// Checks if the resource can be edited by this editor
		bool CanEditResource( const res::ResourcePath& path ) const;

		//---

		/// request a preview panel of given type to be created with given rendering window
		/// note: only one preview panel can exist for a given window handle
		/// returns panel ID of 0 if not created
		Uint32 CreatePreviewPanel(const String& type, Uint64 parentWindowHandle);

		/// close preview panel
		Bool DestroyPreviewPanel(const Uint32 panelId);

		/// close all preview panels
		void DestroyAllPreviewPanels();

		/// get preview panel by ID
		PreviewPanelPtr GetPreviewPanel(const Uint32 panelId) const;

		/// send a generic message to any preview panel, usually a "refresh", "reset", etc
		void SendPreviewMessage(const String& message, const void* userData = nullptr);

		/// factory for viewports, should return new viewport or NULL if we failed to initialize one
		virtual void OnPreviewPanelRequest(const String& type, RenderingWindowPtr renderingWindowPtr, PreviewPanelPtr& outPanel);

		/// notify the editor that the resource edited in THIS editor has been reloaded/reimported
		/// called AFTER changing the m_resource pointer
		virtual Bool HasReloadSupport() const { return false; }
		virtual Bool OnOpenResourceReloaded( const THandle<CResource>& newResource );

		//----

		// call the activation notifier so the frontend can react to the window being activated
		RED_FORCE_INLINE void SendActivateNotification() const { m_activateNotifier.Notify(); }

		// call the notifier so the frontend can react to the window being closed
		RED_FORCE_INLINE void SendCloseNotification() const { m_closeNotifier.Notify(); }

		// call the notifier to pass window focus request to the frontend
		RED_FORCE_INLINE void SendWindowFocusRequestNotification() const { m_windowFocusRequestNotifier.Notify(); }

		// Action was just executed on the resource editor, this is called regardless on the reason of such action (normal operation, undo or redo)
		virtual void OnActionExecute( const IEditAction& action );

		// Get the user context associated with the editor resource
		virtual THandle<ISerializable> GetResourceUserContext() { return nullptr; };

		/// Get node's editor ID path by global node ID path.
		/// For example we might want to find a node by such a string: "$/world15/area7/prefabInstance4/turret2".
		virtual tools::EditorObjectIDPath GetNodeEditorPathByGlobalIDPath( const String& globalIDPath ) const;

		void NotifyOpenedResourcesChanged() const;

		void ExposeSelectionManagerInterop( ::interop::utils::Object& op );

		void OnGainedFocus();
		void OnLostFocus();
	
		void NotifyEditorMessage( const red::String& message );

protected:
		virtual Bool OnQueueEventToEntities( const THandle< red::Event >& evt, const res::ResourcePath& entTemplate, EntityMatchType type ) const { return false; }

		/// Get the resource view
		RED_FORCE_INLINE ObjectCollection& GetResourceView() { return *m_resourceView; }

		/// Signal all resource tools to refresh it's state
		void RefreshTools();

		Bool IsAutoSaveEnabled() const;

		// This is called as the editor is being closed so that derived classes can 
		// mark all of their resources as not modified in preparation of being discarded.
		// This is a bad name for what the function does
		virtual void DiscardModifiedResources();

	private:

		// create resource tools
		void CreateTools();

		// backend (for saving the resource)
		ResourceBackend* m_backend;

		// additional args that can be passed on editor initialization
		String m_args;

		// path of the edited resource
		res::ResourcePath m_path;

		// resource editor name/id, used for exposing the editor via interop
		String m_name;

		// name of the editor type eg world editor
		String m_editorTypeName;

		// action history, controls undo/redo. Exposed through interop.
		red::UniquePtr< IEditActionHistory > m_actionHistory;

		// Resource being edited (undoable view)
		red::UniquePtr< ObjectCollection > m_resourceView;

		// general manipulation by interop
		interop::InteropNotifier m_activateNotifier;
		interop::InteropNotifier m_closeNotifier;
		interop::InteropNotifier m_windowFocusRequestNotifier;

		interop::InteropNotifier m_openedResourcesChanged;

		// preview panels owned by this editor
		typedef red::DynArray<PreviewPanelPtr> TPreviewPanels;
		TPreviewPanels m_previewPanels;

		// resource monitor checking for the main resource reload condition
		red::UniquePtr< res::Monitor > m_resourceMonitor;

		red::UniquePtr< SelectionManager > m_selectionManager;

		red::DynArray< IResourceEditorListener* > m_listeners;

		//----

		struct ToolWrapper;

		// Resource tools supported on this resource
		typedef red::DynArray< ToolWrapper > TPipelineTools;
		TPipelineTools m_tools;
	};

	//----

	// Helper functions for configuration naming for editors

	// Constructs a config system group identifier of "editor/<group>/<editor_class_name>/<resource_hash>"
	BACKEND_ENGINE_API String ConstructResourceRelatedConfigGroupName(const String& group, const IResourceEditor& editor );

	// Constructs a config system group identifier of "editor/<group>/<editor_class_name>"
	BACKEND_ENGINE_API String ConstructEditorRelatedConfigGroupName(const String& group, const IResourceEditor& editor );

	// Constructs a config system group identifier of "editor/<group>"
	BACKEND_ENGINE_API String ConstructGlobalEditorConfigGroupName(const String& group);

    template< typename T >
    const THandle<T> IResourceEditor::GetResource() const
    {
        return Cast<T>(GetResource());
    }


	//////////////////////////////////////////////////////////////////////////
	class BACKEND_ENGINE_API ResourceEditorCreator final
	{
		RED_USE_MEMORY_POOL( red::PoolBackend );

	public:
		ResourceEditorCreator( ResourceBackend* backend, const rtti::ClassType* editorClassType, const res::ResourcePath& path, const String& editorName, const String& args );
		~ResourceEditorCreator();

		Bool Update( Float timeDelta );

		RED_INLINE const res::ResourcePath& GetResourcePath() const { return m_resourcePath; }

	private:
		String m_editorName;
		res::ResourcePath m_resourcePath;
		const rtti::ClassType* m_editorClassType;

		res::ResourceTokenHandle m_resourceLoadingToken;
		ResourceBackend* m_backend;
		String m_args;
	};

} // res

