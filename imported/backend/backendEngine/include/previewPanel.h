/**
* Copyright (c) 2015 CD Projekt Red. All Rights Reserved.
*/

#pragma once

#include "../../../common/interop/include/interopUtilsManual.h"
#include "../../../common/redReflection/include/redReflectionPublic.h"
#include "../../../common/redReflection/include/editorObjectIdPath.h"
#include "../../../common/generalEditorBackend/include/generalEditorBackendPublic.h"
#include "../../../common/generalEditorBackend/include/cameraLocation.h"

#include "objectCollection.h"
#include "previewPanel.h"
#include "previewInput.h"
#include "previewInputHandler.h"
#include "renderingWindow.h"
#include "renderingWindowManager.h"

class CCubeTexture;
class CRenderFrameInfo;
class CRenderCameraFrameInfo;
class CWorld;

namespace rend
{
	struct SingleScreenShotData;
	struct ScreenshotBatchData;
	struct GridGeneratorData;
}
namespace world
{
	class EnvironmentAreaParameters;
	class EnvironmentDefinition;
	class RuntimeScene;
}

namespace tools
{
	class ResourceBackend;
	class IPreviewEditMode;
	class IEditActionHistory;
	struct EditModeViewport;
	class ObjectCollection;
	class EnvironmentManager;

	class IPreviewInputHandler;
	class IPreviewInputListener;
	class IPreviewDragDropHandler;

	struct InputKeyEvent;
	struct InputMouseClick;
	struct InputMouseMove;
	struct InputMouseWheel;

	class PreviewShortcutInputHandler;

	class PreviewPanelSharedData;
	typedef red::SharedPtr<PreviewPanelSharedData> PreviewPanelSharedDataPtr;

	/// Shared data listener
	class BACKEND_ENGINE_API IPreviewPanelSharedDataListener
	{
	public:
		virtual ~IPreviewPanelSharedDataListener() = default;
		virtual void OnEditModeChanged() = 0;
	};


	class BACKEND_ENGINE_API CameraClipboardCoordinates : public ISerializable
	{
		RTTI_DECLARE_TYPE(CameraClipboardCoordinates);
		RED_BASE_CLASS(ISerializable);
	public:
		red::DynArray< String > m_entries;
		red::DynArray< String > m_prefixes;
	};

	/// Basic preview panel
	/// Capable of displaying any kind of scene since it has it's own world
	class BACKEND_ENGINE_API PreviewPanel : public IPreviewPanelSharedDataListener
	{
		RED_USE_POLYMORPHIC_MEMORY_POOL( red::PoolBackend );
		DECLARE_INTEROP( PreviewPanel );

	public:
		struct SetupInfo
		{
			String m_defaultEnvMode;

			SetupInfo()
				: m_defaultEnvMode( "none" )
			{}

			SetupInfo( const String& envModeName )
				: m_defaultEnvMode( envModeName )
			{}
		};

		/// Request for showing/hiding of the mouse cursor.
		enum MouseCursorShowHideRequest : Uint8
		{
			MOUSE_CURSOR_SHOW = 1,
			MOUSE_CURSOR_HIDE = 2
		};

		enum ScreenshotFlags
		{
			DisableMotionBlur,
			Scale
		};

		PreviewPanel( RenderingWindowPtr renderingWindow, const PreviewPanelSharedDataPtr& sharedState, const SetupInfo& setupIno = SetupInfo() );
		virtual ~PreviewPanel();

		/// Get internal ID assigned to this preview panel
		const Uint32 GetID() const { return m_panelID; }

		/// Handle generic message from editor
		virtual void OnEditorMessage(const String& message, const void* userData);

		/// Initialize is called just after the preview is created (see usage of OnPreviewPanelRequest, Initialize is called after that method)
		virtual Bool Initialize() { return true; };

		/// Focus on selected bounds
		void FocusOnBounds( const Box& bounds, const Bool snap = false );

		/// Zoom out to show all scene content
		void ZoomOut( Bool snap = true );

		virtual Box GetSceneBounds() const { return Box::EMPTY(); }

		virtual red::SharedPtr< world::RuntimeScene > GetRuntimeScene();

		//----

		// Cancel current input handler
		void CancelInputHandler();

		// Set new input handler (current one will be canceled)
		// Handler will be owned by the edit mode, all calls to the handler pointer are ILLEGAL
		void AttachInputHandler( red::UniquePtr< IPreviewInputHandler > handler );

		// Attach input listener, by default the input listener is attached at the end of the stack, you can override it
		void AttachInputListener( IPreviewInputListener* listener, Bool atTheBack = true );

		// Detach input listener from the listener queue
		void DetachInputListener( IPreviewInputListener* listener );

		//----

		void CancelCommand();
		void FinishCommand();
		Bool IsCommandActive( IPreviewCommand* command ) const;
		void SetActiveCommand( red::SharedPtr< IPreviewCommand > command );
	
		red::SharedPtr< ShortcutManager >& GetShortcutManager() { return m_shortcutManager; }

		/// Request showing or hiding of the mouse cursor. This request will be processed during the nearest input update.
		void RequestMouseCursorShowHide( MouseCursorShowHideRequest request );

		//----

		// Render selection map and extract information like: selection IDs, depth values from given area (can be a single pixel)
		// Returned IDs can be converted to selection proxies via IRenderSelectable::ResolveSelection()
		void RenderSelectionMap( const Rect& selectionRect, red::SharedPtr<rend::SelectionResult>& selectionResult, Bool buffered = false, Bool isMultiLayer = false ) const;

		// Render depth map and extract depth informations
		void RenderDepthMap( const Rect& viewportRect, red::SharedPtr<rend::DepthResult>& depthResult, red::SharedPtr<rend::Color10Result> normalResult = nullptr ) const;
		
		/// Get the shared scene state
		const PreviewPanelSharedDataPtr& GetSharedData() const { return m_sharedState; }

		/// Get unique ID of the preview panel
		const Uint32 GetPreviewPanelID() const { return m_panelID; }

		/// Get context name for shortcuts
		const CName GetContextName() const { return m_contextName; }

		/// Get panel width
		Uint32 GetWidth() const { return m_viewport->GetWidth(); }

		/// Get panel height
		Uint32 GetHeight() const { return m_viewport->GetHeight(); }

		Vector3 GetCameraPos() const;

		RenderingWindowCamera* GetCamera() const { return m_viewport->GetCamera(); }

		Bool IsOrbitCameraEnabled() const;

		virtual void EnableOrbitCamera( Bool enable );
		void EnableOrbitCameraInternal( Bool enable, const Vector3 orbitCenter );

		/// Get 3D point location for given viewport coordinates (in pixels)
		/// Note: this is slow, as it needs to read from depth map
		Bool GetPointAt( Uint32 x, Uint32 y, Vector3& outPoint ) const;

		/// Jump camera to a given point on screen (in pixels)
		Bool CameraJumpTo( Uint32 x, Uint32 y );

		// prepare useful set of variables that describe the physical dimension of the viewport and the rendering camera
		// those values are required to do any kind of ray tracing, selection, etc.
		void SetupEditModeViewport( EditModeViewport& vpt ) const;

		Int32 GetMouseX() const { return m_lastMouseX; }
		Int32 GetMouseY() const { return m_lastMouseY; }
		Bool IsMouseCursorOverPreviewPanel() const;

		virtual void SelectObject( const EditorObjectIDPath& path );

		//Call when the editor this panel belongs to gains focus
		void OnGainFocus();

		//Call when the editor this panel belongs to loses focus
		void OnLoseFocus();

		virtual void SetTimeOfDay( Float timeOfDay );
		virtual void SetEnvProbeYawAngle( Float angle );
		virtual Float GetTimeOfDay();
		virtual Float GetEnvProbeYawAngle();

		// Full-Screenshot
		void TakeFullScreenshot( const rend::SingleScreenShotData& screenshotData, Bool allowDebugDraw = false );
		void TakeBatchFullScreenshot(const rend::SingleScreenShotData& screenshotData, const red::DynArray< std::pair< CameraLocation, String > >& coords,
			const Float delayTime = 10.0f, const Uint32 dumpCoordsCount = 0, res::ResourcePath resPath = res::ResourcePath() );
		void StopScreenshotBatch();

	protected:
		/// Additional input handling - use with care
		virtual Bool OnInput( input::IInputContext& input );

		/// Called at the beginning of the frame
		virtual void OnFrameBegin( RenderContext& ctx );

		/// Scene update
		virtual void OnUpdate( const UpdateContext& ctx );

		/// Scene render (debug fragments only)
		virtual void OnRender( RenderContext& ctx );

		virtual void InitializeBaseEnvironment();
		virtual void InitializeEnvironmentInfo( CRenderFrameInfo& info );
		virtual void PerformOverrides( CRenderFrameInfo& info );


		/// Get the internal viewport
		/// DO NOT EXPOSE TO PUBLIC!
		RenderingWindow* GetViewport() { return m_viewport.Get(); }
		const RenderingWindow* GetViewport() const { return m_viewport.Get(); }
		const THandle< EnvironmentManager >& GetEnvironmentManager() { return m_envManager; }

		virtual void RenderDebugFragments( rend::IDebugDrawer& dd );
		virtual void RenderDebugOvelay( rend::IDebugDrawer& dd );

		virtual void OnEditModeChanged() override;

		THandle<world::EnvironmentDefinition> m_defaultEnvironmentDefinition;

		Int32						m_lastMouseX;
		Int32						m_lastMouseY;

	private:
		struct SZoomState
		{
			SZoomState ();
			void Reset();
			bool operator!=( const SZoomState &other ) const { return !operator==( other ); }
			bool operator==( const SZoomState &other ) const;

			Float m_div;
			Float m_offsetX;
			Float m_offsetY;
		};

		struct SZoomData
		{
			SZoomData ();
			void Reset();
			void SetZoomTarget( Float currentTime, Float transitionStartTime, const SZoomState &newTarget );
			void TransformCoords( Float currentTime, Int32 x, Int32 y, Int32 &outX, Int32 &outY ) const;
			SZoomState GetState( Float time ) const;

			Float		m_startTime;
			SZoomState	m_startState;
			SZoomState	m_targetState;
		};

		/// Input handler (capture mode input) for camera orbiting around target point
		class CameraOrbitInputHandler : public IPreviewInputHandler
		{
		public:
			CameraOrbitInputHandler( PreviewPanel* panel, const Vector3& targetPoint );
			virtual Bool OnMouseUp( const InputMouseButton& evt ) override;
			virtual Bool OnMouseMoved( const InputMouseMove& evt ) override;
			virtual void OnCanceled() override;
		private:
			PreviewPanel* m_panel;
			EulerAngles m_baseCameraRotation;
			Vector3 m_baseCameraPosition;
			Vector3 m_targetPoint;
			Float m_rotationX;
			Float m_rotationY;
		};

		struct BatchedScreenshotData
		{
			CameraLocation m_cameraLocation;
			Float m_delayTime;
			String m_prefixStr;
		};

		/// Scene update & render
		void UpdateAndRender( RenderWindowFrame& frame );

		/// Shortcuts
		void RegisterShortcuts();

		Bool OnShowContextMenu( Int32 mouseX, Int32 mouseY );

		/// Input handling
		void NotifyInputListeners( EInputKey key, Bool isDown );
		void NotifyInputListeners( EMouseButton button, Bool isDown );

		Bool HandleMouseButton( const EMouseButton button, const Bool state, const Int32 x, const Int32 y );
		Bool HandleMouseClick( const EMouseButton button, const Bool dbl, const Int32 x, const Int32 y );
		Bool HandleMouseMovement( const Int32 x, const Int32 y, const Float rawDx, const Float rawDy );
		Bool HandleMouseWheel( const Float delta, const Int32 x, const Int32 y );
		Bool HandleKeyPress( const EInputKey key );
		Bool HandleKeyRelease( const EInputKey key );
		Bool HandleZoom( const Bool zoomOut, const Int32 x, const Int32 y );

		Bool HandleDragEnter( const String& formats, const String& data, Int32 x, Int32 y );
		Bool HandleDragOver( const String& data, Int32 x, Int32 y );
		void HandleDragLeave( const String& data );
		Bool HandleDrop( const String& data, Int32 x, Int32 y );
		Bool HandleLocalDragAndDrop( input::IInputContext& ctx );

		Bool HandleDirectKeyDown( const InputKeyEvent& evt );
		Bool HandleDirectKeyUp( const InputKeyEvent& evt );
		Bool HandleDirectMouseDown( const InputMouseButton& evt );
		Bool HandleDirectMouseUp( const InputMouseButton& evt );
		Bool HandleDirectMouseClick( const InputMouseClick& evt );
		Bool HandleDirectMouseDoubleClick( const InputMouseClick& evt );
		Bool HandleDirectMouseMoved( const InputMouseMove& evt );
		Bool HandleDirectMouseWheel( const InputMouseWheel& evt );
		Bool HandleDirectDragEnter( const String& data, const InputMouseMove& evt );
		Bool HandleDirectDragOver( const InputMouseMove& evt );
		void HandleDirectDragLeave();
		Bool HandleDirectDragDrop( const InputMouseButton& evt );

		// Mini-Screenshots for camera history
		String TakeScreenshot( Uint32 width, Uint32 height, const String& label, Uint32 flags );
		void InitializeScreenshotFrame( CRenderFrameInfo& info, CRenderCamera& renderCamera, Uint32 width, Uint32 height );

		void ProcessInputHandlerChange( Bool keepAlive );
		void CancelActiveDragDropOperation();
		
		void UpdateScreenshotBatcher(const UpdateContext& ctx);

		void GenerateGridJson();
		
		PreviewPanelSharedDataPtr	m_sharedState;			// shared state between multiple panel

		Uint32						m_panelID;				// editor unique ID of the preview panel
		CName						m_contextName;			// used to identify shortcuts

		RenderingWindowPtr			m_viewport;				// owned rendering window

		Bool						m_disableTonemapping;	// disable tonemapping

		Uint8						m_mouseButtonMask;		// mask of pressed mouse buttons

		Int32						m_mouseTotalTravel;				// calculate distance of moved mouse cursor when a mouse button is pressed (TODO: input system could do that)
		interop::InteropNotifier	m_contextMenuNotifier;
		interop::InteropNotifier	m_orbitNotifier;
		interop::InteropNotifier	m_runningScreenshotBatchNotifier;
		interop::InteropNotifier    m_screenshotTakenNotifier;

		SZoomData					m_zoomData;

		bool						m_useExtenedDynamicRangeSky;

		red::UniquePtr< IPreviewInputHandler >		m_inputHandler;
		red::SharedPtr< IPreviewCommand > m_inputCommand;
		red::UniquePtr< IPreviewDragDropHandler >	m_dragDropHandler;
		red::DynArray< IPreviewInputListener* >		m_inputListeners;

		red::UniquePtr< ObjectCollection > m_envManagerCollection;
		THandle< EnvironmentManager > m_envManager;

		red::UniquePtr< ObjectCollection > m_screenshotDataCollection;
		THandle< rend::SingleScreenShotData > m_screenshotData;
		red::UniquePtr< ObjectCollection > m_screenshotBatchCollection;
		THandle< rend::ScreenshotBatchData > m_screenshotBatchData;
		red::UniquePtr< ObjectCollection > m_gridGeneratorCollection;
		THandle< rend::GridGeneratorData > m_gridGeneratorData;

		red::DynArray< BatchedScreenshotData > m_batchedScreenshotsData;
		res::ResourcePath m_screenshotCoordsResource;
		Bool m_screenshotCoordsDump;
		Bool m_screenshotTaken = false;
		Bool m_isRunningScreenshotBatch = false;

		String m_defaultEnvMode;

		red::SharedPtr< ShortcutManager > m_shortcutManager;
		red::UniquePtr< PreviewShortcutInputHandler > m_shortcutInputHandler;

		Bool m_requestMouseCursorHide;   // Set by some input handlers as means to safely show/hide cursor without the need...
		Bool m_requestMouseCursorShow;   // ... to directly fiddle with IInputSystem from inside the handler code.

		Bool m_motionBlurEnabled;

		static const char sm_envBasePath[];
	};
} // tools
