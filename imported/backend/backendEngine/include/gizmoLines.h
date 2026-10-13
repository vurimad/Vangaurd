/**
* Copyright (c) 2013 CD Projekt Red. All Rights Reserved.
*/
#pragma once

#include "gizmo.h"

namespace tools
{
	/// Gizmo that is rendered using lines
	/// It automatically handles the rendering and the hit test
	/// It requires the placement matrix to be calculated
	class IGizmoLines : public IGizmo
	{
	public:
		IGizmoLines( const Color& lineColor );
		virtual ~IGizmoLines();

		/// Implemented part of IGizmo interface
		virtual Bool OnCheckCollision( Int32 x, Int32 y, const EditModeViewport& vpt, Priority& outPriority ) override;
		virtual void OnRender( const CRenderCamera& camera, rend::IDebugDrawer& dd, ERenderMode mode ) override;

		/// Calculate the placement for the gizmo
		virtual Transform CalcPlacement() const = 0;

		virtual void GetDepth( const EditModeViewport& vpt, Float& outMin, Float& outMax ) const override;

	protected:
		// Add line segment to widget, note: points are in local space of the placement matrix
		void AddLine( const Vector3& start, const Vector3& end, Priority priority = Priority_Low );

		const Vector3* PrepareCollisionLines( const EditModeViewport& vpt );

		RED_INLINE const red::DynArray< Vector3 > GetPoints() const { return m_points; }
		RED_INLINE const Color& GetColor() const { return m_lineColor; }

	private:
		red::DynArray< Vector3 >			m_points;				// points to render (always transformed by placement matrix)
		red::DynArray< Priority >			m_priorities;			// priority for each line
		Color								m_lineColor;			// color to render the points with

		Float								m_lastRenderScale;		// cached rendering scale
		red::DynArray< Vector3 >			m_tempPoints;			// prepared memory for the transformed points
		red::DynArray< DebugVertex >		m_renderPoints;			// prepared memory for the rendering points

		const DebugVertex* PrepareRenderLines( const EditModeViewport& vpt, ERenderMode mode );
	};

} // tools
