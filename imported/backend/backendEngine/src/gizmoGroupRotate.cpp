/**
* Copyright (c) 2015-2017 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"
#include "gizmoGroupRotate.h"
#include "gizmoLines.h"
#include "gizmoManager.h"
#include "gizmoCollection.h"
#include "previewInputHandler.h"
#include "previewInput.h"
#include "previewEditMode.h"
#include "gizmoTransformHandler.h"
#include "../../../common/redReflection/include/mathCommon.h"
#include "../../../common/renderDraw/include/fragmentBuilder.h"

using red::DynArray;

namespace Config
{
	TConfigVar< Float > cvRotateGizmoOuterRadius( "Editor/Gizmo/Rotate", "OuterRadius", 1.0f, eConsoleVarFlag_Save | eConsoleVarFlag_Developer );
	TConfigVar< Float > cvRotateGizmoInnerRadius( "Editor/Gizmo/Rotate", "InnerRadius", 0.9f, eConsoleVarFlag_Save | eConsoleVarFlag_Developer );
	TConfigVar< Float > cvRotateGizmoFillAlpha( "Editor/Gizmo/Rotate", "FillAlpha", 0.4f, eConsoleVarFlag_Save | eConsoleVarFlag_Developer );
	TConfigVar< Float > cvRotateGizmoMarkerSize( "Editor/Gizmo/Rotate", "MarkerSize", 0.05f, eConsoleVarFlag_Save | eConsoleVarFlag_Developer );
	TConfigVar< Color > cvRotateGizmoMarkerColor( "Editor/Gizmo/Rotate", "MarkerColor", Color::YELLOW(), eConsoleVarFlag_Save | eConsoleVarFlag_Developer );
	TConfigVar< Color > cvRotateGizmoGridLinesColor( "Editor/Gizmo/Rotate", "GridLinesColor", Color::YELLOW(), eConsoleVarFlag_Save | eConsoleVarFlag_Developer );

	TConfigVar< Float > cvRotateGizmoBaseRotationSpeed( "Editor/Gizmo/Rotate", "BaseSpeed", 0.5f, 0.1f, 10.0f, eConsoleVarFlag_Save );
	TConfigVar< Float > cvRotateGizmoPrecissionMultiplier( "Editor/Gizmo/Rotate", "PrecissionModeMultiplier", 0.1f, 0.01f, 0.5f, eConsoleVarFlag_Save );
}

namespace tools
{

	class GizmoRotateAxisInputHandler : public IPreviewInputHandler
	{
	public:
		GizmoRotateAxisInputHandler( const GizmoTransformHandlerPtr& transformHandler, const Vector3& worldBaseDir, const Vector3& rotationDirection, const Transform& worldTransform, Float* backingRotation, const GizmoTransformMode mode )
			: m_transformHandler( transformHandler )
			, m_mode( mode )
			, m_worldDir( worldBaseDir )
			, m_worldTransform( worldTransform )
			, m_rotationDirection( rotationDirection )
			, m_mousePosition( Vector2::ZEROS() )
			, m_totalMovement( 0.0f )
			, m_externalRotationAngle( backingRotation )
			, m_lastTargetRotationIsValid( false )
			, m_lastTargetRotation( 0.0f )
			, m_firstOffsetCaptured( false )
			, m_precissionMode( false )
		{
			RED_FATAL_ASSERT( transformHandler );

			transformHandler->OnCaptureTransforms();
		}

		virtual void OnRender( const CRenderCamera& camera, rend::IDebugDrawer& dd ) const override
		{
			// DONE EXTERNALLY
		}

		virtual void OnCanceled() override
		{
			if ( GizmoTransformHandlerPtr transformHandler = m_transformHandler.Lock() )
			{
				transformHandler->OnRevertTransforms();
			}
		}

		virtual Bool OnKeyDown( const InputKeyEvent& evt ) override
		{
			if ( evt.m_key == IK_LControl )
			{
				m_precissionMode = true;
			}

			return true;
		}

		virtual Bool OnKeyUp( const InputKeyEvent& evt ) override
		{
			if ( evt.m_key == IK_LControl )
			{
				m_precissionMode = false;
			}

			return true;
		}

		virtual Bool OnMouseUp( const InputMouseButton& evt ) override
		{
			if ( evt.m_button == LMB )
			{
				if ( m_lastTargetRotationIsValid )
				{
					if ( GizmoTransformHandlerPtr transformHandler = m_transformHandler.Lock() )
					{
						const Transform transform( Quaternion( m_worldDir.Normalized(), DEG2RAD( m_lastTargetRotation ) ) );
						transformHandler->OnApplyMovement( m_worldTransform, transform, GizmoTransformHandler::ActionMode::Final, m_mode );
					}
				}

				return false;
			}

			return true;
		}

		virtual Bool OnMouseMoved( const InputMouseMove& evt ) override
		{
			GizmoTransformHandlerPtr transformHandler = m_transformHandler.Lock();
			if ( !transformHandler )
			{
				return false;
			}

			Vector4 worldSpaceStart = m_worldTransform.GetPosition();
			Vector4 worldSpaceEnd = worldSpaceStart + m_rotationDirection;
			worldSpaceStart.W = 1.0f;
			worldSpaceEnd.W = 1.0f;

			// convert to screen space (including perspective)
			const Matrix& worldToScreen = evt.m_viewport.m_camera.GetWorldToScreen();
			Vector4 screenSpaceStart = worldToScreen.TransformPoint( worldSpaceStart );
			Vector4 screenSpaceEnd = worldToScreen.TransformPoint( worldSpaceEnd );
			screenSpaceStart /= screenSpaceStart.W;
			screenSpaceEnd /= screenSpaceEnd.W;

			Vector2 screenSpaceRotationDirection = screenSpaceEnd - screenSpaceStart;
			screenSpaceRotationDirection.Normalize(); // make sure the rotation speed is always the same

			// update mouse cursor (including precision mode)
			{
				Float scalingFactor = Config::cvRotateGizmoBaseRotationSpeed.Get();
				if ( m_precissionMode )
				{
					scalingFactor *= Config::cvRotateGizmoPrecissionMultiplier.Get();
				}

				m_mousePosition.X += scalingFactor * evt.m_rawDx;
				m_mousePosition.Y -= scalingFactor * evt.m_rawDy;
			}

			const Float movement = m_mousePosition.Dot( screenSpaceRotationDirection );

			// Offset
			if ( !m_firstOffsetCaptured )
			{
				m_firstOffsetCaptured = true;
				m_firstOffset = movement;
			}

			Float adjustedMovementDelta = movement - m_firstOffset;

			// apply new rotation
			if ( adjustedMovementDelta != m_totalMovement )
			{
				m_totalMovement = adjustedMovementDelta;

				// preprocess movement
				Float targetRotation = 0.0f;
				if ( transformHandler->OnPreProcessRotation( m_worldTransform, m_worldDir, m_totalMovement, targetRotation ) )
				{
					m_lastTargetRotation = targetRotation;
					m_lastTargetRotationIsValid = true;

					// preview the rotation
					const Transform transform( Quaternion( m_worldDir.Normalized(), DEG2RAD( m_lastTargetRotation ) ) );
					transformHandler->OnApplyMovement( m_worldTransform, transform, GizmoTransformHandler::ActionMode::Preview, m_mode );

					// propagate rotation
					if ( m_externalRotationAngle )
						*m_externalRotationAngle = m_lastTargetRotation;
				}
			}

			// keep rendered
			return true;
		}

	private:
		GizmoTransformHandlerWeakPtr m_transformHandler;
		GizmoTransformMode m_mode;
		Transform m_worldTransform;
		Vector3 m_worldDir;
		Vector3 m_rotationDirection;
		Vector2 m_mousePosition;
		Float m_totalMovement;
		Float* m_externalRotationAngle; // HACK
		Float m_lastTargetRotation;
		Float m_firstOffset;
		Bool m_firstOffsetCaptured;
		Bool m_lastTargetRotationIsValid;
		Bool m_precissionMode;
	};

	class GizmoRotateAxis : public IGizmo
	{
	public:
		GizmoRotateAxis( const GizmoTransformHandlerPtr& transformHandler, const Vector3& axis, const Color& lineColor )
			: IGizmo()
			, m_transformHandler( transformHandler )
			, m_rotationAxis( axis )
			, m_quadrantCode( -1 )
			, m_lastRenderScale( 0.0f )
			, m_lineColor( lineColor )
			, m_rotationAngle( 0.0f )
			, m_uvMapping{ red::PoolBackend() }
			, m_points{ red::PoolBackend() }
			, m_tempPoints{ red::PoolBackend() }
		{
			m_innerRadius = Config::cvRotateGizmoInnerRadius.Get();
			m_outerRadius = Config::cvRotateGizmoOuterRadius.Get();
			m_fillAlpha = Config::cvRotateGizmoFillAlpha.Get();
			m_markerSize = Config::cvRotateGizmoMarkerSize.Get();
			m_markerColor = Config::cvRotateGizmoMarkerColor.Get();
			m_gridLinesColor = Config::cvRotateGizmoGridLinesColor.Get();
		}

		virtual Transform CalcPlacement() const
		{
			return m_transformHandler->GetGizmoPlacement();
		}

		virtual Bool OnCheckCollision( Int32 x, Int32 y, const EditModeViewport& vpt, Priority& outPriority )
		{
			// Project into world space (in-place)
			PrepareWorldSpacePoints( vpt );
			vpt.WorldToScreen( m_tempPoints.TypedData(), m_tempPoints.TypedData(), m_tempPoints.Size() );

			// Convert mouse click to screen space coords
			const Vector2 mousePosVector( (Float)x, (Float)y );

			// Check distance to each line in screen space
			const Float minDist = 10.0f;
			const Uint32 numPoints = m_tempPoints.Size();
			for ( Uint32 i = 0; i < numPoints; i += 2 )
			{
				// Check distance
				const Float dist = CalcDistanceToSegment( mousePosVector, m_tempPoints[i].AsVector2(), m_tempPoints[i+1].AsVector2() );
				if ( dist < minDist )
				{
					outPriority = Priority_Low;
					return true;
				}
			}

			// no collision
			outPriority = Priority_Low;
			return false;
		}

		virtual void OnRender( const CRenderCamera& camera, rend::IDebugDrawer& dd, ERenderMode mode ) override
		{
			rend::HighPriorityFragmentBuilder builder( dd );
			builder.EnableOverlay( true );
			builder.EnableTransparency( true );

			// Draw the widget if not dragging
			if ( mode == ERenderMode::Normal || mode == ERenderMode::Selected )
			{
				// Update quadrant code for lines				
				UpdateQuadrantCode( camera );

				// Transform rendering lines to frame space				
				PrepareWorldSpacePoints( EditModeViewport( camera, dd ) );

				// Draw color
				const Color drawColor = (mode == ERenderMode::Selected) ? Color::YELLOW() : m_lineColor;

				// Generate solid drag
				{
					Color polyColor( drawColor.ToVector() * 0.4f );
					polyColor.A = 150;
					builder.SetColor( polyColor );

					for ( Uint32 i=0; i<m_tempPoints.Size(); i += 4 )
					{
						Vector3 points[4];
						points[0] = m_tempPoints[i+0];
						points[1] = m_tempPoints[i+1];
						points[2] = m_tempPoints[i+3];
						points[3] = m_tempPoints[i+2];
						builder.AddSolidPolygon( points, 4, true );
						builder.AddSolidPolygon( points, 4, false );
					}
				}

				// Generate drag border
				{
					// Generate fragment
					builder.SetColor( drawColor );

					for ( Uint32 i=0; i<m_tempPoints.Size(); i += 2 )
						builder.AddLine( m_tempPoints[i+0], m_tempPoints[i+1] );
				}

				// Generate inner grid
				{
					DynArray< DebugVertexUV > vertices{ red::PoolBackend() };

					Color polyColor( drawColor.ToVector() * 0.3f );
					polyColor.A = 16;

					// Start with center
					const Vector3 center = CalcPlacement().GetPosition3();
					vertices.EmplaceBack( center, polyColor, 0.0f, 0.0f );

					for ( Uint32 i=0; i<STEPS; ++i )
					{
						const Vector2& uv = m_uvMapping[i];
						const Vector3& pos = m_tempPoints[4*i+1];
						vertices.EmplaceBack( pos, polyColor, uv.X, uv.Y );

						if ( i == (STEPS-1) )
						{
							const Vector2& uv = m_uvMapping[i+1];
							const Vector3& pos = m_tempPoints[4*i+0];
							vertices.EmplaceBack( pos, polyColor, uv.X, uv.Y );
						}
					}

					// Flush polygon to rendering
					builder.SetColor( polyColor );
					builder.AddSolidPolygon( vertices.TypedData(), vertices.Size(), true );
					builder.AddSolidPolygon( vertices.TypedData(), vertices.Size(), false );
				}
			}

			// We are dragging
			else if ( mode == ERenderMode::Active )
			{
				// Calculate UV axes
				Vector3 localU, localV;
				CalcUVAxes( m_rotationAxis, localU, localV, 0 );

				// Get drawing scale
				const Transform pivotTransform = m_capturedPlacement;
				const Vector3 origin = pivotTransform.GetPosition();
				const Vector3 u = pivotTransform.TransformVector( localU ).Normalized();
				const Vector3 v = pivotTransform.TransformVector( localV ).Normalized();

				// Draw circle
				Float angleDelta = DEG2RAD( m_rotationAngle );
				while ( angleDelta < -RED_PI ) angleDelta += 2.0f * RED_PI;
				while ( angleDelta >= RED_PI ) angleDelta -= 2.0f * RED_PI;

				RenderRingFill( builder, angleDelta, origin, u, v );
				Vector3 marker0Position = RenderRing( builder, angleDelta, origin, u, v );
				RenderRingGridLines( builder, angleDelta, origin, u, v );
				RenderLabel( builder, marker0Position );
			}
			else
			{
				// Do not draw other rotation gizmos that are not the active ones
			}
		}

		virtual red::UniquePtr< IPreviewInputHandler > OnActivate( const GizmoTransformMode transformMode ) override
		{
			const auto placement = CalcPlacement();
			const Vector3 worldDir = placement.TransformVector( m_rotationAxis ).Normalized();

			m_capturedPlacement = placement;
			m_rotationAngle = 0.0f; // reset

			return red::CreateUniquePtr< GizmoRotateAxisInputHandler >( m_transformHandler, worldDir, GetRotationDirection(), placement, &m_rotationAngle, transformMode );
		}


		virtual void GetDepth( const EditModeViewport& vpt, Float& outMin, Float& outMax ) const override
		{
			outMin = std::numeric_limits< Float >::max();
			outMax = std::numeric_limits< Float >::min();

			for ( Uint32 i = 0; i < m_tempPoints.Size(); ++i )
			{
				if ( m_tempPoints[i].Z < outMin )
				{
					outMin = m_tempPoints[i].Z;
				}

				if ( m_tempPoints[i].Z > outMin )
				{
					outMax = m_tempPoints[i].Z;
				}
			}
		}

		RED_INLINE virtual Bool SupportsPivotMovement() const override { return true; }

	private:
		static const Uint32	STEPS = 64;
		static const Uint32	RING_SEGMENTS = STEPS * 4;
		static const Float TAU;

		GizmoTransformHandlerPtr						m_transformHandler;
		Color											m_lineColor;
		Vector3											m_rotationAxis;
		DynArray< Vector2 >								m_uvMapping;
		DynArray< Vector3 >								m_points;
		Int8											m_quadrantCode;

		Float											m_lastRenderScale;
		DynArray< Vector3 >								m_tempPoints;

		Transform										m_capturedPlacement;
		Float											m_rotationAngle; // PASS TO INPUT HANDLER (HACK)

		// Cached config values
		Float											m_innerRadius;
		Float											m_outerRadius;
		Float											m_fillAlpha;
		Float											m_markerSize;
		Color											m_markerColor;
		Color											m_gridLinesColor;

		Vector3 RenderMarker( rend::FragmentBuilder& builder, Float angle, const Vector3& origin, const Vector3& u, const Vector3& v, Bool fill ) const
		{
			const Float VerticalScale = 1.0f + m_markerSize * 2.0f;
			const Float scale = m_lastRenderScale;

			const Vector3 p0 = origin + CalculateRadius( angle, u, v ) * m_lastRenderScale;
			const Vector3 p1 = origin + CalculateRadius( angle - m_markerSize, u, v ) * m_lastRenderScale * VerticalScale;
			const Vector3 p2 = origin + CalculateRadius( angle + m_markerSize, u, v ) * m_lastRenderScale * VerticalScale;

			const Vector3 retval = p1;

			builder.SetColor( m_markerColor );
			builder.AddLine( p0, p1 );
			builder.AddLine( p1, p2 );
			builder.AddLine( p2, p0 );

			if( fill )
			{
				DebugVertex vertices[ 3 ];
				vertices[ 0 ].Set( p0, Color::YELLOW().ToVector() * m_fillAlpha );
				vertices[ 1 ].Set( p1, Color::YELLOW().ToVector() * m_fillAlpha );
				vertices[ 2 ].Set( p2, Color::YELLOW().ToVector() * m_fillAlpha );
				builder.AddSolidPolygon( vertices, 3, true );
				builder.AddSolidPolygon( vertices, 3, false );
			}

			return retval;
		}

		Vector3 CalculateRadius( Float angle, const Vector3& u, const Vector3& v ) const
		{
			// Calculate sin/cos values
			const Float s = MSin( angle );
			const Float c = MCos( angle );

			// Calculate position
			return ( u * c ) + ( v * s );
		}

		void CalculatePointsOnRing( Float angle, const Vector3& origin, const Vector3& u, const Vector3& v, Vector3& outerPoint, Vector3& innerPoint ) const
		{
			Vector3 radius = CalculateRadius( angle, u, v );

			innerPoint = origin + radius * m_innerRadius * m_lastRenderScale;
			outerPoint = origin + radius * m_outerRadius * m_lastRenderScale;
		}

		void RenderRingSegment( rend::FragmentBuilder& builder, const Vector3& a1, const Vector3& b1, const Vector3& a2, const Vector3& b2 ) const
		{
			Vector3 points[4] = { a2, a1, b1, b2 };

            builder.SetColor( Color { m_lineColor.ToVector() * m_fillAlpha } );
			builder.AddSolidPolygon( points, RED_ARRAY_COUNT_U32( points ), true );
			builder.AddSolidPolygon( points, RED_ARRAY_COUNT_U32( points ), false );
		}

		Vector3 RenderRing( rend::FragmentBuilder& builder, Float angleDelta, const Vector3& origin, const Vector3& u, const Vector3& v ) const
		{
			Vector3 oldA, oldB, marker0Position;
			for ( Uint32 i = 0; i <= RING_SEGMENTS; ++i )
			{
				// Calculate angle placement
				const Float angle = TAU * (Float)i / (Float)RING_SEGMENTS;

				Vector3 a, b;
				CalculatePointsOnRing( angle, origin, u, v, a, b );

				if ( i == 0 )
				{
					// Solid marker to denote "original orientation" (0 degrees)
					RenderMarker( builder, angle, origin, u, v, true );

					// Hollow marker to denote current rotation (offset from 0)
					marker0Position = RenderMarker( builder, angleDelta + angle, origin, u, v, false );
				}

				// Generate segments
				if ( i )
				{
					// Outer lines
					builder.SetColor( m_lineColor );
					builder.AddLine( oldB, b );

					// Inner lines
					builder.SetColor( m_lineColor );
					builder.AddLine( oldA, a );
				}

				// Remember for next run
				oldA = a;
				oldB = b;
			}

			return marker0Position;
		}

		void RenderRingFill( rend::FragmentBuilder& builder, Float angleDelta, const Vector3& origin, const Vector3& u, const Vector3& v ) const
		{
			// Inner ring
			Float startAngle = ( angleDelta < 0.0f )? angleDelta + TAU : 0.0f;
			Float endAngle = ( angleDelta < 0.0f )? 0.0f : angleDelta;

			Uint32 numInnerRingSteps = (Uint32)( ( math::Abs( angleDelta ) / TAU ) * RING_SEGMENTS );

			Vector3 oldA, oldB;
			for( Uint32 i = 0; i < numInnerRingSteps; ++i )
			{
				// Calculate angle placement
				const Float angle = startAngle + ( TAU * (Float)i / (Float)RING_SEGMENTS );

				Vector3 a, b;
				CalculatePointsOnRing( angle, origin, u, v, a, b );

				if ( i )
				{
					RenderRingSegment( builder, oldA, oldB, a, b );
				}

				// Remember for next run
				oldA = a;
				oldB = b;

				if( i == numInnerRingSteps - 1 )
				{
					CalculatePointsOnRing( endAngle, origin, u, v, a, b );
					RenderRingSegment( builder, oldA, oldB, a, b );
				}
			}
		}

		void RenderRingGridLines( rend::FragmentBuilder& builder, Float angleDelta, const Vector3& origin, const Vector3& u, const Vector3& v )
		{
			const Bool isRotationGridEnabled = true;
			const Float rotGridSize = 5.0f;
			if ( isRotationGridEnabled )
			{
				// Calculate number of steps
				const Uint32 numSteps = (Uint32)( 360.0f / rotGridSize );
				const Uint32 longerStep = (Uint32)( 15.0f / rotGridSize );
				for ( Uint32 i = 0; i < numSteps; ++i )
				{
					const Float angle = angleDelta + ( TAU * (Float)i / (Float)numSteps );

					Vector3 radius = CalculateRadius( angle, u, v );

					// Determine line length
					Float lineLength = ( m_outerRadius - m_innerRadius ) * 0.3f;
					if ( !longerStep || ( i % longerStep == 0 ) )
						lineLength *= 2.0f;

					// Add line
					builder.SetColor( m_gridLinesColor );
					builder.AddLine( origin + ( radius * m_outerRadius * m_lastRenderScale ), origin + ( radius * ( m_outerRadius - lineLength ) * m_lastRenderScale ) );
				}
			}
		}

		void RenderLabel( rend::FragmentBuilder& builder, const Vector3& position ) const
		{
			builder.SetColor( Color::WHITE() );
			builder.EnableTextBackground( Color::GRAY() );
			builder.AddWorldTextf( position, 0, 0, "%1.2f", m_rotationAngle );
		}

		void PrepareWorldSpacePoints( const EditModeViewport& vpt )
		{
			// make sure the quadrant mapping is OK
			UpdateQuadrantCode( vpt.m_camera );

			// Get placement matrix
			const auto localToWorld = CalcPlacement();

			// Get render scale
			const auto scale = vpt.CalcScreenSpaceScale( localToWorld.GetPosition3() );
			m_lastRenderScale = scale * 1.3f; // HACK

			// Transform lines
			const Uint32 numPoints = m_points.Size();
			m_tempPoints.Resize( numPoints );
			for ( Uint32 i=0; i<numPoints; ++i )
				m_tempPoints[i] = localToWorld.TransformPoint( m_points[i] * scale );
		}

		void UpdateQuadrantCode( const CRenderCamera& camera )
		{
			Int8 newCode = 0;

			// Calculate code for perspective camera
			if ( camera.IsPerspective() )
			{
				// Calculate pivot matrix
				const Transform pivot = CalcPlacement();
				const WorldPosition origin = WorldPosition( pivot.GetPosition3() );
				const Vector3 delta = ( camera.GetPosition() - origin );

				// Update quadrant code
				if ( delta.Dot( pivot.TransformVector( Vector3::EX() ) ) < 0.0f ) newCode |= 1;
				if ( delta.Dot( pivot.TransformVector( Vector3::EY() ) ) < 0.0f ) newCode |= 2;
				if ( delta.Dot( pivot.TransformVector( Vector3::EZ() ) ) < 0.0f ) newCode |= 4;
			}

			// If quadrant code changed (means we moved around the gizmo to new area) we need to rebuild the gizmo lines
			if ( newCode != m_quadrantCode )
			{
				m_quadrantCode = newCode;
				BuildLines();
			}
		}

		static Int8 LargestAbsoluteAxis( const Vector3& axis )
		{
			const Float x = fabs(axis.X);
			const Float y = fabs(axis.Y);
			const Float z = fabs(axis.Z);
			
			if ( (x>y) && (x>z) )
				return 0;
			else if ( (y>x) && (y>z) )
				return 1;
			else
				return 2;
		}

		static void CalcUVAxes( const Vector3& axis, Vector3& u, Vector3& v, Int8 quadrantCode, Bool* outIsMirrored = nullptr )
		{
			const auto largestAxis = LargestAbsoluteAxis( axis );
			Bool isMirrored = false;

			if ( largestAxis == 0 )
			{
				u = ( quadrantCode & 2 ) ? -Vector3::EY() : Vector3::EY();
				v = ( quadrantCode & 4 ) ? -Vector3::EZ() : Vector3::EZ();

				// If one (and only one) bit of quadrant code is set it means that
				// the UV vectors are mirrored. If both bits are set, the vectors are
				// "normal" again.
				isMirrored = ( ( quadrantCode & 2 ) != 0 ) != ( ( quadrantCode & 4 ) != 0 );
			}
			else if ( largestAxis == 1 )
			{
				v = ( quadrantCode & 1 ) ? -Vector3::EX() : Vector3::EX();
				u = ( quadrantCode & 4 ) ? -Vector3::EZ() : Vector3::EZ();
				isMirrored = ( ( quadrantCode & 1 ) != 0 ) != ( ( quadrantCode & 4 ) != 0 );
			}
			else
			{
				u = ( quadrantCode & 1 ) ? -Vector3::EX() : Vector3::EX();
				v = ( quadrantCode & 2 ) ? -Vector3::EY() : Vector3::EY();
				isMirrored = ( ( quadrantCode & 1 ) != 0 ) != ( ( quadrantCode & 2 ) != 0 );
			}
			
			if ( outIsMirrored )
			{
				*outIsMirrored = isMirrored;
			}
		}

		void BuildLines()
		{
			// Get the axes
			Vector3 u, v;
			CalcUVAxes( m_rotationAxis, u, v, m_quadrantCode );

			// Prepare size
			m_points.Resize( RING_SEGMENTS );

			// Create the UV mapping table
			m_uvMapping.Resize( STEPS+1 );

			// Generate the rotation circle
			const Float outerSize = 1.0f;
			const Float innerSize = 0.9f;
			Vector3 oldA, oldB;
			Uint32 runningIndex = 0;
			for ( Uint32 i = 0; i <= STEPS; ++i )
			{
				const Float angle = RED_PI_HALF * (Float)i / (Float)STEPS;
				const Float c = MCos( angle );
				const Float s = MSin( angle );

				// Remember UV mapping angles
				m_uvMapping[i].Set( c, s );

				// Calculate vertices
				const Vector3 r = (u * c) + (v * s);
				const Vector3 a = r * innerSize;
				const Vector3 b = r * outerSize;

				// Create segments
				if ( i > 0 )
				{
					m_points[ runningIndex + 0 ] = a;
					m_points[ runningIndex + 1 ] = oldA;
					m_points[ runningIndex + 2 ] = b;
					m_points[ runningIndex + 3 ] = oldB;
					runningIndex += 4;
				}

				// Remember for next segment
				oldA = a;
				oldB = b;
			}
		}

		// Calculate world-space rotation direction
		// It is a tangent vector at 45 degrees of the visible gizmo
		// pointing at rotation direction.
		Vector3 GetRotationDirection() const
		{
			// Get the axes
			Vector3 u, v;
			Bool isMirrored = false;

			CalcUVAxes( m_rotationAxis, u, v, m_quadrantCode, &isMirrored );

			if ( isMirrored )
			{
				std::swap( u, v );
			}

			// average end points (this gives quite good 45-degree angle approximation)
			const Vector3 localSpaceDirection = ( v - u ).Normalized();

			return CalcPlacement().TransformVector( localSpaceDirection );
		}
	};

	//----

	GizmoGroupRotate::GizmoGroupRotate( GizmoManager* manager, const GizmoTransformHandlerPtr& transformHandler, const Uint8 axisMask /*= 7*/ )
		: m_manager( manager )
		, m_gizmos{ red::PoolBackend() }
	{
		// create the axis translations
		if ( axisMask & 1 )
			m_gizmos.PushBack( red::CreateUniquePtr< GizmoRotateAxis >( transformHandler, Vector3::EX(), Color::RED() ) );
		if ( axisMask & 2 )
			m_gizmos.PushBack( red::CreateUniquePtr< GizmoRotateAxis >( transformHandler, Vector3::EY(), Color::GREEN() ) );
		if ( axisMask & 4 )
			m_gizmos.PushBack( red::CreateUniquePtr< GizmoRotateAxis >( transformHandler, Vector3::EZ(), Color::BLUE() ) );

		// register the gizmos
		for ( const auto& ptr : m_gizmos )
		{
			m_manager->Register( ptr.Get() );
		}
	}

	GizmoGroupRotate::~GizmoGroupRotate()
	{
		// unregister the gizmos
		for ( const auto& ptr : m_gizmos )
		{
			m_manager->Unregister( ptr.Get() );
		}
	}

	const Float GizmoRotateAxis::TAU = RED_PI * 2;
}
