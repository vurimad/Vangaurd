/**
* Copyright (c) 2013 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"
#include "gizmoGroupScale.h"
#include "gizmoLines.h"
#include "gizmoManager.h"
#include "previewInputHandler.h"
#include "previewInput.h"
#include "previewEditMode.h"
#include "gizmoTransformHandler.h"

#include "../../common/renderDraw/include/fragmentBuilder.h"
#include "../../common/world/include/worldNodeTypes.h"

namespace Config
{
	TConfigVar< Float > cvScaleGizmoBoxSize( "Editor/Gizmo/Scale", "BoxSize", 0.05f, 0.02f, 0.15f, eConsoleVarFlag_Save );
	TConfigVar< Float > cvScaleGizmoPlaneOffset( "Editor/Gizmo/Scale", "PlaneOffset", 0.4f, 0.1f, 0.9f, eConsoleVarFlag_Save );
}

namespace tools
{

	class GizmoScaleSingleAxisBaseInputHandler : public IPreviewInputHandler
	{
	public:
		GizmoScaleSingleAxisBaseInputHandler(
			const GizmoTransformHandlerPtr& transformHandler,
			const Vector3& worldBaseDir,
			const GizmoGroupScale::AxesMask axesMask,
			const GizmoTransformMode mode,
			const Transform& worldTransform)
			: m_transformHandler( transformHandler )
			, m_worldDir( worldBaseDir )
			, m_axesMask( axesMask )
			, m_worldTransform( worldTransform )
			, m_firstOffsetCaptured( false )
			, m_lastTargetScaleIsValid( false )
			, m_lastTargetScale( 0.0f )
			, m_mode( mode )
		{
			RED_FATAL_ASSERT( transformHandler );

			transformHandler->OnCaptureTransforms();
		}

		virtual void OnRender( const CRenderCamera& camera, rend::IDebugDrawer& dd ) const override
		{
			if ( m_lastTargetScaleIsValid )
			{
				rend::FragmentBuilder builder(dd);

				builder.AddWorldTextf( m_worldTransform.GetPosition3(), -5, 0, "%1.2f", m_lastTargetScale );
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
				if ( m_firstOffsetCaptured && m_lastTargetScale )
				{
					if ( GizmoTransformHandlerPtr transformHandler = m_transformHandler.Lock() )
					{
						transformHandler->OnApplyScaling( m_worldTransform, m_worldDir, m_lastTargetScale, GizmoTransformHandler::ActionMode::Final, m_mode, m_axesMask );
					}
				}

				return false;
			}

			return true;
		}

	protected:
		GizmoTransformHandlerWeakPtr m_transformHandler;
		Transform m_worldTransform;
		Vector3 m_worldDir;
		Float m_lastTargetScale;
		GizmoGroupScale::AxesMask m_axesMask; // 1 on axes we want to scale, 0 on axis with disabled scaling.
		GizmoTransformMode m_mode;
		Bool m_firstOffsetCaptured;
		Bool m_lastTargetScaleIsValid;
	};

	class GizmoScaleInputHandler : public GizmoScaleSingleAxisBaseInputHandler
	{
	public:
		GizmoScaleInputHandler(
			const GizmoTransformHandlerPtr& transformHandler,
			const Vector3& worldBaseDir,
			GizmoGroupScale::AxesMask axesMask,
			const GizmoTransformMode mode,
			const Transform& worldTransform )
			: GizmoScaleSingleAxisBaseInputHandler( transformHandler, worldBaseDir, axesMask, mode, worldTransform )
			, m_firstOffset( 0.0f )
			, m_totalMovement( 0.0f )
		{
		}

		virtual Bool OnMouseMoved( const InputMouseMove& evt ) override
		{
			GizmoTransformHandlerPtr transformHandler = m_transformHandler.Lock();
			if ( !transformHandler )
			{
				return false;
			}

			// Get plane perpendicular to world movement axis based on the camera position
			const Vector3 worldPos = m_worldTransform.GetPosition3();
			const auto cameraForward = evt.m_viewport.m_camera.GetCameraForward();
			const auto scalePlane = Plane( -cameraForward.Normalized3(), worldPos );

			// Calculate ray at pixel
			Vector3 rayOrigin, rayDir;
			evt.m_viewport.CalcRay( evt.m_x, evt.m_y, rayOrigin, rayDir );

			// Calculate intersection with drag plane
			Vector4 intersectionPoint;
			Float intersectionDistance;
			if ( scalePlane.FrontIntersectLine( rayOrigin, rayDir, intersectionPoint, intersectionDistance ) )
			{
				// Calculate movement delta ALONG THE AXIS
				const Float movement = ( m_worldDir.IsZero() ) ? Vector3::ONES().Dot( intersectionPoint - worldPos ) : m_worldDir.Dot( intersectionPoint - worldPos );

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

					// calculate scale
					Float ssScale = evt.m_viewport.CalcScreenSpaceScale( m_worldTransform.GetPosition3() ) * 0.25f;
					Float base = Min( Max( 1.f / ssScale, 0.05f ), 0.25f );
					const Float scale = powf( 1.f + base, m_totalMovement );

					// compute valid movement
					Float targetScale = 1.0f;
					if ( transformHandler->OnPreProcessScale( m_worldTransform, m_worldDir, scale, targetScale ) )
					{
						m_lastTargetScaleIsValid = true;
						m_lastTargetScale = targetScale;

						transformHandler->OnApplyScaling( m_worldTransform, m_worldDir, m_lastTargetScale, GizmoTransformHandler::ActionMode::Preview, m_mode, m_axesMask );
					}
				}
			}

			// keep alive
			return true;
		}

	private:
		void GetMostPerpendicularPlane( const Vector3& cameraForward, Plane& outPlane )
		{
			Vector3 cross = Vector4::Cross( cameraForward, m_worldDir ).Normalized3();
			Vector3 normal = Vector4::Cross( cross, m_worldDir ).Normalized3();
			outPlane = Plane( normal, m_worldTransform.GetPosition3() );
		}

		Float	m_firstOffset;
		Float	m_totalMovement;
	};

	class GizmoScaleAxis : public IGizmoLines
	{
	public:
		enum ScalingMode : Uint8  // Note: we could use NodeScalingMode here, but it's "All" & "None" values would be confusing here, so I decided to make yet another enum.
		{
			SingleAxis,           // Gizmo scale axis scales along specified direction (1D scaling).
			Uniform,              // Gizmo scale axis scales uniformly (3D scaling).
			UniformXY             // Gizmo scale axis scales uniformly on XY axes (2D scaling aka plane scaling), does not scale on Z axis.
		};

		GizmoScaleAxis( const GizmoTransformHandlerPtr& transformHandler, const Vector3& dir, const Color& lineColor, ScalingMode scalingMode )
			: IGizmoLines( lineColor )
			, m_transformHandler( transformHandler )
			, m_localDir( dir )
			, m_scalingMode( scalingMode )
		{
			// Generate arrow
			const Float len = 1.0f;
			const Float boxSize = Config::cvScaleGizmoBoxSize.Get();

			Matrix space = Vector4(dir).ToEulerAnglesInversePitch().ToMatrix();
			const Vector3 base = space.TransformVector( Vector4::EY() ) * len;
			const Vector3 u = space.TransformVector( Vector4::EX() ) * len * boxSize;
			const Vector3 v = space.TransformVector( Vector4::EZ() ) * len * boxSize;
			const Vector3 w = space.TransformVector( Vector4::EY() ) * len * boxSize;

			m_corners[0] = Vector3::ZEROS();
			m_corners[1] = base - w;
			m_corners[2] = base - u - v - w;
			m_corners[3] = base - u + v - w;
			m_corners[4] = base + u + v - w;
			m_corners[5] = base + u - v - w;
			m_corners[6] = base - u - v + w;
			m_corners[7] = base - u + v + w;
			m_corners[8] = base + u + v + w;
			m_corners[9] = base + u - v + w;

			// Arrow lines
			AddLine( m_corners[0], m_corners[1], Priority_Low );

			AddLine( m_corners[2], m_corners[3], Priority_High );
			AddLine( m_corners[3], m_corners[4], Priority_High );
			AddLine( m_corners[4], m_corners[5], Priority_High );
			AddLine( m_corners[5], m_corners[2], Priority_High );

			AddLine( m_corners[6], m_corners[7], Priority_High );
			AddLine( m_corners[7], m_corners[8], Priority_High );
			AddLine( m_corners[8], m_corners[9], Priority_High );
			AddLine( m_corners[9], m_corners[6], Priority_High );

			AddLine( m_corners[6], m_corners[2], Priority_High );
			AddLine( m_corners[7], m_corners[3], Priority_High );
			AddLine( m_corners[8], m_corners[4], Priority_High );
			AddLine( m_corners[9], m_corners[5], Priority_High );
		}

		virtual Transform CalcPlacement() const override
		{
			return m_transformHandler->GetGizmoPlacement();
		}

		virtual red::UniquePtr< IPreviewInputHandler > OnActivate( const GizmoTransformMode mode )
		{
			RED_FATAL_ASSERT( mode != GizmoTransformMode::ObjectsPivots );

			const auto placement = CalcPlacement();
			const Vector3 worldDir = placement.TransformVector( m_localDir ).Normalized();
			const Vector3 scaleAxis = (m_scalingMode == SingleAxis) ? worldDir : Vector4::ZEROS();
			const GizmoGroupScale::AxesMask axesMask = (m_scalingMode == UniformXY) ? GizmoGroupScale::AxesMaskXY : GizmoGroupScale::AxesMaskAll;

			return red::CreateUniquePtr< GizmoScaleInputHandler >( m_transformHandler, scaleAxis, axesMask, mode, placement );
		}

		virtual void OnRender( const CRenderCamera& camera, rend::IDebugDrawer& dd, ERenderMode mode ) override
		{
			const EditModeViewport vpt( camera, dd );

			rend::HighPriorityFragmentBuilder builder( dd );
			builder.SetColor( GetColor() );
			builder.EnableOverlay( true );

			const Transform transform = CalcPlacement();
			Float screenSpaceScale = vpt.CalcScreenSpaceScale( transform.GetPosition3() );

			Uint32 bottomLeft = 2;
			for ( Uint32 i = 0; i < 3; ++i )
			{
				Uint32 startInd = bottomLeft + i;
				FaceInfo face( m_corners[startInd], m_corners[startInd + 1], m_corners[startInd + 5], m_corners[startInd + 4] );
				AddFace( builder, transform, screenSpaceScale, face );
			}

			FaceInfo face( m_corners[bottomLeft + 3], m_corners[bottomLeft], m_corners[bottomLeft + 4], m_corners[bottomLeft + 7] );
			AddFace( builder, transform, screenSpaceScale, face );

			for ( Uint32 i = 0; i < 2; ++i )
			{
				Uint32 startInd = bottomLeft + i * 4;
				FaceInfo face( m_corners[startInd], m_corners[startInd + 1], m_corners[startInd + 2], m_corners[startInd + 3] );
				AddFace( builder, transform, screenSpaceScale, face );
			}

			IGizmoLines::OnRender( camera, dd, mode );
		}

	private:
		struct FaceInfo
		{
			FaceInfo( const Vector3& v1, const Vector3& v2, const Vector3& v3, const Vector3& v4 )
			{
				m_vertices[0] = v1;
				m_vertices[1] = v2;
				m_vertices[2] = v3;
				m_vertices[3] = v4;
			}

			Vector3 m_vertices[4];
		};

		void AddFace( rend::FragmentBuilder& builder, const Transform& transform, const Float& screenSpaceScale, const FaceInfo& faceInfo )
		{
			Vector3 a[4];
			for ( size_t i = 0; i < 4; ++i )
			{
				a[i] = transform.TransformPoint( faceInfo.m_vertices[i] * screenSpaceScale );
			}

			builder.AddSolidPolygon( a, 4, true );
			builder.AddSolidPolygon( a, 4, false );
		}

		GizmoTransformHandlerPtr m_transformHandler;
		Vector3 m_localDir;
		Vector3 m_corners[10];
		ScalingMode m_scalingMode;
	};

	class GizmoScaleUniform : public IGizmoLines
	{
	public:
		GizmoScaleUniform( const GizmoTransformHandlerPtr& transformHandler, const Color& color, Float scale )
			: IGizmoLines( color )
			, m_transformHandler( transformHandler )
			, m_planeColor( color )
			, m_scale( scale )
		{
			m_corners[0] = Vector4::EX();
			m_corners[1] = Vector4::EY();
			m_corners[2] = Vector4::EZ();
			m_corners[3] = Vector4::EX();

			for(int i = 0; i < 4; ++i )
				m_corners[i] *= m_scale;

			AddLine( m_corners[0], m_corners[1], Priority_Mid );
			AddLine( m_corners[1], m_corners[2], Priority_Mid );
			AddLine( m_corners[2], m_corners[3], Priority_Mid );
		}

		virtual Transform CalcPlacement() const override
		{
			return m_transformHandler->GetGizmoPlacement();
		}

		virtual red::UniquePtr< IPreviewInputHandler > OnActivate( const GizmoTransformMode mode )
		{
			RED_FATAL_ASSERT( mode != GizmoTransformMode::ObjectsPivots );

			const auto placement = CalcPlacement();
			return red::CreateUniquePtr< GizmoScaleInputHandler >( m_transformHandler, Vector4::ZEROS(), GizmoGroupScale::AxesMaskAll, mode, placement );
		}

	private:
		GizmoTransformHandlerPtr m_transformHandler;
		Vector4 m_corners[4]; //< 0 -> Bottom Left 1 -> Bottom Right 2 -> Top Left 3 -> Top Right
		Color m_planeColor;
		Float m_scale;
	};

	//----

	GizmoGroupScale::GizmoGroupScale( GizmoManager* manager, const GizmoTransformHandlerPtr& transformHandler, world::NodeScalingMode scalingMode )
		: m_manager( manager )
		, m_gizmos{ red::PoolBackend() }
	{
		// translate node scaling mode into gizmo axes scaling modes.
		GizmoScaleAxis::ScalingMode scalingModeFoAxesXY = GizmoScaleAxis::Uniform;
		GizmoScaleAxis::ScalingMode scalingModeFoAxisZ = GizmoScaleAxis::Uniform;

		if ( scalingMode == world::NodeScalingMode::All )
			scalingModeFoAxesXY = scalingModeFoAxisZ = GizmoScaleAxis::SingleAxis;
		else if ( scalingMode == world::NodeScalingMode::UniformXY )
		{
			scalingModeFoAxesXY = GizmoScaleAxis::UniformXY;
			scalingModeFoAxisZ = GizmoScaleAxis::SingleAxis;
		}

		// create the axis translations
		m_gizmos.PushBack( red::CreateUniquePtr< GizmoScaleAxis >( transformHandler, Vector3::EX(), Color::RED(), scalingModeFoAxesXY ) );
		m_gizmos.PushBack( red::CreateUniquePtr< GizmoScaleAxis >( transformHandler, Vector3::EY(), Color::GREEN(), scalingModeFoAxesXY ) );
		m_gizmos.PushBack( red::CreateUniquePtr< GizmoScaleAxis >( transformHandler, Vector3::EZ(), Color::BLUE(), scalingModeFoAxisZ ) );

		// TODO plane scaling

		// uniform scale
		Float planeOffset = Config::cvScaleGizmoPlaneOffset.Get();
		m_gizmos.PushBack( red::CreateUniquePtr< GizmoScaleUniform >( transformHandler, Color::WHITE(), planeOffset ) );

		// register the gizmos
		for ( const auto& ptr : m_gizmos )
		{
			m_manager->Register( ptr.Get() );
		}
	}

	GizmoGroupScale::~GizmoGroupScale()
	{
		// unregister the gizmos
		for ( const auto& ptr : m_gizmos )
		{
			m_manager->Unregister( ptr.Get() );
		}
	}

} // tools