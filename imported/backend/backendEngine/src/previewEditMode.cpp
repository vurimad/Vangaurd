/**
* Copyright (c) 2013 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"
#include "previewPanel.h"
#include "previewEditMode.h"

#include "../../../common/engine/include/renderFrame.h"

namespace tools
{
	//--------------------

	EditModeViewport::EditModeViewport()
		: m_viewportWidth( 100 )
		, m_viewportHeight( 100 )
	{}

	EditModeViewport::EditModeViewport( const CRenderCamera& camera, rend::IDebugDrawer& dd )
		: m_viewportWidth( dd.GetWidth() )
		, m_viewportHeight( dd.GetHeight() )
		, m_camera( camera )
	{}

	void EditModeViewport::CalcRay( const Int32 x, const Int32 y, Vector3& outPosition, Vector3& outDirection ) const
	{
		const Float halfWidth = (Float)m_viewportWidth * 0.5f;
		const Float halfHeight = (Float)m_viewportHeight * 0.5f;

		// Calculate screen space point
		Vector4 screenSpacePoint, startPoint, endPoint;
		screenSpacePoint.X = ( x / halfWidth ) - 1.0f;
		screenSpacePoint.Y = 1.0f - ( y / halfHeight );
		screenSpacePoint.Z = 0.0f;
		screenSpacePoint.W = 1.0f;

		if ( m_camera.GetFOV() == 0 )
		{
			// Transform to world space
			startPoint = m_camera.GetScreenToWorld().TransformVectorWithW( screenSpacePoint );
			startPoint /= startPoint.W;

			// End point
			screenSpacePoint.Z = 1.0f;
			endPoint = m_camera.GetScreenToWorld().TransformVectorWithW( screenSpacePoint );
			endPoint /= endPoint.W;

			// Use camera origin as ray origin
			outPosition = startPoint;
			outDirection = ( endPoint - startPoint ).Normalized3();
		}
		else
		{
			outPosition = m_camera.GetPosition().AsVector3();

			const Float yFactor = MTan( 0.5f * DEG2RAD( m_camera.GetFOV() ) );
			const Float xFactor = yFactor * m_camera.GetAspect();

			outDirection = m_camera.GetCameraForward();
			outDirection += m_camera.GetCameraRight() * ( screenSpacePoint.X * xFactor );
			outDirection += m_camera.GetCameraUp() * ( screenSpacePoint.Y * yFactor );
			outDirection.Normalize();
		}
	}

	Bool EditModeViewport::WorldToScreen( const Vector3& worldPosition, Vector3& outScreenPosition ) const
	{
		// Get viewport dimensions
		const Float halfWidth = (Float)m_viewportWidth * 0.5f;
		const Float halfHeight = (Float)m_viewportHeight * 0.5f;

		// Transform to screen space
		Vector4 worldSpacePoint( worldPosition.X, worldPosition.Y, worldPosition.Z, 1.0f );
		Vector4 screenSpacePoint = m_camera.GetWorldToScreen().TransformVectorWithW( worldSpacePoint );

		// Behind near plane
		if ( screenSpacePoint.W < 0.001f )
		{
			// Clamp to one of the screen corners
			screenSpacePoint.X = ( screenSpacePoint.X < 0.0f) ? -1.0f : 1.0f;
			screenSpacePoint.Y = ( screenSpacePoint.Y < 0.0f) ? -1.0f : 1.0f;
			screenSpacePoint.Z = 0.0f;
		}
		else
		{
			// Vertex inside screen space, project
			screenSpacePoint /= screenSpacePoint.W;
		}

		// Convert to pixel coordinates
		outScreenPosition.X = (( screenSpacePoint.X + 1.0f)*halfWidth)  + m_camera.GetSubpixelOffsetX();
		outScreenPosition.Y = ((-screenSpacePoint.Y + 1.0f)*halfHeight) + m_camera.GetSubpixelOffsetY();
		outScreenPosition.Z = (m_camera.GetPosition() - WorldPosition(worldPosition)).Mag();

        return true;
	}

	void EditModeViewport::WorldToScreen( const Vector3* points, Vector3* screenSpacePoints, Uint32 count ) const
	{
		// Get viewport dimensions
		const Float halfWidth = (Float)m_viewportWidth * 0.5f;
		const Float halfHeight = (Float)m_viewportHeight * 0.5f;

		// Project points
		for ( Uint32 i=0; i<count; i++ )
		{
			// Transform to screen space
			Vector4 worldSpacePoint( points[i].X, points[i].Y, points[i].Z, 1.0f );
			Vector4 screenSpacePoint = m_camera.GetWorldToScreen().TransformVectorWithW( worldSpacePoint );

			// Behind near plane
			if ( screenSpacePoint.W < 0.001f )
			{
				// Clamp to one of the screen corners
				screenSpacePoint.X = ( screenSpacePoint.X < 0.0f) ? -1.0f : 1.0f;
				screenSpacePoint.Y = ( screenSpacePoint.Y < 0.0f) ? -1.0f : 1.0f;
				screenSpacePoint.Z = 0.0f;
			}
			else
			{
				// Vertex inside screen space, project
				screenSpacePoint /= screenSpacePoint.W;
			}

			// Convert to pixel coordinates
			screenSpacePoints[i].X = (( screenSpacePoint.X + 1.0f)*halfWidth) + m_camera.GetSubpixelOffsetX();
			screenSpacePoints[i].Y = ((-screenSpacePoint.Y + 1.0f)*halfHeight) + m_camera.GetSubpixelOffsetY();
			screenSpacePoints[i].Z = screenSpacePoint.Z;
		}
	}

	Float EditModeViewport::CalcScreenSpaceScale( const Vector3& centerPoint ) const
	{
		// Transform point from world space to screen space
		Vector4 worldSpacePoint( centerPoint.X, centerPoint.Y, centerPoint.Z, 1.0f );
		Vector4 screenSpacePoint = m_camera.GetWorldToScreen().TransformVectorWithW( worldSpacePoint );

		// Calculate render scale ( empirical :P )
		const Matrix& projectionMatrix = m_camera.GetViewToScreen();
		return screenSpacePoint.W * ( 256.0f / (Float)m_viewportWidth / projectionMatrix[0].X );
	}

	void EditModeViewport::ScreenToWorld( const Vector3& screenPosition, Vector3& outWorldPosition ) const
	{
        Vector4 screenSpacePoint( screenPosition, 1.0f );
		Vector4 worldSpacePoint = m_camera.GetScreenToWorld().TransformVectorWithW( screenSpacePoint );
        outWorldPosition = worldSpacePoint.AsVector3();

		if( worldSpacePoint.W != 0.0f )
		{
			outWorldPosition /= worldSpacePoint.W;
		}
	}

	//--------------------

	IPreviewEditMode::IPreviewEditMode()
	{
	}

	IPreviewEditMode::~IPreviewEditMode()
	{
	}

	const AnsiChar* IPreviewEditMode::GetDisplayName() const
	{
		return "";
	}

    Color IPreviewEditMode::GetDisplayColor() const
    {
        return {182, 73, 73};
    }

	Bool IPreviewEditMode::OnInitialize( const PreviewPanelSharedDataPtr& sharedData )
	{
		return true;
	}

	Bool IPreviewEditMode::OnActivate( IPreviewEditMode* currentMode )
	{
		return true;
	}

	void IPreviewEditMode::OnDeactivate()
	{
		// filled in in child classes	
	}

	void IPreviewEditMode::OnRender( CRenderFrameInfo& frame, CRenderCameraFrameInfo& fcameraInfo )
	{
		// filled in in child classes
	}

	void IPreviewEditMode::OnUpdate( const UpdateContext& ctx )
	{
		// filled in in child classes
	}

    void IPreviewEditMode::NotifyPanelCreated(PreviewPanel* panel)
    {
        // filled in in child classes
    }

	void IPreviewEditMode::NotifyPanelDestroyed(PreviewPanel* panel)
    {
        // filled in in child classes
    }

	void IPreviewEditMode::OnSetupInterop( ::interop::utils::Object& op )
	{
		// filled in in child classes
	}

	//--------------------

} // tools