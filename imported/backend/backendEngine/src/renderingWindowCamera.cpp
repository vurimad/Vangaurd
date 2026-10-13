/**
* Copyright (c) 2013-2018 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"
#include "renderingWindowCamera.h"
#include "../../common/interop/include/interopUtilsRTTI.h"

#include "../../common/input/include/inputContext.h"

namespace Config
{
	TConfigVar< Float >	 cvKeyboardMovementSpeed( "Editor/RenderingWindow/Camera", "KeyboardMovementSpeed", 10.0f, 5.0f,  20.f,		eConsoleVarFlag_Save );
	TConfigVar< Float >	 cvKeyboardRotationSpeed( "Editor/RenderingWindow/Camera", "KeyboardRotationSpeed", 90.0f, 35.0f, 180.0f,	eConsoleVarFlag_Save );
	TConfigVar< Float >	 cvKeyboardAcceleration ( "Editor/RenderingWindow/Camera", "KeyboardAcceleration",  5.0f,  1.0f,  10.f,		eConsoleVarFlag_Save );
	TConfigVar< Float >	 cvKeyboardMovementMod  ( "Editor/RenderingWindow/Camera", "KeyboardMovementMod",   5.0f,  1.0f,  30.f,		eConsoleVarFlag_Save );

	TConfigVar< Float >	 cvMouseRotationScaleX  ( "Editor/RenderingWindow/Camera", "MouseRotationScaleX",   1.0f,  0.01f, 2.0f,		eConsoleVarFlag_Save );
	TConfigVar< Float >	 cvMouseRotationScaleY  ( "Editor/RenderingWindow/Camera", "MouseRotationScaleY",   1.0f,  0.01f, 2.0f,		eConsoleVarFlag_Save );
																																
	TConfigVar< Float >	 cvMouseMovementScaleX  ( "Editor/RenderingWindow/Camera", "MouseMovementScaleX",   0.05f, 0.01f, 0.1f,		eConsoleVarFlag_Save );
	TConfigVar< Float >	 cvMouseMovementScaleY  ( "Editor/RenderingWindow/Camera", "MouseMovementScaleY",   0.05f, 0.01f, 0.1f,		eConsoleVarFlag_Save );
	TConfigVar< Float >	 cvMouseMovementModStep ( "Editor/RenderingWindow/Camera", "MouseMovementModStep",  2.0f,  0.1f,  100.f,	eConsoleVarFlag_Save);
																																
	TConfigVar< Float >	 cvBaseMovementSpeed	( "Editor/RenderingWindow/Camera", "BaseMovementSpeed",	    1.0f,  1.0f,  10.f,		eConsoleVarFlag_Save );
	TConfigVar< Float >	 cvOrthoMovementSpeed	( "Editor/RenderingWindow/Camera", "OrthoMovementSpeed",	0.2f,  0.1f,  1.0f,		eConsoleVarFlag_Save );
	TConfigVar< Float >	 cvBaseLookSensitivity  ( "Editor/RenderingWindow/Camera", "BaseLookSensitivity",   1.0f,  0.01f, 2.0f,		eConsoleVarFlag_Save );
	TConfigVar< Float >	 cvMaxOrbitDistance		( "Editor/RenderingWindow/Camera", "MaxOrbitDistance",		30.f,  10.f,  1000.f,	eConsoleVarFlag_Save );

	TConfigVar< Float >	 cvNearDistance			( "Editor/RenderingWindow/Camera", "NearDistance",			0.02f,  0.001f,  0.5f,		eConsoleVarFlag_Save );
	TConfigVar< Float >	 cvFarDistance			( "Editor/RenderingWindow/Camera", "FarDistance",			16000.0f, 1000.0f, 32000.0f,	eConsoleVarFlag_Save );
	TConfigVar< Float >	 cvFOV					( "Editor/RenderingWindow/Camera", "FOV",					75.f,  5.f,  150.f,			eConsoleVarFlag_Save );

	TConfigVar< Bool  >	 cvNavigateWithLMB		( "Editor/RenderingWindow/Camera", "NavigateWithLeftMouseButton", true, eConsoleVarFlag_Save );
	TConfigVar< Bool  >	 cvLookInverted		    ( "Editor/RenderingWindow/Camera", "BaseLookInverted",	    false, eConsoleVarFlag_Save );
} // config

namespace tools
{
	//-----

	RenderingWindowCamera::RenderingWindowCamera( interop::InteropNotifier& cameraChanged )
		: m_projectionMode( PM_Perspective )
		, m_mode( EMode::Free )
		, m_state( EState::User )
		, m_position( 0, -10, 0 )
		, m_rotation( 0, 0, 0 )
		, m_cachedPosition( Vector3::ZEROS() )
		, m_cachedRotation( EulerAngles::ZEROS() )
		//, m_cachedFov( 0.f )
		, m_orbitDistance( 1.0f )
		, m_maxOrbitDistance( Config::cvMaxOrbitDistance.Get() )
		, m_velocity( 0,0,0 )
		, m_hasCustomFov( false )
		, m_customFov( Config::cvFOV.Get() )
		, m_zoom( 50.0f )
		, m_lastFrameDataAspectRatio( -1.f )
		, m_cameraChanged( cameraChanged )
		, m_usingCustomTransform( false )
		, m_defaultPosition( Vector3::ZEROS() )
		, m_defaultRotation( EulerAngles::ZEROS() )
	{
        m_renderCameraDOF = {0};
	}

	RenderingWindowCamera::~RenderingWindowCamera() = default;

	const Float RenderingWindowCamera::GetFOV() const
	{
		return m_hasCustomFov ? m_customFov : Config::cvFOV.Get();
	}

	Float RenderingWindowCamera::GetMouseRotationScaleX()
	{
		return Config::cvMouseRotationScaleX.Get() * Config::cvBaseLookSensitivity.Get();
	}

	Float RenderingWindowCamera::GetMouseRotationScaleY()
	{
		return Config::cvMouseRotationScaleY.Get() * Config::cvBaseLookSensitivity.Get();
	}

	void RenderingWindowCamera::FocusOn( bool snap )
	{
		if( snap )
		{
			Vector4 vecForward;
			m_rotation.ToAngleVectors( &vecForward, nullptr, nullptr );
			m_velocity = Vector3::ZEROS();

			if ( m_mode == EMode::Free )
			{
				if ( m_projectionMode != PM_Perspective )
				{
					m_position = m_focalArea.CalcCenter();
					m_zoom = m_focalArea.CalcSize().Mag3();
				}
				else
				{
					m_position = m_focalArea.CalcCenter() - vecForward * m_focalArea.CalcSize().Mag3();
				}
			}
			else if ( m_mode == EMode::Orbit )
			{
				m_position		   = m_focalArea.CalcCenter();
				m_orbitDistance	   = m_focalArea.CalcSize().Mag3() * 1.2f;
				m_maxOrbitDistance = Max(m_orbitDistance * 2.f, Config::cvMaxOrbitDistance.Get());
			}
		}
		else if(m_mode != EMode::MoveTo && m_mode != EMode::LookAt)
		{
			m_previousMode = m_mode;
			m_mode  = EMode::MoveTo;
			m_state = EState::Auto;

			if( m_previousMode == EMode::Free )
			{
				Vector4 vecForward;
				m_rotation.ToAngleVectors( &vecForward, nullptr, nullptr );

				if ( m_projectionMode == PM_Perspective )
				{
					m_input.m_targetPoint = m_focalArea.CalcCenter() - vecForward * m_focalArea.CalcSize().Mag3();
					m_input.m_initialPoint = m_position;
				}
				else
				{
					m_input.m_targetPoint = m_focalArea.CalcCenter();
					m_input.m_initialPoint = m_position;
					m_input.m_targetZoom = m_focalArea.CalcSize().Mag3();
					m_input.m_initialZoom = m_zoom;
				}
			}
			else
			{
				m_input.m_initialOrbit = m_orbitDistance;
				m_input.m_targetOrbit  = m_focalArea.CalcSize().Mag3() * 1.2f;
			}
		}

		m_input.m_orbitPoint = m_focalArea.CalcCenter();
	}

	void RenderingWindowCamera::LookAt( const Vector3& position, const Vector3& target )
	{
		Matrix m; 
		m.BuildFromDirectionVector((target - position).Normalized());

		m_previousMode			  = m_mode;
		m_mode					  = EMode::LookAt;
		m_state					  = EState::Auto;
		m_input.m_initialRotation = m_rotation;
		m_input.m_targetRotation  = m.ToEulerAnglesFull();
	}

	void RenderingWindowCamera::SetFocalArea(const Box& focalArea)
	{
        const auto distance = focalArea.CalcSize().Mag3() * 2.2f;

		m_focalArea		   = focalArea;
		m_maxOrbitDistance = Clamp(distance, distance, Config::cvMaxOrbitDistance.Get());

		if( m_mode == EMode::Orbit )
		{
			///
			///	If we have changed the focal area and we are in orbit mode we need to look at the new focal area.
			///	Calling SetModeInternal to free camera so it resets the position to the correct place then set it 
			///	back to orbit so it does the look at animation again
			///
			SetModeInternal(EMode::Free);
			SetModeInternal(EMode::Orbit);
		}
	}

	Vector3 RenderingWindowCamera::GetStreamingReferencePosition() const
	{
		if ( m_mode == EMode::MoveTo )
		{
			if ( m_projectionMode == PM_Perspective )
			{
				if ( m_previousMode == EMode::Free )
				{
					return m_input.m_targetPoint;
				}
			}
		}

		// TODO handle orbit camera

		return m_position;
	}

	void RenderingWindowCamera::SetCustomFov(Float customFov)
	{
		m_hasCustomFov = true;
		m_customFov = customFov;
	}

	void RenderingWindowCamera::ResetToDefaultFov()
	{
		m_hasCustomFov = false;
	}

	void RenderingWindowCamera::SetCustomDof( const RenderCameraDOFData& renderCameraDOF )
	{
		m_renderCameraDOF = renderCameraDOF;
	}

	void RenderingWindowCamera::ResetToDefaultDof()
	{
        m_renderCameraDOF = {0};
	}

	void RenderingWindowCamera::SetCustomTransform( const Transform& transform )
	{
		if ( !m_usingCustomTransform )
		{
			m_defaultPosition = m_position;
			m_defaultRotation = m_rotation;
			m_usingCustomTransform = true;
		}

		ResetToFreeCamera( transform.GetPosition3(), transform.GetOrientation().ToEulerAngles() );
	}

	void RenderingWindowCamera::ResetToDefaultTransform()
	{
		if ( m_usingCustomTransform )
		{
			ResetToFreeCamera( m_defaultPosition, m_defaultRotation );
			m_usingCustomTransform = false;
		}
	}

	void RenderingWindowCamera::ResetToFreeCamera( const Vector3& position, const EulerAngles& rotation )
	{
		m_mode	   = EMode::Free;
		m_state	   = EState::User;
		m_position = position;
		m_rotation = rotation;
		m_velocity = Vector3::ZEROS();

		ResetCameraParams();
	}

	void RenderingWindowCamera::ResetToOrbitCamera( const Vector3& position, const EulerAngles& rotation, Float distance )
	{
		m_mode			= EMode::Orbit;
		m_state			= EState::User;
		m_position		= position;
		m_rotation		= rotation;
		m_orbitDistance = distance;
		m_velocity		= Vector3::ZEROS();

		ResetCameraParams();
	}

	void RenderingWindowCamera::ProcessInput( input::IInputContext& input )
	{
		m_input.m_keyboardInUse = false;

		for ( auto& it : input.GetBufferedInputData() )
		{
            if( it.m_type == input::BufferedInputEvent::Type::DragDrop )
                continue;

            if(it.m_raw.m_key == IK_LShift)
            {
                m_input.m_keyboardSpeedModifier = (it.m_raw.m_action == IACT_Press) ? Config::cvKeyboardMovementMod.Get() : 1.f;
            }
            else if(it.m_raw.m_key == IK_LControl)
            {
                m_input.m_precisionModifier = (it.m_raw.m_action == IACT_Press) ? 0.1f : 1.0f; 
            }
            else if(it.m_raw.m_key == IK_W)
            {
                m_input.m_forward = (it.m_raw.m_action == IACT_Press) ? 1.0f : 0.f;
                m_input.m_keyboardInUse = true;
            }
            else if(it.m_raw.m_key == IK_S)
            {
                m_input.m_backward = (it.m_raw.m_action == IACT_Press) ? -1.0f : 0.f;
                m_input.m_keyboardInUse = true;
            }
            else if(it.m_raw.m_key == IK_A)
            {
                m_input.m_left = (it.m_raw.m_action == IACT_Press) ? -1.0f : 0.f;
                m_input.m_keyboardInUse = true;
            }
            else if(it.m_raw.m_key == IK_D)
            {
                m_input.m_right = (it.m_raw.m_action == IACT_Press) ? 1.0f : 0.f;
                m_input.m_keyboardInUse = true;
            }
            else if(it.m_raw.m_key == IK_E)
            {
                m_input.m_upDown = (it.m_raw.m_action == IACT_Press) ? -1.0f : 0.f;
                m_input.m_keyboardInUse = true;
            }
            else if(it.m_raw.m_key == IK_Q)
            {
                m_input.m_upDown = (it.m_raw.m_action == IACT_Press) ? 1.0f : 0.f;
                m_input.m_keyboardInUse = true;
            }

			else if ( !ProcessModeMouse( m_input, it.m_raw.m_key, it.m_raw.m_action ) )
			{
                if(it.m_raw.m_key == IK_MouseX && it.m_raw.m_action == IACT_Axis)
                {
                    m_input.m_mouseXDelta += it.m_raw.m_data * Config::cvBaseLookSensitivity.Get();
                }
                else if(it.m_raw.m_key == IK_MouseY && it.m_raw.m_action == IACT_Axis)
                {
                    m_input.m_mouseYDelta += it.m_raw.m_data * Config::cvBaseLookSensitivity.Get();
                }
                else if(it.m_raw.m_key == IK_MouseZ && it.m_raw.m_action == IACT_Axis)
                {
                    if(m_mode != EMode::Orbit && m_input.m_mouseMode == RED_FLAG(RightDown))
                    {
                        AdjustMouseMovementSpeed(it.m_raw.m_data);
                    }
                    else
                    {
                        m_input.m_mouseZDelta += it.m_raw.m_data * Config::cvBaseLookSensitivity.Get();
                    }
                }
			}
		}
	}

	namespace Helper
	{
		Vector3 ClampLength( const Vector3& v, const Float maxLength )
		{
			if ( maxLength <= 0.00001f )
				return Vector3::ZEROS();

			const Float length = v.Mag();
			if ( length >= maxLength )
			{
				const Float scale = maxLength / length;
				RED_ASSERT( scale <= 1.0f );
				return v * scale;
			}
			else
			{
				return v;
			}
		}
	}

	void RenderingWindowCamera::InvalidateLastFrameData()
	{
		// #ToDo: Send render command
	}

	void RenderingWindowCamera::SetLastFrameDataAspectRatio( Float aspectRatio )
	{
		RED_FATAL_ASSERT( aspectRatio > 0, "Invalid aspect ratio" );
		m_lastFrameDataAspectRatio = aspectRatio;
	}

	void RenderingWindowCamera::SetProjectionMode( EProjectionMode projectionMode )
	{
		if ( m_projectionMode == PM_Perspective || m_projectionMode == PM_Ortho )
		{
			m_savedRotation = m_rotation;
		}

		m_projectionMode = projectionMode;

		switch ( projectionMode )
		{
		case PM_OrthoMinusX:
			m_rotation = EulerAngles( 0.0f, 0.0f, 90.0f );
			break;
		case PM_OrthoPlusX:
			m_rotation = EulerAngles( 0.0f, 0.0f, 270.0f );
			break;
		case PM_OrthoMinusY:
			m_rotation = EulerAngles( 0.0f, 0.0f, 180.0f );
			break;
		case PM_OrthoPlusY:
			m_rotation = EulerAngles( 0.0f, 0.0f, 0.0f );
			break;
		case PM_OrthoMinusZ:
			m_rotation = EulerAngles( 0.0f, -90.0f, 0.0f );
			break;
		case PM_OrthoPlusZ:
			m_rotation = EulerAngles( 0.0f, 90.0f, 0.0f );
			break;
		case PM_Perspective:
		case PM_Ortho:
			m_rotation = m_savedRotation;
			break;
		default:
			break;
		}
	}

	void RenderingWindowCamera::Integrate( Float dt )
	{
		if( m_state == EState::User )
		{
			IntegrateUser(dt);
		}
		else if( m_state == EState::Auto )
		{
			IntegrateAuto(dt);
		}

		if ( !( m_position == m_cachedPosition && m_rotation == m_cachedRotation ) )
		{
			m_cachedPosition = m_position;
			m_cachedRotation = m_rotation;
			m_cameraChanged.Notify();

			CameraChanged( m_position, m_rotation );
		}
	}

	void RenderingWindowCamera::IntegrateUser( Float dt )
	{
		// get movement deltas for this integration step (may be modified)
		Float fowardBackward = 0;
		Float leftRight = 0;
		Float upDown = 0;

		const Float speed = ( Config::cvBaseMovementSpeed.Get() * m_input.m_keyboardSpeedModifier * m_input.m_precisionModifier * m_input.m_mouseSpeedModifier );
		// transform input to camera movement
		if ( !IsOrthogonal() )
		{
			if ( m_mode == EMode::Free )
			{
				if ( ( m_input.m_mouseMode == RED_FLAG( LeftDown ) ) && Config::cvNavigateWithLMB.Get() ) // L: mouseX: yaw, mouseY: forward/backward
				{
					m_rotation.Yaw -= m_input.m_mouseXDelta * Config::cvMouseRotationScaleX.Get(); // factional movement when zooming, allows user to feel less locked
					fowardBackward -= m_input.m_mouseYDelta * Config::cvMouseMovementScaleY.Get();
				}
				else if ( m_input.m_mouseMode == RED_FLAG( RightDown ) ) // R: mouseX: yaw, mouseY: pitch
				{
					const Float invert = Config::cvLookInverted.Get() ? -1.f : 1.f;
					m_rotation.Yaw -= m_input.m_mouseXDelta * Config::cvMouseRotationScaleX.Get();

					// If the camera rotation sensitivity was high enough, it would be possible to overcome the clamping code below 
					// and make a full spin during one frame. Clamping the pitch rotation delta to values lower than 90 degrees fixes it, 
					// without affecting normal functionality.
					m_rotation.Pitch -= Clamp( m_input.m_mouseYDelta * Config::cvMouseRotationScaleY.Get() * invert, -60.f, 60.f );

					// clamping the pitch rotation to prevent upside-down camera rotation in free mode
					if ( m_rotation.Pitch < 180.f )
					{
						m_rotation.Pitch = Clamp( m_rotation.Pitch, -90.f, 90.f );
					}
					else
					{
						m_rotation.Pitch = Clamp( m_rotation.Pitch, 270.f, 360.f + 90.f );
					}
				}
				else if ( m_input.m_mouseMode == ( RED_FLAG( LeftDown ) | RED_FLAG( RightDown ) ) || m_input.m_mouseMode == RED_FLAG( MiddleDown ) )
				{
					// LR: mouseX: pan left/right, mouseY: up/down
					leftRight += m_input.m_mouseXDelta * Config::cvMouseMovementScaleX.Get();
					upDown -= m_input.m_mouseYDelta * Config::cvMouseMovementScaleY.Get();
				}
			}

			// ORBIT
			else if ( m_mode == EMode::Orbit )
			{
				// look around
				if ( m_input.m_mouseMode == RED_FLAG( m_input.m_rotate ) ) // mouseX: yaw, mouseY: pitch
				{
					const Float invert = Config::cvLookInverted.Get() ? -1.f : 1.f;
					m_rotation.Yaw -= m_input.m_mouseXDelta * Config::cvMouseRotationScaleX.Get();
					m_rotation.Pitch -= m_input.m_mouseYDelta * Config::cvMouseRotationScaleY.Get() * invert;
				}
				else if ( m_input.m_mouseMode == RED_FLAG( m_input.m_zoom ) ) // mouseY: distance
				{
                    if( !m_input.m_rotateLockedDuringZoom )
                    {
					    m_rotation.Yaw -= m_input.m_mouseXDelta * Config::cvMouseRotationScaleX.Get() * speed; // fractional movement when zooming, allows user to feel less locked
                    }

					// zoom in/out using the mouse movement
					m_orbitDistance += m_input.m_mouseYDelta * Config::cvMouseMovementScaleY.Get() * speed;
				}
				else if ( m_input.m_mouseMode == ( RED_FLAG( LeftDown ) | RED_FLAG( RightDown ) ) || m_input.m_mouseMode == RED_FLAG( m_input.m_pan ) )
				{
					// Horizontal and Vertical movement on LMB+RMB or MMB
					leftRight += m_input.m_mouseXDelta * Config::cvMouseMovementScaleX.Get();
					upDown -= m_input.m_mouseYDelta * Config::cvMouseMovementScaleY.Get();
				}

				// zoom in/out using mouse wheel
				m_orbitDistance = m_orbitDistance * ( 1.0f - m_input.m_mouseZDelta * 0.02f );
			}
		}

		// decompose rotation 
		Vector4 vecForward, vecRight, vecUp, vecForward2d, vecRight2d, vecUp2d;
		EulerAngles(0, 0, m_rotation.Yaw).ToAngleVectors(&vecForward2d, &vecRight2d, &vecUp2d);
		m_rotation.ToAngleVectors( &vecForward, &vecRight, &vecUp );

		if ( IsOrthogonal() )
		{
			if ( m_input.m_mouseMode == RED_FLAG( MiddleDown ) ) // Middle: movement
			{
				m_position -= vecRight * ( m_input.m_mouseXDelta * m_zoom / 500.0f );
				m_position += vecUp * ( m_input.m_mouseYDelta * m_zoom / 500.0f );
			}
			else if ( m_input.m_mouseMode == ( RED_FLAG( LeftDown ) | RED_FLAG( RightDown ) ) ) // Right+Left: rotation
			{
				const Float invert = Config::cvLookInverted.Get() ? -1.f : 1.f;
				m_rotation.Yaw -= m_input.m_mouseXDelta * Config::cvMouseRotationScaleX.Get();
				m_rotation.Pitch -= m_input.m_mouseYDelta * Config::cvMouseRotationScaleY.Get() * invert;
			}
			else if ( m_input.m_mouseMode == RED_FLAG( RightDown ) ) // Right: zoom
			{
				const Float zoomFactor = 1.02f;
				const Float minZoom = 0.01f;
				const Float maxZoom = 20000.0f;

				m_zoom *= powf( zoomFactor, m_input.m_mouseYDelta );
                m_zoom = Clamp(m_zoom, minZoom, maxZoom);
			}

			// TODO: adjust camera position so the cursor position remains in the same world-space location
		}
		else
		{
			const auto forward = (m_input.m_mouseMode == RED_FLAG(LeftDown)) ? vecForward2d : vecForward;
			const auto up      = (m_input.m_mouseMode == RED_FLAG(LeftDown)) ? vecUp2d      : vecUp;
			const auto right   = (m_input.m_mouseMode == RED_FLAG(LeftDown)) ? vecRight2d   : vecRight;

			// panning
			m_position += forward * fowardBackward * speed;
			m_position += right * leftRight * speed;
			m_position += up * upDown * speed;

			// WSAD keyboard movement
			if ( m_mode == EMode::Free )
			{
				// build keyboard movement vector
				Vector4 keyboardDir = Vector4::ZEROS();
				
				keyboardDir += forward * (m_input.m_forward + m_input.m_backward);
				keyboardDir += right * (m_input.m_left + m_input.m_right);
				keyboardDir += up * m_input.m_upDown;

				keyboardDir.Normalize3();
				
				// adjust movement velocity
				const Float accelerationFactor = Min< Float >( 1.0f, dt * Config::cvKeyboardAcceleration.Get() );
				const Vector4 velocityDiff = Helper::ClampLength( keyboardDir - m_velocity, accelerationFactor );
				m_velocity += velocityDiff;

				// move, additionally to the mouse movement
				m_position += m_velocity * ( speed * dt * Config::cvKeyboardMovementSpeed.Get() );
			}
			else
			{
				// additional WSAD rotation
				m_rotation.Yaw += (m_input.m_left + m_input.m_right) * dt * speed * Config::cvKeyboardRotationSpeed.Get();
				m_rotation.Pitch += (m_input.m_forward + m_input.m_backward) * dt * speed * Config::cvKeyboardRotationSpeed.Get();

				// distance
				m_orbitDistance -= m_input.m_upDown * speed * dt * Config::cvKeyboardMovementSpeed.Get();
			}

			// keep the distance in range
			m_orbitDistance = Clamp( m_orbitDistance, 0.1f, m_maxOrbitDistance );

			// renormalize rotation (to keep the angles within 360 deg)
			m_rotation.Normalize();
		}

		m_isUnderUserManipulation = m_input.m_mouseMode || m_input.m_keyboardInUse;

		if( !m_wasUnderUserManipulation && m_isUnderUserManipulation )
		{
			m_wasUnderUserManipulation = true;
		}
		else if( m_wasUnderUserManipulation && !m_isUnderUserManipulation && m_velocity.SquareMag() == 0 )
		{
			NotifyManipulation();

			m_wasUnderUserManipulation = false;
		}

		m_input.m_mouseXDelta = 0.0f;
		m_input.m_mouseYDelta = 0.0f;
		m_input.m_mouseZDelta = 0.0f;
	}

	void RenderingWindowCamera::IntegrateAuto(Float dt)
	{
		const Float t = Min<Float>( (m_input.m_focusTime / m_input.m_focusDuration), 1.f);

		if( m_mode == EMode::MoveTo )
		{
			if ( m_projectionMode == PM_Perspective )
			{
				if ( m_previousMode == EMode::Free )
				{
					m_position = math::Lerp( t, m_input.m_initialPoint, m_input.m_targetPoint );

					if ( m_input.m_focusTime >= m_input.m_focusDuration )
					{
						ResetToFreeCamera( m_input.m_targetPoint, m_rotation );
					}
				}
				else if ( m_previousMode == EMode::Orbit )
				{
					m_orbitDistance = math::Lerp( t, m_input.m_initialOrbit, m_input.m_targetOrbit );

					if ( m_input.m_focusTime >= m_input.m_focusDuration )
					{
						ResetToOrbitCamera( m_position, m_rotation, m_input.m_targetOrbit );
					}
				}
			}
			else
			{
				m_position = math::Lerp( t, m_input.m_initialPoint, m_input.m_targetPoint );
				m_zoom = 1.0f / math::Lerp( t, 1.0f / m_input.m_initialZoom, 1.0f / m_input.m_targetZoom );

				if ( m_input.m_focusTime >= m_input.m_focusDuration )
				{
					m_zoom = m_input.m_targetZoom;
					ResetToFreeCamera( m_input.m_targetPoint, m_rotation );
				}
			}
		}
		else if( m_mode == EMode::LookAt )
		{
			m_rotation = EulerAngles::Interpolate(m_input.m_initialRotation, m_input.m_targetRotation, t);

			if( m_input.m_focusTime >= m_input.m_focusDuration )
			{
				m_rotation = m_input.m_targetRotation;
				m_input.m_LookAtCompleteCallback();
			}
		}

		m_input.m_focusTime += dt;
	}

	void RenderingWindowCamera::SetModeInternal(EMode mode)
	{
		if( m_state == EState::User )
		{
			switch( mode )
			{
				case EMode::Free:
				{
					Vector4 forward;
					GetRotation().ToAngleVectors(&forward, nullptr, nullptr);
					ResetToFreeCamera( m_position - forward * m_orbitDistance, m_rotation );
					break;
				}

				case EMode::Orbit:
				{
					m_input.m_orbitPoint = m_focalArea.IsEmpty() ? Vector4::ZERO_3D_POINT() : m_focalArea.CalcCenter();
					LookAt(m_position, m_input.m_orbitPoint);
					m_input.m_LookAtCompleteCallback = [this]()
					{
						const Float orbitDistance = (m_position - m_input.m_orbitPoint).Mag();
						ResetToOrbitCamera(m_input.m_orbitPoint, m_rotation, orbitDistance);

						if( orbitDistance > m_maxOrbitDistance )
						{
							m_previousMode		   = m_mode;
							m_mode				   = EMode::MoveTo;
							m_state				   = EState::Auto;
							m_input.m_initialOrbit = m_orbitDistance;
							m_input.m_targetOrbit  = m_maxOrbitDistance;
						}
					};
					break;
				}
			}
		}
	}

	void RenderingWindowCamera::ResetConsoleVars()
	{
		Config::cvKeyboardMovementSpeed.Reset();
		Config::cvKeyboardRotationSpeed.Reset(); 
		Config::cvKeyboardAcceleration.Reset();

		Config::cvMouseRotationScaleX.Reset();
		Config::cvMouseRotationScaleY.Reset();

		Config::cvMouseMovementScaleX.Reset();
		Config::cvMouseMovementScaleY.Reset();

		Config::cvBaseMovementSpeed.Reset();
		Config::cvBaseLookSensitivity.Reset();
		Config::cvLookInverted.Reset();
		Config::cvMaxOrbitDistance.Reset();

		Config::cvFOV.Reset();
		Config::cvNearDistance.Reset();
		Config::cvFarDistance.Reset();
	}

    void RenderingWindowCamera::ResetCameraParams()
    {
        m_input.m_keyboardSpeedModifier	= 1.0f;
		m_input.m_mouseSpeedModifier	= 1.0f;
		m_input.m_precisionModifier		= 1.0f;
		m_input.m_left				    = 0.0f;
		m_input.m_right                 = 0.0f;
		m_input.m_forward               = 0.0f;
		m_input.m_backward		        = 0.0f;
		m_input.m_mouseXDelta			= 0.0f;
		m_input.m_mouseYDelta			= 0.0f;
		m_input.m_mouseZDelta			= 0.0f;
		m_input.m_upDown				= 0.0f;
		m_input.m_mouseMode				= 0;

		m_input.m_focusTime				= 0.0f;
		m_input.m_focusDuration			= 0.4f;
		m_input.m_targetPoint			= Vector3::ZEROS();
		m_input.m_orbitPoint			= Vector3::ZEROS();

		m_input.m_initialOrbit			= 0.0f;
		m_input.m_targetOrbit			= 0.0f;

		m_input.m_LookAtCompleteCallback = [](){};
    }

    void RenderingWindowCamera::AdjustMouseMovementSpeed(const Float data)
    {
        const Float step = Config::cvMouseMovementModStep.Get();
        const Float clampedData = Clamp(data, -1.0f, 1.0f);
        Float change = step * clampedData;

        // If the modifier is below 1.0 (user wants the camera to move slowly) divide instead
        if(m_input.m_mouseSpeedModifier < 1.0f || m_input.m_mouseSpeedModifier + change < 1.0f)
        {
            change = (change < 0) ? 1 / change : change;
            m_input.m_mouseSpeedModifier *= Abs(change);
        }
        // Otherwise, just linearly adjust 
        else
        {
            m_input.m_mouseSpeedModifier += change;
        }
    }

	Bool RenderingWindowCamera::ProcessModeMouse( InputState& state, Uint16 key, EInputAction action )
	{
		// mouse input state
		if ( key == IK_LeftMouse )
		{
			if ( action == IACT_Press )
			{
				state.m_mouseMode |= RED_FLAG( LeftDown );
			}
			else if ( action == IACT_Release )
			{
				state.m_mouseMode &= ~RED_FLAG( LeftDown );
			}

			return true;
		}
		else if ( key == IK_RightMouse )
		{
			if ( action == IACT_Press )
			{
				state.m_mouseMode |= RED_FLAG( RightDown );
			}
			else if ( action == IACT_Release )
			{
				state.m_mouseMode &= ~RED_FLAG( RightDown );
			}

			return true;
		}
		else if ( key == IK_MiddleMouse )
		{
			if ( action == IACT_Press )
			{
				state.m_mouseMode |= RED_FLAG( MiddleDown );
			}
			else if ( action == IACT_Release )
			{
				state.m_mouseMode &= ~RED_FLAG( MiddleDown );
			}

			return true;
		}

		return false;
	}

	void RenderingWindowCamera::PrepareCamera( Float aspectRatio, CRenderCamera& outCamera ) const
	{
		WorldPosition cameraPosition( m_position );

		CRenderCamera::CreationContext context;

		if ( IsOrthogonal() )
		{
			context.position = cameraPosition;
			context.fov = 0.0f;
			context.nearPlane = -1000.0f;
			context.farPlane = 1000.0f;
			context.zoom = m_zoom;
			context.isReversedProjection = false;
		}
		else
		{
			const Bool computeOrbit = m_mode == EMode::Orbit || ( m_mode == EMode::MoveTo && m_previousMode == EMode::Orbit );
			if ( computeOrbit )
			{
				Vector4 forward;
				m_rotation.ToAngleVectors( &forward, nullptr, nullptr );
				cameraPosition -= forward * m_orbitDistance;
			}

			context.position = cameraPosition;
			context.fov = GetFOV();
			context.nearPlane = Config::cvNearDistance.Get();
			context.farPlane = Config::cvFarDistance.Get();
			context.zoom = 1.0f;
			context.isReversedProjection = true;

			// Clamp to some sane values
			{
				context.nearPlane = Clamp<Float>( context.nearPlane, 0.001f, 0.5f );		// 1mm ~ 50cm
				context.farPlane  = Clamp<Float>( context.farPlane, 100.0f, 16000.0f );	// 100m ~ 16000m
			}
		}


		context.rotation = m_rotation.ToQuat();
		context.aspect = aspectRatio;

		const Transform currentCameraTransform(context.position.AsVector3(), context.rotation);
		const Transform resultCameraTransform = currentCameraTransform * m_additionalTransform;
		context.position = WorldPosition( resultCameraTransform.GetPosition() );
		context.rotation = resultCameraTransform.GetOrientation();

		outCamera.Set( context );
	}

    void RenderingWindowCamera::MapPanButton(EMouseMode btn)
    {
        m_input.m_pan = static_cast<EMouseMode>(btn);
    }

    void RenderingWindowCamera::MapRotateButton(EMouseMode btn)
    {
        m_input.m_rotate = static_cast<EMouseMode>(btn);
    }

    void RenderingWindowCamera::MapZoomButton(EMouseMode btn)
    {
        m_input.m_zoom = static_cast<EMouseMode>(btn);
    }

    void RenderingWindowCamera::ResetDefaultMappings()
    {
        m_input.m_pan = EMouseMode::MiddleDown;
        m_input.m_rotate = EMouseMode::RightDown;
        m_input.m_zoom = EMouseMode::LeftDown;
    }

    void RenderingWindowCamera::OrbitLockRotateDuringZoom(Bool lock)
    {
        m_input.m_rotateLockedDuringZoom = lock;
    }

	void RenderingWindowCamera::PrepareCameraDOFData( RenderCameraDOFData& outRenderCameraDOF ) const
	{
		if ( IsOrthogonal() )
		{
            outRenderCameraDOF = {0};
		}
		else
		{
			outRenderCameraDOF = m_renderCameraDOF;
		}
	}

	void RenderingWindowCamera::SetupInterop( RenderingWindowCamera* camera, ::interop::utils::Object& op )
	{
		op.RegisterNotifier( "_onCameraManipulated", camera->m_cameraManipulated );

		op.RegisterValue< CameraLocation >( "cameraLocation",
			/*GET*/ [ camera ]( interop::IContext& ctx, CameraLocation& outValue ) { outValue = camera->GetCameraLocation(); },
			/*SET*/ [ camera ]( interop::IContext& ctx, const CameraLocation& newValue ) { camera->SetCameraLocation( newValue ); } );

		op.RegisterFunctor<Bool, String>( "_validateCameraLocation",
			/*CALL*/[ camera ]( interop::IContext& ctx, const String& cameraLoc ) { return camera->ValidateCameraLocation( cameraLoc ); } );

		op.RegisterFunctor<Bool>( "reset",
			/*CALL*/[ camera ]( interop::IContext& ctx ) { camera->ResetConsoleVars(); return true; } );

		op.RegisterRTTIEnumValue<EMode>( "cameraMode",
			/*GET*/ [ camera ]( interop::IContext& ctx, EMode& outValue ) { outValue = camera->m_mode; },
			/*SET*/ [ camera ]( interop::IContext& ctx, const EMode& newValue ) { camera->SetModeInternal( newValue ); } );

		op.RegisterRTTIEnumValue<EProjectionMode>( "projectionMode", 
			/*GET*/ [camera](interop::IContext& ctx, EProjectionMode& outValue) { outValue = camera->m_projectionMode; },
			/*SET*/ [camera](interop::IContext& ctx, const EProjectionMode& newValue ) { camera->SetProjectionMode( newValue ); },
			&camera->m_projectionModeChanged );
		
		op.RegisterObject( "cameraPosition", [camera](interop::IContext& ctx) { return interop::utils::CreateRTTIAccessFunc(ctx, &camera->m_position ); } );
		op.RegisterObject( "cameraRotation", [camera](interop::IContext& ctx) { return interop::utils::CreateRTTIAccessFunc(ctx, &camera->m_rotation ); } );

		op.RegisterValue<Transform>( "cameraTransform", [camera](interop::IContext& ctx, Transform &outValue) 
														{ 
															const Transform currentCameraTransform( camera->m_position, camera->m_rotation.ToQuat() );
															outValue = currentCameraTransform * camera->m_additionalTransform;
														} );

		op.RegisterValue<Float>( "cameraFOV", [camera](interop::IContext& ctx, Float &outValue ) { outValue = camera->GetFOV(); } ) ;
		op.RegisterValue<Float>( "cameraZoom", [camera](interop::IContext& ctx, Float &outValue ) { outValue = camera->GetZoom(); } );

		op.RegisterValueRO< String >( "cameraInfo", [camera]() { return String::Printf( "[%.2f, %.2f, %.2f], pitch: %.2f, yaw: %.2f",
			camera->GetPosition().X, camera->GetPosition().Y, camera->GetPosition().Z, camera->GetRotation().Pitch, camera->GetRotation().Yaw ); } );

		op.RegisterFunctor< void, Float >( "setFOV", [ camera ]( interop::IContext& ctx, Float fov ) { camera->SetCustomFov( fov ); } );
		op.RegisterFunctor< void >( "resetFOV", [ camera ]( interop::IContext& ctx ) { camera->ResetToDefaultFov(); } );
	}

	// mind that changes here must be compatible with code in freeCameraComponent.cpp
	CameraLocation RenderingWindowCamera::GetCameraLocation() const
	{
		return CameraLocation{ m_position, m_rotation, m_projectionMode, m_mode, m_orbitDistance, m_zoom };
	}

	void RenderingWindowCamera::NotifyManipulation()
	{
		m_cameraManipulated.Notify( GetCameraLocation() );
	}

#define READ_REQUIRED_PARAM( parameter, parts, output )  if( parts.Size() <= parameter || !text::LoadFromTextAndConvertSafe( output, parts[ parameter ] ) ) return false
#define READ_OPTIONAL_PARAM( parameter, parts, output )  if( parts.Size() > parameter && !text::LoadFromTextAndConvertSafe( output, parts[ parameter ] ) ) return false

	Bool RenderingWindowCamera::ValidateCameraLocationDeprecated( const String& location, CameraLocation& camLocation ) const
	{
		RED_FATAL_ASSERT( location.BeginsWith( "CamLoc" ) );

		enum OldCameraFormatParameterOrder
		{
			Prefix,
			ProjectionMode,
			CameraMode,
			Translation,
			Rotation,
			OrbitDistance,
			OrbitZoom
		};

		red::DynArray< String > parts = location.Split( " " );

		READ_REQUIRED_PARAM( ProjectionMode, parts, camLocation.m_projectionMode );
		READ_REQUIRED_PARAM( CameraMode, parts, camLocation.m_mode );
		READ_REQUIRED_PARAM( Translation, parts, camLocation.m_position );
		READ_REQUIRED_PARAM( Rotation, parts, camLocation.m_rotation );

		if ( camLocation.m_mode == EMode::Orbit )
		{
			READ_OPTIONAL_PARAM( OrbitDistance, parts, camLocation.m_orbitDistance );
			READ_OPTIONAL_PARAM( OrbitZoom, parts, camLocation.m_zoom );
		}

		return true;
	}

	Bool RenderingWindowCamera::ValidateCameraLocation( const String& location, CameraLocation& camLocation ) const
	{
		enum NewCameraFormatParameterOrder
		{
			Prefix,
			Translation,
			Rotation,
			ProjectionMode,
			CameraMode,
			OrbitDistance,
			OrbitZoom
		};

		// first try it the old way - for strings taht starts with 'Cam' or 'CamLoc'
		if( location.BeginsWith( "CamLoc" ) )
			return ValidateCameraLocationDeprecated( location, camLocation );

		if ( location.BeginsWith( "Cam" ) )
		{
			red::DynArray< String > parts = location.Split( " " );

			if ( parts.Size() == 0 || !parts[0].EqualsNC( "Cam" ) )
				return false;

			READ_REQUIRED_PARAM( Translation, parts, camLocation.m_position );

			camLocation.m_rotation = EulerAngles::ZEROS();
			READ_OPTIONAL_PARAM( Rotation, parts, camLocation.m_rotation );

			camLocation.m_projectionMode = EProjectionMode::PM_Perspective;
			READ_OPTIONAL_PARAM( ProjectionMode, parts, camLocation.m_projectionMode );

			camLocation.m_mode = EMode::Free;
			READ_OPTIONAL_PARAM( CameraMode, parts, camLocation.m_mode );

			if ( camLocation.m_mode == EMode::Orbit )
			{
				READ_OPTIONAL_PARAM( OrbitDistance, parts, camLocation.m_orbitDistance );
				READ_OPTIONAL_PARAM( OrbitZoom, parts, camLocation.m_zoom );
			}

			return true;
		}
		
		camLocation.m_position = Vector3::PLUS_INF();
		if ( text::LoadFromText( location, &camLocation, GetTypeObject< CameraLocation >() ) && camLocation.m_position != Vector3::PLUS_INF() )
		{
			return true;
		}
		return false;
	}

	Bool RenderingWindowCamera::ValidateCameraLocation( const String& location )
	{
		CameraLocation camLocation;
		camLocation.m_orbitDistance = m_orbitDistance;
		camLocation.m_zoom = m_zoom;

		return ValidateCameraLocation( location, camLocation );
	}

	void RenderingWindowCamera::SetCameraLocation( const CameraLocation& cameraLocation )
	{
		m_mode = cameraLocation.m_mode;
		m_position = cameraLocation.m_position;
		m_rotation = cameraLocation.m_rotation;
		SetProjectionMode( cameraLocation.m_projectionMode );

		if ( cameraLocation.m_mode == EMode::Orbit )
		{
			m_orbitDistance = cameraLocation.m_orbitDistance;
			m_zoom = cameraLocation.m_zoom;
		}
			
		m_projectionModeChanged.Notify();
	}
}
