/**
* Copyright (c) 2013 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"
#include "previewPanelUtils.h"
#include "../../../common/renderData/include/renderVertices.h"
#include "../../../common/engine/include/renderFrame.h"
#include "../../../common/renderDraw/include/fragmentBuilder.h"

using red::DynArray;

namespace tools
{
	// for now the filters function accepts just one argument CRenderFrame - maybe it's worth to change it so that it accepts also position? 
	// Let's wait for next filters to check of it's needed
	void RenderGrid( rend::IDebugDrawer& dd )
	{
		Render3DGrid( dd, Vector3::ZEROS() );
	}

	void Render3DGrid( rend::IDebugDrawer& dd, const Vector3& center, const Color& gridColor/*= Color(120, 120, 120)*/, const Color axisColor/*= Color(120, 180, 180)*/ )
	{
		DynArray< DebugVertex > vertices{ red::PoolBackend() };
		vertices.Reserve( 10*10*2*2*3 );

		// Grid size
		const Int32 gridSize = 10;
		const Float gridSpacing = 10.0f;

		// Grid passes
		Float currentGridSpacing = gridSpacing;
		for ( Uint32 level=0; level<3; ++level )
		{
			// Calculate grid extents
			const Float extents = gridSize * currentGridSpacing;

			// Draw the grid
			for ( Int32 d=-gridSize; d<=gridSize; ++d )
			{
				if ( d != 0 )
				{
					const Float pos = d * currentGridSpacing;
					const Vector3 startX( -extents, pos, 0.0f );
					const Vector3 endX( extents, pos, 0.0f );
					const Vector3 startY( pos, -extents, 0.0f );
					const Vector3 endY( pos, extents, 0.0f );

					vertices.PushBack( DebugVertex( startX + center, gridColor ) );
					vertices.PushBack( DebugVertex( endX + center, gridColor ) );
					vertices.PushBack( DebugVertex( startY + center, gridColor ) );
					vertices.PushBack( DebugVertex( endY + center, gridColor ) );
				}
			}

			// Make the grid smaller on the next pass
			currentGridSpacing = currentGridSpacing / 10.0f;
		}

		// Add root lines
		{
			const Float pos = 0.0f;
			const Float extents = gridSize * gridSpacing;
			const Vector3 startX( -extents, pos, 0.0f );
			const Vector3 endX( extents, pos, 0.0f );
			const Vector3 startY( pos, -extents, 0.0f );
			const Vector3 endY( pos, extents, 0.0f );

			vertices.PushBack( DebugVertex( startX + center, axisColor ) );
			vertices.PushBack( DebugVertex( endX + center, axisColor ) );
			vertices.PushBack( DebugVertex( startY + center, axisColor ) );
			vertices.PushBack( DebugVertex( endY + center, axisColor ) );
		}

		// flush
		rend::FragmentBuilder builder( dd );
		builder.AddLines( vertices.TypedData(), vertices.Size() );
	}
}
