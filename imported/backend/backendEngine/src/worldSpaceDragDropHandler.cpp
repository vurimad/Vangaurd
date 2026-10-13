/**
* Copyright (c) 2015 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"
#include "worldSpaceDragDropHandler.h"
#include "snapManager.h"
#include "previewEditMode.h"
#include "previewInput.h"
#include "editAction.h"
#include "../../../common/engine/include/renderFrame.h"
#include "../../../common/renderData/include/renderSelectionResult.h"
#include "../../../common/renderDraw/include/fragmentBuilder.h"

namespace tools
{
	IWorldSpaceDragDropHandler::IWorldSpaceDragDropHandler( red::SharedPtr< rend::DepthResult >& depthMap, red::SharedPtr< rend::Color10Result > normalMap, SnapManager* snapManager )
		: m_lastMouseX( -1 )
		, m_lastMouseY( -1 )
		, m_computedPosition( Vector3::ZEROS() )
		, m_computedRotation( Quaternion::IDENTITY() )
		, m_computedTransformValid( false )
		, m_depthMap( std::move( depthMap ) )
		, m_normalMap( std::move( normalMap ) )
		, m_snapManager( snapManager )
	{
	}

	IWorldSpaceDragDropHandler::~IWorldSpaceDragDropHandler()
	{
	}

	void IWorldSpaceDragDropHandler::OnRender( const CRenderCamera& camera, rend::IDebugDrawer& dd ) const
	{
		if ( m_computedTransformValid )
		{
			rend::FragmentBuilder builder(dd);
			builder.SetColor( Color::WHITE() );
			builder.EnableOverlay( true );
			builder.AddWireSphere( m_computedPosition, 0.1f );
		}
	}

	void IWorldSpaceDragDropHandler::OnMouseMoved( const InputMouseMove& evt )
	{
		RecomputeTransform( evt.m_viewport, evt.m_x, evt.m_y );

		// propagate to the actual world-space event
		if ( m_computedTransformValid )
			OnMouseMoved( m_computedPosition, m_computedRotation );
	}

	Bool IWorldSpaceDragDropHandler::OnFinished( const InputMouseButton& evt )
	{
		RecomputeTransform( evt.m_viewport, evt.m_x, evt.m_y );

		// propagate to the actual world-space event
		if ( m_computedTransformValid )
			return OnFinished( m_computedPosition, m_computedRotation );
		return false;
	}

	void IWorldSpaceDragDropHandler::RecomputeTransform( const EditModeViewport& vpt, const Int32 x, const Int32 y )
	{
		// filter out zero movements
		if ( m_lastMouseX != x || m_lastMouseY != y )
		{
			m_lastMouseX = x;
			m_lastMouseY = y;

			Vector3 hitPosition, hitNormal;
			if ( SolveIntersection( vpt, x, y, hitPosition, hitNormal ) )
			{
				const TranslationSnapParams snapParams =
				{
					Transform( m_computedPosition ),
					hitPosition - m_computedPosition,
					1.0f,
				};

				red::UniquePtr< IEditAction > tempAction;
				Transform snappedTransform = Transform::IDENTITY();

				if ( m_snapManager && m_snapManager->ProcessTranslationalSnap( snapParams, snappedTransform, tempAction ) )
				{
					m_computedPosition += snappedTransform.GetPosition3();
				}
				else
				{
					m_computedPosition = hitPosition;
				}
				
				if ( m_normalMap )
				{
					const Vector3 zAxis = hitNormal;
					const Vector3 xAxis = Vector3 { -zAxis.Y, zAxis.X, 0.0f }; // Parallel to XY plane, perpendicular to hitNormal
					const Vector3 yAxis = zAxis.Cross( xAxis.SquareMag() < 1e-4f ? Vector4::RIGHT() : xAxis.Normalized() ).Normalized();

					const Quaternion rotation = Quaternion().BuildFromDirectionVector( yAxis, zAxis );
					EulerAngles rotationEuler = rotation.ToEulerAngles();
					if ( m_snapManager
						&& m_snapManager->ProcessRotationSnap( { Transform( m_computedPosition ), Vector4::FRONT(), rotationEuler.Roll }, rotationEuler.Roll, tempAction )
						&& m_snapManager->ProcessRotationSnap( { Transform( m_computedPosition ), Vector4::RIGHT(), rotationEuler.Pitch }, rotationEuler.Pitch, tempAction )
						&& m_snapManager->ProcessRotationSnap( { Transform( m_computedPosition ), Vector4::UP(), rotationEuler.Yaw }, rotationEuler.Yaw, tempAction ) )
					{
						m_computedRotation = rotationEuler.ToQuat();
					}
					else
					{
						m_computedRotation = rotation;
					}
				}
				else
				{
					m_computedRotation = Quaternion::IDENTITY();
				}
				m_computedTransformValid = true;
			}			
			else
			{
				// computed transform is no longer valid
				m_computedTransformValid  = false;
			}
		}
	}

	Bool IWorldSpaceDragDropHandler::SolveIntersection( const EditModeViewport& vpt, const Int32 pixelX, const Int32 pixelY, Vector3& outPosition, Vector3& outNormal ) const
	{
		// Get the world space position from the depth map
		Vector3 depthWorldPoint;
		outNormal = Vector3::EZ();
		if ( m_depthMap && m_depthMap->CalcWorldSpacePoint( pixelX, pixelY, depthWorldPoint ) )
		{
			outPosition = depthWorldPoint;
		
			if ( m_normalMap )
			{
				outNormal = m_normalMap->GetNormalized( pixelX, pixelY );
			}

			return true;
		}

		// compute ray for geometry checks
		Vector3 rayOrigin, rayDirection;
		vpt.CalcRay( pixelX, pixelY, rayOrigin, rayDirection );

		// TODO: insert any CUSTOM INTERSECTION CODE HERE

		// If we are above the ground use the Z=0 plane
		if ( rayOrigin.Z > 0.0f )
		{
			const Plane fallbackPlane( Vector4::EZ(), 0.0f );

			Vector4 fallbackPlanePoint = Vector4::ZERO_3D_POINT();
			Float fallbackPlaneDist = 0.0f;
			if ( fallbackPlane.FrontIntersectLine( rayOrigin, rayDirection, fallbackPlanePoint, fallbackPlaneDist ) )
			{
				outPosition = fallbackPlanePoint;
				outNormal = Vector3::EZ();
				return true;
			}
		}

		// Last resort - place object 5m from the camera
		outPosition = rayOrigin + rayDirection * 5.0f;
		outNormal = Vector3::EZ();
		return true;
	}

} // tools