/**
* Copyright (c) 2015 CD Projekt Red. All Rights Reserved.
*/

#pragma once

#include "undoFwd.h"
#include "previewFwd.h"
#include "snapManager.h"

#include "../../../common/renderData/include/renderScene.h"

namespace tick
{
	struct Info;
}

namespace world
{
	class RuntimeScene;
}

namespace tools
{
	class ResourceBackend;
	class SimpleGridSnapper;
	class IResourceEditor;
	class PreviewFiltersInterop;
	struct UpdateContext;

	enum class ConfigScope
	{
		Global,
		Editor,
		Resource
	};

	/// Shared preview panel state
	class BACKEND_ENGINE_API PreviewPanelSharedData
		: public red::EnableSharedFromThis<PreviewPanelSharedData>
		, public red::NonCopyable
	{
		RED_USE_MEMORY_POOL( red::PoolBackend );
		DECLARE_INTEROP(PreviewPanelSharedData);

	public:
		// creates new render scene of type 'sceneType' if external renderScene is nullptr
		PreviewPanelSharedData( IResourceEditor& editor, const TRenderPtr<IRenderScene> renderScene = nullptr, IRenderScene::Type sceneType = IRenderScene::PREVIEW );
		virtual ~PreviewPanelSharedData();

		//---

		// Update internal state
		void Update( const UpdateContext& ctx );

		// Render internal state
		void Render(CRenderFrameInfo& info, CRenderCameraFrameInfo& cameraInfo);

		//----

		/// send a generic message to any preview panel, usually a "refresh", "reset", etc
		void SendPreviewMessage(const String& message, const void* userData = nullptr);

		/// register a preview panel using this shared data
		/// NOTE: for now the only recommended way to communicate is to use the SendPreviewMessage
		virtual void NotifyPanelCreated(PreviewPanel* panel);

		/// unregister a preview panel using this shared data
		virtual void NotifyPanelDestroyed(PreviewPanel* panel);

		//----

		// Get current edit name
		CName GetEditModeName() const;

		// Switch edit mode, may fail
		Bool ChangeEditMode(CName editModeName);

		// Get action history - edition interface
		IEditActionHistory& GetActionHistory();

		// Get action history - edition interface
		const IEditActionHistory& GetActionHistory() const;

		// Get resource backend - edition interface
		ResourceBackend& GetResourceBackend();

		// Get parent resource editor
		RED_FORCE_INLINE IResourceEditor& GetEditor() const { return m_resourceEditor; }

		template< typename T >
		T& GetEditor() const;

		/// Get current edit mode
		RED_FORCE_INLINE IPreviewEditMode* GetEditMode() const { return m_editMode; }

        template<typename T>
        T* GetEditMode() const { return static_cast<T*>(m_editMode); }

		/// Get the main rendering scene
		RED_FORCE_INLINE const TRenderPtr<IRenderScene>& GetRenderScene() { return m_renderScene; }

		/// Get all preview panels
		typedef red::DynArray< PreviewPanel* > TPreviewPanels;
		RED_FORCE_INLINE const TPreviewPanels& GetPreviewPanels() const { return m_previewPanels; }

		RED_FORCE_INLINE SnapManager& GetSnapManager() const { return *m_snapManager; }
		RED_FORCE_INLINE SimpleGridSnapper& GetSimpleGridSnapper() const { return *m_simpleGridSnapper; }

		virtual void OnFileEdited( const res::ResourcePath& path ) const;

		// Add a resource to the list of potentially modified resources (i.e. when your editor is also modifying some other resources internally, and they also need saving).
		// It does not MarkModified, you need to do this manually.
		// We need OnResourceEdited mostly so we know which of the open editors is responsible for this resource, when we actually do MarkModified.
		// Default implementation is empty.
		virtual void OnResourceEdited( const THandle<CResource>& resource );

		Bool GetConfigValue( const ConfigScope& scope, const String& group, const String& key, String& value );
		Bool SetConfigValue( const ConfigScope& scope, const String& group, const String& key, const String& value );

		struct SelectionInfo
		{
			Float		m_minDistance;
			Uint32		m_numItems;
			Box			m_bounding;
		};

		virtual Bool	CalculateSelectionInfo( const Vector3& refPoint, SelectionInfo& info ) const { (void)refPoint;  return false; }

		// move selected object into a new position
		Bool MoveSelection( SelectionStatePtr selectionState, const Transform& targetTransform );

		//Call when the editor this belongs to comes into focus
		virtual void OnGainFocus();

		//Call when the editor this belongs to loses focus
		virtual void OnLoseFocus();

		virtual void ForceNoProxies( Bool enabled ) {}

		virtual Bool HasStreaming() const { return false; }
		virtual Bool IsStreaming() const { return false; }

	protected:
		/// Factory function type for spawning object implementing edit mode.
		typedef std::function< IPreviewEditMode*() > TEditModeFactory;
		/// Register edit mode - first parameter is the editor name for the edit mode, the second parameter is the factory function spawning the object actually implementing the edit mode.
		void RegisterEditMode(const CName editName, TEditModeFactory factoryFunc);

		void ClearEditMode( const CName editName );

		/// Custom update
		virtual void OnUpdateSharedState( const UpdateContext& ctx );

		/// Custom render
		virtual void OnRenderSharedState(CRenderFrameInfo& info, CRenderCameraFrameInfo& cameraInfo);

	private:
		IResourceEditor&			m_resourceEditor;

		TRenderPtr<IRenderScene>	m_renderScene;			// low-level rendering scene

		IPreviewEditMode*			m_editMode;				// active edit mode, NOT OWNED
		CName						m_editModeName;			// active edit mode name (from hash map)

		typedef red::HashMap< CName, TEditModeFactory > TPreviewEditModeClassList;
		TPreviewEditModeClassList	m_editModeClasses;

		typedef red::HashMap< CName, IPreviewEditMode* > TPreviewEditModes;
		TPreviewEditModes			m_editModeCache;		// OWNED

		typedef red::DynArray< PreviewPanel* > TPreviewPanels;
		TPreviewPanels				m_previewPanels;

		// snapping solver
		red::UniquePtr< SimpleGridSnapper > m_simpleGridSnapper;
		red::UniquePtr< SnapManager > m_snapManager;

		interop::InteropNotifier m_editModeChangedNotifier;	// notifier for active edit mode change
		interop::InteropNotifier m_fileEditCheckout;

		TRenderPtr<IRenderScene> CreateRenderScene( IRenderScene::Type type );
		void DeleteRenderScene();

		void RenderEditModeFrame(rend::IDebugDrawer& dd);

		red::UniquePtr< PreviewFiltersInterop > m_previewFilterHandler;
	};

	template< typename T >
	T& PreviewPanelSharedData::GetEditor() const 
	{ 
		static_assert( std::is_base_of<IResourceEditor, T>::value == true, "Error: Must be subclassed from IResourceEditor");
		return static_cast<T&>(m_resourceEditor); 
	}
} // tools