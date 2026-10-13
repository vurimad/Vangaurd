/**
* Copyright (c) 2013 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"
#include "previewInput.h"
#include "previewEditMode.h"
#include "selectionHandler.h"
#include "selectionScreenAreaInputHandler.h"
#include "../../../common/engine/include/renderFrame.h"
#include "../../common/renderDraw/include/fragmentBuilder.h"

namespace tools
{
	SelectionScreenAreaInputHandler::SelectionScreenAreaInputHandler( SelectionHandler& target, Int32 startX, Int32 startY, const SelectionContext& selectionContext )
		: m_target( target )
		, m_startX( startX )
		, m_startY( startY )
		, m_endX( startX )
		, m_endY( startY )
		, m_selectionContext( selectionContext )
	{
	}

	void SelectionScreenAreaInputHandler::OnRender( const CRenderCamera& camera, rend::IDebugDrawer& dd ) const
	{
		const Rect rect = CalcSelectionRect();
		if ( !rect.IsEmpty() )
		{
			rend::FragmentBuilder builder( dd );
			builder.EnableTransparency( true );

			const Color color = m_selectionContext.m_multlayer ? Color( 128, 0, 255, 64 ) : Color( 0, 0, 255, 64 );

			builder.SetColor( color );
			builder.AddFlatRectWH( rect.m_left, rect.m_top, rect.Width(), rect.Height() );

			builder.SetColor( Color::WHITE( 128 ) );
			builder.AddFlatFrameWH( rect.m_left, rect.m_top, rect.Width(), rect.Height() );
		}
	}

	Bool SelectionScreenAreaInputHandler::OnMouseUp( const InputMouseButton& evt )
	{
		if ( evt.m_button == LMB )
		{
			const Rect rect = CalcSelectionRect();
			if ( !rect.IsEmpty() )
			{
				SelectionContext context = m_selectionContext;
				context.m_selectionRect = rect;

				m_target.PerformSelection( context );
			}

			// we are done
			return false;
		}
		else
		{
			// keep us alive
			return true;
		}
	}

	Bool SelectionScreenAreaInputHandler::OnMouseMoved( const InputMouseMove& evt )
	{
		// update tracking position
		m_endX = evt.m_x;
		m_endY = evt.m_y;

		// keep us active
		return true;
	}

	const Rect SelectionScreenAreaInputHandler::CalcSelectionRect() const
	{
		Rect ret;
		ret.m_left = math::Min< Int32 >( m_startX, m_endX );
		ret.m_top = math::Min< Int32 >( m_startY, m_endY );
		ret.m_right = math::Max< Int32 >( m_startX, m_endX );
		ret.m_bottom = math::Max< Int32 >( m_startY, m_endY );
		return ret;
	}

} // tools
