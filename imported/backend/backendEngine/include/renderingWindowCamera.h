/**
* Copyright (c) 2013-2018 CD Projekt Red. All Rights Reserved.
*/
#pragma once

#include "../../../common/renderData/include/renderCamera.h"
#include "../../../common/interop/include/interopNotifier.h"
#include "../../../common/interop/include/interopUtilsManual.h"
#include "../../../common/renderData/include/renderFrameInfo.h"
#include "../../../common/generalEditorBackend/include/cameraLocation.h"
#include "../../../common/redConfig/include/configVar.h"

enum EInputAction : Int32;
enum EInputKey : Int32;

namespace input
{
	class IInputContext;
}

namespace Config
{
	BACKEND_ENGINE_API extern TConfigVar< Float > cvFOV;
}

namespace tools
{
	/// Camera for the preview system
	class BACKEND_ENGINE_API RenderingWindowCamera
	{
		RED_USE_MEMORY_POOL( red::PoolBackend );
		DECLARE_INTEROP( RenderingWindowCamera );

	public:
		RenderingWindowCamera( interop::InteropNotifier& cameraChanged );
		virtual ~RenderingWindowCamera();

		// get camera location
		CameraLocation GetCameraLocation() const;
		void SetCameraLocation( const CameraLocation& cameraLocation );

		// get camera position
		RED_FORCE_INLINE const Vector3& GetPosition() const { return m_position; }

		// get camera rotation
		RED_FORCE_INLINE const EulerAngles& GetRotation() const { return m_rotation; }

		// get camera orbiting distance
		RED_FORCE_INLINE const Float GetOrbitDistance() const { return m_orbitDistance; }

		// is this orbiting camera?
		RED_FORCE_INLINE const Bool IsOrbiting() const { return m_mode == EMode::Orbit; }

		// is this a free camera?
		RED_FORCE_INLINE const Bool IsFree() const { return m_mode == EMode::Free; }

		// is this a camera with orthogonal projection?
		RED_FORCE_INLINE const Bool IsOrthogonal() const { return m_projectionMode != PM_Perspective; }
		RED_FORCE_INLINE const EProjectionMode GetProjectionMode() const { return m_projectionMode; }

		const Float GetFOV() const;
		RED_FORCE_INLINE const Float GetZoom() const { return m_zoom; }

		// get input mouse mode (debug)
		RED_FORCE_INLINE const Int32 GetInputMouseMode() const { return m_input.m_mouseMode; }

		// Get and set transform that is applied just before sending final camera position and rotation to rendering
		RED_FORCE_INLINE const Transform& GetAdditionalTransform() const { return m_additionalTransform; }
		RED_FORCE_INLINE void SetAdditionalTransform(const Transform& additionalTransform) { m_additionalTransform = additionalTransform; }

		// read mouse rotation speed from config
		static Float GetMouseRotationScaleX();
		static Float GetMouseRotationScaleY();

		// get 3D point location that should be used for streaming calculations
		// Note: this is different from GetPosition - it's bypassing smooth camera transitions to avoid unnecessary resource loading
		Vector3 GetStreamingReferencePosition() const;

		// set last frame data aspect ratio, will be used in next lastFrameData update
		void SetLastFrameDataAspectRatio( Float aspectRatio );

		// change projection mode
		void SetProjectionMode( EProjectionMode projectionMode );

		// reset camera to be a free camera at given position and rotation
		void ResetToFreeCamera( const Vector3& position, const EulerAngles& rotation );
		void ResetToOrbitCamera( const Vector3& position, const EulerAngles& rotation, Float distance );

		// reset to show given box
		void FocusOn(bool snap);
		void LookAt(const Vector3& position, const Vector3& target);
		void SetFocalArea(const Box& focalArea);

		// fov management
		void SetCustomFov(Float customFov);
		void ResetToDefaultFov();

		// dof management
		void SetCustomDof( const RenderCameraDOFData& renderCameraDOF );
		void ResetToDefaultDof();

		// transform management
		void SetCustomTransform( const Transform& transform );
		void ResetToDefaultTransform();

		// user interaction
		void ProcessInput( input::IInputContext& input );
		void Integrate( Float dt );
		void InvalidateLastFrameData();

		// calculate rendering camera settings based on our position and rotation
		void PrepareCamera( Float aspectRatio, CRenderCamera& outCamera ) const;
		void PrepareCameraDOFData( RenderCameraDOFData& outRenderCameraDOF ) const;

        enum EMouseMode
		{
			LeftDown,
			RightDown,
			MiddleDown,
		};

        void MapPanButton(EMouseMode btn);
        void MapRotateButton(EMouseMode btn);
        void MapZoomButton(EMouseMode btn);
        void ResetDefaultMappings();
        void OrbitLockRotateDuringZoom(Bool lock);

		Event< Vector3, EulerAngles > CameraChanged;

	private:

		enum class EState : Uint8
		{
			Auto,
			User
		};

		struct InputState
		{
			Float m_keyboardSpeedModifier = 1.f; // continuous value
			Float m_mouseSpeedModifier = 1.f; // continuous value
			Float m_precisionModifier = 1.f; // continuous value
			Float m_left = 0.f;
			Float m_right = 0.f;
			Float m_forward = 0.f;
			Float m_backward = 0.f;
			Float m_upDown = 0.f; // mouse only
			Uint8 m_mouseMode = 0; // bitfield
			Float m_mouseXDelta = 0.f; // delta, requires reset
			Float m_mouseYDelta = 0.f; // delta, requires reset
			Float m_mouseZDelta = 0.f; // delta, requires reset

			Vector3 m_targetPoint = Vector3::ZEROS();
			Vector3 m_orbitPoint = Vector3::ZEROS();
			Vector3 m_initialPoint;

			EulerAngles m_initialRotation;
			EulerAngles m_targetRotation;

			Float m_focusTime = 0.f;
			Float m_focusDuration = 0.4f;
                  
			Float m_initialOrbit = 0.f;
			Float m_targetOrbit = 0.f;

			// for orthogonal projection
			Float m_initialZoom;
			Float m_targetZoom;

			// viewport normalized mouse coordinates
			Float m_mouseX;
			Float m_mouseY;

			Bool m_keyboardInUse = false;

            EMouseMode m_pan = MiddleDown;
            EMouseMode m_rotate = RightDown;
            EMouseMode m_zoom = LeftDown;
            Bool m_rotateLockedDuringZoom = false;

            std::function<void()> m_LookAtCompleteCallback = []{};
		};

		void IntegrateUser(Float dt);
		void IntegrateAuto(Float dt);
		void SetModeInternal(EMode mode);
		void NotifyManipulation();
		Bool ValidateCameraLocation( const String& location );

		// New format (Falls back to deprecated format)
		// [] denotes required parameter
		// <> denotes optional
		// Cam [Position] <Rotation> <EProjectionMode> <RenderingWindowCamera::EMode> <OrbitDistance> <OrbitZoom>
		Bool ValidateCameraLocation( const String& location, CameraLocation& camLocation ) const;

		// Old format
		// [] denotes required parameter
		// <> denotes optional
		// CamLoc [EProjectionMode] [RenderingWindowCamera::EMode] [Position] [Rotation] <OrbitDistance> <OrbitZoom>
		Bool ValidateCameraLocationDeprecated( const String& location, CameraLocation& camLocation ) const;

		void ResetConsoleVars();
        void ResetCameraParams();
        void AdjustMouseMovementSpeed(const Float data);

		Bool ProcessModeMouse( InputState& state, Uint16 key, EInputAction action );
		

		EProjectionMode	m_projectionMode;
		EMode			m_previousMode;
		EMode			m_mode;
		EState			m_state;

		Bool			m_hasCustomFov;		// for perspective only
		Float			m_customFov;
		Float			m_zoom;				// for orthogonal only

		RenderCameraDOFData m_renderCameraDOF;		// for perspective only

		Vector3			m_position;
		EulerAngles		m_rotation;
		EulerAngles		m_savedRotation;	// used when switching from ortho to perspective

		Vector3			m_cachedPosition;
		EulerAngles		m_cachedRotation;

		Float			m_orbitDistance;
		Float			m_maxOrbitDistance;

		Vector3			m_velocity;

		InputState		m_input;

		Box				m_focalArea;

		Float			m_lastFrameDataAspectRatio;

		// Transform that is applied just before sending final camera position and rotation to rendering
		Transform m_additionalTransform = Transform::IDENTITY();

		interop::InteropNotifier& m_cameraChanged;		// Whenever the camera changes, frame to frame
		interop::InteropNotifier m_cameraManipulated;	// Whenever the user finishes changing the camera, creating discrete points of history
		interop::InteropNotifier m_projectionModeChanged;

		Bool			m_isUnderUserManipulation;
		Bool			m_wasUnderUserManipulation;

		Bool			m_usingCustomTransform;			// Overriding user input controlled transform
		Vector3			m_defaultPosition;				// Latest position before override
		EulerAngles		m_defaultRotation;				// Latest rotation before override
	};

} // tools
