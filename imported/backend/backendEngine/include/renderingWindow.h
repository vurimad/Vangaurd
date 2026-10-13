/**
* Copyright (c) 2013-2020 CD Projekt Red. All Rights Reserved.
*/
#pragma once

#include "../../../common/interop/include/interopUtilsManual.h"
#include "../../../common/renderData/include/renderCamera.h"
#include "../../../common/renderData/include/renderFrameInfo.h"
#include "../../../common/engine/include/renderEngineViewportManager.h"

class CRenderFrameInfo;
class CRenderCameraFrameInfo;
struct RenderCameraDOFData;

namespace tick
{
	struct Info;
}

namespace gsm
{
	struct ExternalMessage;
}

namespace input
{
	class IInputContext;
}

namespace tools
{
	class RenderingWindow;

	class RenderingWindowCamera;
	typedef red::UniquePtr< RenderingWindowCamera >	CameraPtr;

	enum EProjectionMode : Int32;

	struct BACKEND_ENGINE_API UpdateContext
	{
		UpdateContext( Float timeDelta, const tick::Info& tickInfo );

		Float m_timeDelta;
		const tick::Info& m_tickInfo;
	};

	struct BACKEND_ENGINE_API RenderContext
	{
		RenderContext(CRenderFrameInfo& frameInfo, CRenderCameraFrameInfo& cameraInfo, const class CRenderCamera* camera);

		CRenderFrameInfo& m_frameInfo;
		CRenderCameraFrameInfo& m_cameraInfo;
		const class CRenderCamera* m_camera;
	};

	struct BACKEND_ENGINE_API RenderWindowFrame
	{
		RenderWindowFrame( const UpdateContext* updateCtx, RenderContext& renderCtx );

		const UpdateContext* m_updateCtx;
		RenderContext& m_renderCtx;
	};

	// process frame function handler (for integrating external systems)
	typedef std::function<void( RenderWindowFrame& )> RenderWindowProcessFunc;

	// additional input function handler (for integrating external systems)
	typedef std::function<bool(input::IInputContext&)> RenderWindowInputFunc;

	// close function handler (for integrating external systems)
	typedef std::function<void(RenderingWindow*)> RenderWindowCloseFunc;

	/// A window that can handle 3D rendering, contains rendering viewport and preview scene
	class BACKEND_ENGINE_API RenderingWindow: public red::NonCopyable
	{
		RED_USE_MEMORY_POOL( red::PoolBackend );
		DECLARE_INTEROP( RenderingWindow );

	public:
		struct RenderInfo
		{
			rend::EngineViewportPtr viewport;
			CRenderFrameInfo frameInfo;
			CRenderCameraFrameInfo cameraInfo;
		};

	public:
		RenderingWindow( rend::EngineViewportPtr viewport, RenderWindowCloseFunc closeFunc );
		virtual ~RenderingWindow();

		/// Is this a top most/active window ?
		Bool IsActiveWindow() const;

		/// Bind process frame function (will be called when it's time for this window to render)
		void BindExternalProcessFunction( RenderWindowProcessFunc func );

		/// Bind external input processing function, called BEFORE processing normal input like camera
		void BindExternalInputFunction( RenderWindowInputFunc func );

		/// Begin update and render this window
		RenderInfo UpdateAndRender( const Float engineTimeDelta, const tick::Info& tickInfo );
		
		/// Handle viewport input
		void HandleInput( input::IInputContext& input );

		/// Handle external engine message (says if viewport is active or not)
		void HandleMessage( const gsm::ExternalMessage& msg );

	public:
		// Set camera focus area without actually invoking a zoom operation
		void SetFocalArea( const Box& area );

		// Zoom in/out camera to show the assigned focal point
		void FocusCameraOn( bool snap );

		/// Set a free camera for the viewport
		void SetupFreeCamera( const Vector3& position, const EulerAngles& rotation );

		/// Setup orbiting camera for the viewport
		void SetupOrbitCamera( const Vector3& orbitPosition, const EulerAngles& rotation, const Float distance );

		// Set camera fov custom value
		void SetCustomFov(Float fov);

		// Reset to default fov value from config file
		void ResetToDefaultFov();

		// Set camera dof custom value
		void SetCustomDof( const RenderCameraDOFData& renderCameraDOF );

		// Reset to default dof value from config file
		void ResetToDefaultDof();

		// Used for previewing specific camera settings
		void SetCustomCameraTransform( const Transform& transform );

		// Reset to latest transform before override
		void ResetToDefaultCameraTransform();

		// Calculate camera based on current window settings
		void CalcCamera( CRenderCamera& outCamera ) const;

		/// Change projection mode
		void SetProjectionMode( const EProjectionMode mode );

		/// Get camera position
		Vector3 GetCameraPosition() const;

		/// Get camera rotation
		EulerAngles GetCameraRotation() const;

		/// Get streaming reference position
		Vector3 GetCameraStreamingPosition() const;

		/// Get window width
		Uint32 GetWidth() const;

		/// Get window height
		Uint32 GetHeight() const;

		// Get viewport id
		Uint64 GetViewportID() const;

		void SetViewportAspectRatio( const EAspectRatio aspectRatio );

		// Get the engine viewport
		RED_FORCE_INLINE const rend::EngineViewportPtr& GetEngineViewport() const { return m_viewport; }

		void EnableRadialMenu( Bool enable );
		Bool IsRadialMenuEnabled() const;

		// get camera
		RED_FORCE_INLINE RenderingWindowCamera* GetCamera() const { return m_camera.Get(); }

		// Render selection map and extract information like: selection IDs, depth values from given area (can be a single pixel)
		// Returned IDs can be converted to selection proxies via IRenderSelectable::ResolveSelection()
		// If 'buffered' is set to false, this function will cause render commands flush so the selection result is valid immediately.
		// Otherwise, selection result will be delayed (preferred for selection being resolved every frame).
		void RenderSelectionMap( const Rect& selectionRect, red::SharedPtr<rend::SelectionResult>& selectionResult, Bool buffered, Bool isMultiLayer );

		// Render depth map and extract depth values
		void RenderDepthMap( const Rect& viewportRect, red::SharedPtr<rend::DepthResult>& depthResult, red::SharedPtr<rend::Color10Result> normalResult = nullptr );

	private:

		rend::EngineViewportPtr		m_viewport;				// low-level rendering viewport used for the rendering
		CameraPtr					m_camera;

		Double						m_totalTime;			// internal timer
		Double						m_skippedTime;

		RenderWindowProcessFunc		m_externalProcessFunc;	// external function to update & render frame
		RenderWindowInputFunc		m_externalInputFunc;	// external function to process the input
		RenderWindowCloseFunc		m_externalCloseFunc;	// external function to call when this viewport is closed

		interop::InteropNotifier	m_cameraChangedNotifier;
		interop::InteropNotifier	m_windowActivationNotifier;
		interop::InteropNotifier	m_aspectRatioNotifier;

		red::SharedPtr<rend::SelectionResult>	m_selectionResult;		// selection result for previous  frame

		Int32						m_frameCount;			// Used to determine if this frame should be updated if the window is inactive
		Uint64						m_cameraID;
		Bool						m_isActiveWindow;
		Bool						m_isBufferedFrameSubmitted;

		/// internal debug fragments
		void RenderInternalDebugFragments( rend::IDebugDrawer& dd );
		void RenderPerAreaDebugFragments( rend::IDebugDrawer& dd, const RenderingWindowCamera& cam, const Rect& areaRect );

		/// Camera helper rendering
		void RenderCameraAxes( rend::IDebugDrawer& dd, const RenderingWindowCamera& cam, const Rect& areaRect );
		void RenderCameraInfo( rend::IDebugDrawer& dd, const RenderingWindowCamera& cam, const Rect& areaRect );

		/// Build camera render viewport
		SCameraRenderViewport BuildCameraRenderViewport( const CRenderFrameInfo &frameInfo ) const;

		friend class RenderingWindowManager;
	}; 

} // tools

