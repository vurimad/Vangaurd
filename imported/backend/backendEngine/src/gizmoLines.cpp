/**
* Copyright (c) 2013 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"
#include "gizmoLines.h"
#include "previewEditMode.h"
#include "../../../common/renderData/include/renderVertices.h"
#include "../../../common/engine/include/renderFrame.h"
#include "../../common/renderDraw/include/fragmentBuilder.h"

namespace Config
{
	TConfigVar< Int32 > cvGizmoLinesCollisionDistance( "Editor/Gizmo/Lines", "CollisionOffset", 10, 5, 30, eConsoleVarFlag_Save );
	TConfigVar< Int32 > cvGizmoLinesSize( "Editor/Gizmo/Lines", "Size", 2, 1, 5, eConsoleVarFlag_Save );
}

static const Float FAT_LINE_WIDTH = 0.0035f;

namespace tools
{
	IGizmoLines::IGizmoLines( const Color& lineColor )
		: m_lineColor( lineColor )
		, m_lastRenderScale( 0.1f )
		, m_points{ red::PoolBackend() }
		, m_priorities{ red::PoolBackend() }
		, m_tempPoints{ red::PoolBackend() }
		, m_renderPoints{ red::PoolBackend() }
	{}

	IGizmoLines::~IGizmoLines()
	{
	}

	Transform IGizmoLines::CalcPlacement() const
	{
		return Transform::IDENTITY();
	}

	void IGizmoLines::GetDepth( const EditModeViewport& vpt, Float& outMin, Float& outMax ) const
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

	void IGizmoLines::AddLine( const Vector3& start, const Vector3& end, Priority priority /*= Priority_Low*/ )
	{
		m_points.PushBack( start );
		m_points.PushBack( end );
		m_priorities.PushBack( priority );
	}

	const Vector3* IGizmoLines::PrepareCollisionLines( const EditModeViewport& vpt )
	{
		// make sure the size of the array is the same
		if ( m_tempPoints.Size() != m_points.Size() )
			m_tempPoints.Resize( m_points.Size() );

		// Get placement matrix
		const auto localToWorld = CalcPlacement();

		// Get render scale
		const auto scale = vpt.CalcScreenSpaceScale( localToWorld.GetPosition3() );
		m_lastRenderScale = scale;

		// Transform lines
		const Uint32 numPoints = m_points.Size();
		for ( Uint32 i=0; i<numPoints; ++i )
		{
			m_tempPoints[i] = localToWorld.TransformPoint( m_points[i] * scale );
		}

		// Project into world space (in-place)
		vpt.WorldToScreen( m_tempPoints.TypedData(), m_tempPoints.TypedData(), m_tempPoints.Size() );
		return m_tempPoints.TypedData();
	}

	const DebugVertex* IGizmoLines::PrepareRenderLines( const EditModeViewport& vpt, ERenderMode mode )
	{
		// make sure the size of the array is the same
		if ( m_renderPoints.Size() != m_points.Size() )
			m_renderPoints.Resize( m_points.Size() );

		// Get placement matrix
		const auto localToWorld = CalcPlacement();

		// Get render scale
		const auto scale = vpt.CalcScreenSpaceScale( localToWorld.GetPosition3() );
		m_lastRenderScale = scale;

		// Transform lines
		const Color color = ( mode == ERenderMode::Normal || mode == ERenderMode::OtherActive ) ? m_lineColor : Color::YELLOW();
		const Vector4 colorLinear = color.ToVectorLinearAccu();
		const Uint32 numPoints = m_points.Size();
		for ( Uint32 i=0; i<numPoints; ++i )
		{
			auto pos = m_points[i] * scale;
			m_renderPoints[i].x = pos.X;
			m_renderPoints[i].y = pos.Y;
			m_renderPoints[i].z = pos.Z;
			m_renderPoints[i].colorLinearSpace = colorLinear;
		}

		// Project into world space (in-place)
		return m_renderPoints.TypedData();
	}


	Bool IGizmoLines::OnCheckCollision( Int32 x, Int32 y, const EditModeViewport& vpt, Priority& outPriority )
	{
		// transform points
		const auto* screenPoints = PrepareCollisionLines( vpt );

		// Convert mouse click to screen space coords
		const Vector2 mousePosVector( (Float)x, (Float)y );

		// Check distance to each line in screen space
		const Float minDist = (Float)Config::cvGizmoLinesCollisionDistance.Get();
		const Uint32 numPoints = m_points.Size();

		Bool collisionFound = false;
		Priority maxPriority = Priority_Low;
		for ( Uint32 i = 0; i < numPoints; i += 2 )
		{
			// Check distance
			const Float dist = CalcDistanceToSegment( mousePosVector, screenPoints[i].AsVector2(), screenPoints[i+1].AsVector2() );
			const Priority priority = m_priorities[i / 2];

			if ( dist < minDist && priority >= maxPriority )
			{
				maxPriority = priority;
				collisionFound = true;
			}
		}

		outPriority = maxPriority;
		return collisionFound;
	}

	void IGizmoLines::OnRender( const CRenderCamera& camera, rend::IDebugDrawer& dd, ERenderMode mode )
	{
		// transform points
		const EditModeViewport vpt( camera, dd );
		const auto* renderPoints = PrepareRenderLines( vpt, mode );
		
		const Transform localToWorld = CalcPlacement();
		Float scale = vpt.CalcScreenSpaceScale( localToWorld.GetPosition3() );
		scale *= (Float)Config::cvGizmoLinesSize.Get();

		// Generate fragment
		rend::HighPriorityFragmentBuilder builder(dd);
		builder.EnableOverlay( true );
		builder.BindTransform( localToWorld.ToMatrix() );
		builder.AddFatLines( renderPoints, m_points.Size(), scale * FAT_LINE_WIDTH );
	}

} // tools
