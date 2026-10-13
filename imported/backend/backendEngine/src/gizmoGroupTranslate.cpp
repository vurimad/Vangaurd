/**
* Copyright (c) 2013 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"
#include "gizmoLines.h"
#include "gizmoManager.h"
#include "gizmoTransformHandler.h"
#include "gizmoGroupTranslate.h"
#include "previewInputHandler.h"
#include "previewInput.h"
#include "previewEditMode.h"

#include "../../../common/renderDraw/include/fragmentBuilder.h"

namespace Config
{
	TConfigVar< Float > cvTranslateGizmoArrowSize( "Editor/Gizmo/Translate", "ArrowSize", 0.2f, 0.1f, 0.5f, eConsoleVarFlag_Save );
	TConfigVar< Float > cvTranslateGizmoPlanesOffset( "Editor/Gizmo/Translate", "PlanesOffset", 0.4f, 0.1f, 0.9f, eConsoleVarFlag_Save );
	TConfigVar< Bool > cvTranslateGizmoShowGuideLine( "Editor/Gizmo/Translate", "ShowGuideLine", true );
	TConfigVar< Color > cvTranslationVectorColor( "Editor/Gizmo/Translate", "MoveVectorColor", Color::GRAY(), eConsoleVarFlag_Save );
}

namespace tools
{

	class GizmoTranslateBaseInputHandler : public IPreviewInputHandler
	{
	public:
		GizmoTranslateBaseInputHandler( const GizmoTransformHandlerPtr& transformHandler, const Vector3& worldBaseDir, const Transform& worldTransform, const GizmoTransformMode mode )
			: m_transformHandler( transformHandler )
			, m_mode( mode )
			, m_worldDir( worldBaseDir )
			, m_worldTransform( worldTransform )
			, m_firstOffsetCaptured( false )
			, m_lastTargetMovementIsValid( false )
		{
			RED_FATAL_ASSERT( transformHandler );

			// begin operation
			transformHandler->OnCaptureTransforms();
			m_lastTargetMovement = Transform::IDENTITY();
		}

		virtual void OnRender( const CRenderCamera& camera, rend::IDebugDrawer& dd ) const override
		{
			GizmoTransformHandlerPtr transformHandler = m_transformHandler.Lock();
			if ( m_lastTargetMovementIsValid && transformHandler )
			{
				rend::HighPriorityFragmentBuilder builder(dd);
				builder.EnableOverlay( true );

				const Vector3 start = m_worldTransform.GetPosition(); // actual zero
				const Vector3 end = m_worldTransform.GetPosition() + m_lastTargetMovement.GetPosition();
				builder.SetColor( Config::cvTranslationVectorColor.Get() );
				builder.AddLine( start, end );

				const Vector3 halfPos = (start+end) * 0.5f;
				Vector4 diff( end - start );

				const Transform transform = transformHandler->GetGizmoPlacement();
				diff = transform.GetInverse().TransformVector( diff );

				builder.SetColor( Color::WHITE() );
				builder.AddWorldTextf( halfPos, -5, 0, "%1.2f: [%1.2f, %1.2f, %1.2f]", diff.Mag3(), diff.X, diff.Y, diff.Z );
			}
		}

		virtual void OnCanceled() override
		{
			if ( GizmoTransformHandlerPtr transformHandler = m_transformHandler.Lock() )
			{
				transformHandler->OnRevertTransforms();
			}
		}

		virtual Bool OnMouseUp( const InputMouseButton& evt ) override
		{
			if ( evt.m_button == LMB )
			{
				GizmoTransformHandlerPtr transformHandler = m_transformHandler.Lock();
				if ( transformHandler && m_firstOffsetCaptured && m_lastTargetMovementIsValid )
				{
					transformHandler->OnApplyMovement( m_worldTransform, m_lastTargetMovement, GizmoTransformHandler::ActionMode::Final, m_mode );
				}
				return false;
			}

			return true;
		}

	protected:
		GizmoTransformHandlerWeakPtr m_transformHandler;
		Transform m_worldTransform;
		Transform m_lastTargetMovement;
		Vector3 m_worldDir;
		GizmoTransformMode m_mode;
		Bool m_firstOffsetCaptured;
		Bool m_lastTargetMovementIsValid;
	};

	class GizmoTranslateAxisInputHandler : public GizmoTranslateBaseInputHandler
	{
	public:
		GizmoTranslateAxisInputHandler( const GizmoTransformHandlerPtr& transformHandler, const Vector3& worldBaseDir, const Transform& worldTransform, const GizmoTransformMode mode )
			: GizmoTranslateBaseInputHandler( transformHandler, worldBaseDir, worldTransform, mode )
			, m_firstOffset( 0.0f )
			, m_totalMovement( 0.0f )
		{
			// compute initial (zero) movement
			Transform targetMovement;
			if ( transformHandler->OnPreProcessMove(worldTransform, worldBaseDir, 0.0f, targetMovement))
			{
				m_lastTargetMovementIsValid = true;
				m_lastTargetMovement = targetMovement;
				transformHandler->OnApplyMovement( worldTransform, targetMovement, GizmoTransformHandler::ActionMode::Preview, m_mode );
			}
		}

		virtual Bool OnMouseMoved( const InputMouseMove& evt ) override
		{
			GizmoTransformHandlerPtr transformHandler = m_transformHandler.Lock();
			if ( !transformHandler )
			{
				return false;
			}

			// Calculate ray at pixel
			Vector3 rayOrigin, rayDir;
			evt.m_viewport.CalcRay( evt.m_x, evt.m_y, rayOrigin, rayDir );

			// Avoid situations ray is perfectly parallel to the axis
			if ( rayDir.Cross( m_worldDir ).IsAlmostZero() )
			{
				// keep alive
				return true;
			}

			Plane transformPlane;
			{
				// create a plane that overlaps drag line and is
				const Vector4 p0 = m_worldTransform.GetPosition();
				const Vector4 p1 = p0 + m_worldDir;
				const Vector4 p2 = p0 + ( Vector4::Cross( rayOrigin - p0, m_worldDir ) );
				transformPlane.SetPlane( p0, p1, p2 );
			}

			// Calculate intersection with drag plane
			Vector4 intersectionPoint;
			Float intersectionDistance;
			if ( transformPlane.FrontIntersectLine( rayOrigin, rayDir, intersectionPoint, intersectionDistance ) )
			{
				// Calculate movement delta ALONG THE AXIS
				const Float movement = m_worldDir.Dot( intersectionPoint - m_worldTransform.GetPosition() );

				// Offset
				if ( !m_firstOffsetCaptured )
				{
					m_firstOffsetCaptured = true;
					m_firstOffset = movement;
				}

				// Update
				const Float adjustedMovementDelta = movement - m_firstOffset;
				if ( m_totalMovement != adjustedMovementDelta )
				{
					m_totalMovement = adjustedMovementDelta;

					// compute valid movement
					Transform targetMovement;
					if ( transformHandler->OnPreProcessMove( m_worldTransform, m_worldDir, m_totalMovement, targetMovement ) )
					{
						m_lastTargetMovementIsValid = true;
						m_lastTargetMovement = targetMovement;
						transformHandler->OnApplyMovement( m_worldTransform, m_lastTargetMovement, GizmoTransformHandler::ActionMode::Preview, m_mode );
					}
				}
			}

			// keep alive
			return true;
		}

		virtual void OnRender( const CRenderCamera& camera, rend::IDebugDrawer& dd ) const override
		{
			const float guideLineRange = 2000.0f;

			if ( Config::cvTranslateGizmoShowGuideLine.Get() )
			{
				rend::HighPriorityFragmentBuilder builder( dd );

				const Vector3 worldPos = m_worldTransform.GetPosition();

				builder.SetColor( Color::GRAY() );
				builder.EnableOverlay( false );
				builder.AddLine( worldPos, worldPos + m_worldDir * guideLineRange );
				builder.AddLine( worldPos, worldPos - m_worldDir * guideLineRange );

				GizmoTranslateBaseInputHandler::OnRender( camera, dd );
			}
		}

	private:
		Float m_firstOffset;
		Float m_totalMovement;
	};

	class GizmoTranslatePlaneInputHandler : public GizmoTranslateBaseInputHandler
	{
	public:
		GizmoTranslatePlaneInputHandler( const GizmoTransformHandlerPtr& transformHandler, const Vector4& worldDir, const Transform& worldTransform, const GizmoTransformMode mode )
			: GizmoTranslateBaseInputHandler( transformHandler, worldDir, worldTransform, mode )
		{
			m_worldPlane.SetPlane( worldDir, Vector4::ZEROS() );
		}

		virtual Bool OnMouseMoved( const InputMouseMove& evt ) override
		{
			GizmoTransformHandlerPtr transformHandler = m_transformHandler.Lock();
			if ( !transformHandler )
			{
				return false;
			}

			Plane transformPlane;
			const auto cameraForward = evt.m_viewport.m_camera.GetCameraForward();
			CorrectTranslationPlane( cameraForward, transformPlane );

			Vector3 rayOrigin, rayDir;
			evt.m_viewport.CalcRay( evt.m_x, evt.m_y, rayOrigin, rayDir );
			rayOrigin -= m_worldTransform.GetPosition();

			Vector4 intersectionPoint;
			Float intersectionDistance;
			if ( transformPlane.FrontIntersectLine( rayOrigin, rayDir, intersectionPoint, intersectionDistance ) )
			{
				if(!m_firstOffsetCaptured)
				{
					m_firstOffsetCaptured	   = true;
					m_initialIntersectionPoint = intersectionPoint;
				}
				
				const Vector3 direction	= (intersectionPoint - m_initialIntersectionPoint).Normalized3();
				const Float movement	= (intersectionPoint - m_initialIntersectionPoint).Mag3();

				if( movement > 0.f )
				{
					Transform targetMovement;
					if ( transformHandler->OnPreProcessMove( m_worldTransform, direction, movement, targetMovement ) )
					{
						m_lastTargetMovementIsValid = true;
						m_lastTargetMovement = targetMovement;
						m_worldDir = direction;

						transformHandler->OnApplyMovement( m_worldTransform, targetMovement, GizmoTransformHandler::ActionMode::Preview, m_mode );
					}
				}
			}

			return true;
		}

	private:
		Plane	m_worldPlane;
		Vector4	m_initialIntersectionPoint;

		void CorrectTranslationPlane( const Vector3& cameraForward, Plane& outPlane )
		{
			const Vector3 planeNormal = m_worldPlane.GetVectorRepresentation().AsVector3();
			if( cameraForward.Dot(planeNormal) >= 0 )
			{
				outPlane.SetPlane(-m_worldPlane.GetVectorRepresentation());
			}
			else
			{
				outPlane.SetPlane(m_worldPlane.GetVectorRepresentation());
			}
		}
	};

	class GizmoTranslateAxis : public IGizmoLines
	{
	public:
		GizmoTranslateAxis( const GizmoTransformHandlerPtr& transformHandler, const Vector3& dir, const Color& lineColor )
			: IGizmoLines( lineColor )
			, m_transformHandler( transformHandler )
			, m_localDir( dir )
		{
			// Generate arrow
			const Float len = 1.0f;
			const Float baseLen = len - Config::cvTranslateGizmoArrowSize.Get();

			Matrix space = Vector4(dir).ToEulerAnglesInversePitch().ToMatrix();
			const Vector3 head = space.TransformVector( Vector4::EY() ) * len;
			const Vector3 base = head * baseLen;
			const Vector3 u = space.TransformVector( Vector4::EX() ) * len * 0.05f;
			const Vector3 v = space.TransformVector( Vector4::EZ() )* len * 0.05f;

			m_corners[0] = Vector3::ZEROS();
			m_corners[1] = head;
			m_corners[2] = base - u - v;
			m_corners[3] = base + u - v;
			m_corners[4] = base + u + v;
			m_corners[5] = base - u + v;

			// Arrow lines
			AddLine( m_corners[0], m_corners[1], Priority_Low );

			AddLine( m_corners[1], m_corners[2], Priority_High );
			AddLine( m_corners[1], m_corners[3], Priority_High );
			AddLine( m_corners[1], m_corners[4], Priority_High );
			AddLine( m_corners[1], m_corners[5], Priority_High );

			AddLine( m_corners[2], m_corners[3], Priority_High );
			AddLine( m_corners[3], m_corners[4], Priority_High );
			AddLine( m_corners[4], m_corners[5], Priority_High );
			AddLine( m_corners[5], m_corners[2], Priority_High );
		}

		virtual Transform CalcPlacement() const override
		{
			// pass directly to translation handler
			return m_transformHandler->GetGizmoPlacement();
		}

		virtual red::UniquePtr< IPreviewInputHandler > OnActivate( const GizmoTransformMode mode )
		{
			const auto placement = CalcPlacement();
			const Vector3 worldDir = placement.TransformVector( m_localDir ).Normalized();
			return red::CreateUniquePtr< GizmoTranslateAxisInputHandler >( m_transformHandler, worldDir, placement, mode );
		}

		virtual Bool OnCanRender( const CRenderCamera& camera ) override
		{
			const auto placement = CalcPlacement();

			const Vector4 worldSpaceStart = placement.GetPosition3();
			Vector4 screenSpaceStart = camera.GetWorldToScreen().TransformVectorWithW( worldSpaceStart );

			// empirical values...
			const Float gizmoAxisVisibilityTreshold = 0.001f;
			const Float gizmoAxisScale = 0.1f * screenSpaceStart.W / camera.GetViewToScreen()[0].X;
			const Vector4 worldSpaceEnd = worldSpaceStart + placement.TransformVector( m_localDir ) * gizmoAxisScale;
			Vector4 screenSpaceEnd = camera.GetWorldToScreen().TransformVectorWithW( worldSpaceEnd );

			// calculate axis direction in screen space
			screenSpaceStart /= screenSpaceStart.W;
			screenSpaceEnd /= screenSpaceEnd.W;
			const Vector2 screenSpaceDir = Vector2 ( screenSpaceEnd ) - Vector2 ( screenSpaceStart );

			// don't show the gizmo axis if it's almost parallel with the camera sight
			if ( math::Abs( screenSpaceDir.X ) < gizmoAxisVisibilityTreshold && math::Abs( screenSpaceDir.Y ) < gizmoAxisVisibilityTreshold )
				return false;

			return true;
		}

		virtual void OnRender( const CRenderCamera& camera, rend::IDebugDrawer& dd, ERenderMode mode ) override
		{
			const EditModeViewport vpt( camera, dd );

			rend::HighPriorityFragmentBuilder builder( dd );

			Vector3 a[3] = { m_corners[1], m_corners[2], m_corners[3] };
			Vector3 b[3] = { m_corners[1], m_corners[3], m_corners[4] };
			Vector3 c[3] = { m_corners[1], m_corners[4], m_corners[5] };
			Vector3 d[3] = { m_corners[1], m_corners[5], m_corners[2] };

			const Transform placement = CalcPlacement();
			Float screenSpaceScale = vpt.CalcScreenSpaceScale( placement.GetPosition() );

			for ( size_t i = 0; i < 3; ++i )
			{
				a[i] = placement.TransformPoint( a[i] * screenSpaceScale );
				b[i] = placement.TransformPoint( b[i] * screenSpaceScale );
				c[i] = placement.TransformPoint( c[i] * screenSpaceScale );
				d[i] = placement.TransformPoint( d[i] * screenSpaceScale );
			}

			builder.SetColor( GetColor() );
			builder.EnableOverlay( true );
			builder.AddSolidPolygon( a, 3, true );
			builder.AddSolidPolygon( a, 3, false );

			builder.AddSolidPolygon( b, 3, true );
			builder.AddSolidPolygon( b, 3, false );

			builder.AddSolidPolygon( c, 3, true );
			builder.AddSolidPolygon( c, 3, false );

			builder.AddSolidPolygon( d, 3, true );
			builder.AddSolidPolygon( d, 3, false );

			IGizmoLines::OnRender( camera, dd, mode );
		}

		RED_INLINE virtual Bool SupportsCloning() const override { return true; }
		RED_INLINE virtual Bool SupportsPivotMovement() const override { return true; }

	private:
		GizmoTransformHandlerPtr m_transformHandler;
		Vector3 m_localDir;
		Vector3 m_corners[6]; // base, head, bottom left, bottom right, top right, top left
	};

	class GizmoTranslatePlane : public IGizmoLines
	{
	public:
		GizmoTranslatePlane( const GizmoTransformHandlerPtr& transformHandler, const Plane& plane, const Color& color, Float scale )
			: IGizmoLines( color )
			, m_localPlane( plane )
			, m_transformHandler( transformHandler )
			, m_planeColor( color )
			, m_scale( scale )
		{
			m_corners[0] = Vector4::ZEROS();

			if( m_localPlane.GetVectorRepresentation() == Vector4::EX() )
			{
				m_corners[1] = Vector4::EY();
				m_corners[2] = Vector4::EZ();

			}
			else if( m_localPlane.GetVectorRepresentation() == Vector4::EY() )
			{
				m_corners[1] = Vector4::EX();
				m_corners[2] = Vector4::EZ();
			}
			else
			{
				m_corners[1] = Vector4::EX();
				m_corners[2] = Vector4::EY();
			}

			m_corners[3] = m_corners[1] + m_corners[2];

			for(int i = 0; i < 4; ++i )
				m_corners[i] *= m_scale;

			AddLine( m_corners[0], m_corners[1], Priority_Mid );
			AddLine( m_corners[1], m_corners[3], Priority_Mid );
			AddLine( m_corners[3], m_corners[2], Priority_Mid );
			AddLine( m_corners[2], m_corners[0], Priority_Mid );
		}

		virtual Transform CalcPlacement() const override
		{
			return m_transformHandler->GetGizmoPlacement();
		}

		virtual red::UniquePtr< IPreviewInputHandler > OnActivate( const GizmoTransformMode mode )
		{
			const auto placement = CalcPlacement();
			const Vector4 worldDir = placement.TransformVector( m_localPlane.GetVectorRepresentation() ).Normalized3();
			return red::CreateUniquePtr< GizmoTranslatePlaneInputHandler >( m_transformHandler, worldDir, placement, mode );
		}
			
		RED_INLINE virtual Bool SupportsCloning() const override { return true; }
		RED_INLINE virtual Bool SupportsPivotMovement() const override { return true; }

	private:
		GizmoTransformHandlerPtr m_transformHandler;
		Plane m_localPlane;
		Vector4 m_corners[4]; //< 0 -> Bottom Left 1 -> Bottom Right 2 -> Top Left 3 -> Top Right
		Color m_planeColor;
		Float m_scale;
	};

	//----

	GizmoGroupTranslate::GizmoGroupTranslate( GizmoManager* manager, const GizmoTransformHandlerPtr& transformHandler, const Uint8 axisMask /*= 7*/, const Bool hasPlanes /*= true*/ )
		: m_manager( manager )
		, m_gizmos{ red::PoolBackend() }
	{
		// create the planes
		if ( hasPlanes )
		{
			Plane xy(Vector4::EZ(), Vector4::ZERO_3D_POINT());
			Plane xz(Vector4::EY(), Vector4::ZERO_3D_POINT());
			Plane yz(Vector4::EX(), Vector4::ZERO_3D_POINT());

			Float planeOffset = Config::cvTranslateGizmoPlanesOffset.Get();
			m_gizmos.PushBack( red::CreateUniquePtr< GizmoTranslatePlane >( transformHandler, xy, Color::BLUE(), planeOffset ) );
			m_gizmos.PushBack( red::CreateUniquePtr< GizmoTranslatePlane >( transformHandler, xz, Color::GREEN(), planeOffset ) );
			m_gizmos.PushBack( red::CreateUniquePtr< GizmoTranslatePlane >( transformHandler, yz, Color::RED(), planeOffset ) );
		}

		// create the axis translations
		if ( axisMask & 1 )
			m_gizmos.PushBack( red::CreateUniquePtr< GizmoTranslateAxis >( transformHandler, Vector3::EX(), Color::RED() ) );
		if ( axisMask & 2 )
			m_gizmos.PushBack( red::CreateUniquePtr< GizmoTranslateAxis >( transformHandler, Vector3::EY(), Color::GREEN() ) );
		if ( axisMask & 4 )
			m_gizmos.PushBack( red::CreateUniquePtr< GizmoTranslateAxis >( transformHandler, Vector3::EZ(), Color::BLUE() ) );

		// register the gizmos
		for ( const auto& ptr : m_gizmos )
		{
			m_manager->Register( ptr.Get() );
		}
	}

	GizmoGroupTranslate::~GizmoGroupTranslate()
	{
		// unregister the gizmos
		for ( const auto& ptr : m_gizmos )
		{
			m_manager->Unregister( ptr.Get() );
		}
	}

} // tools