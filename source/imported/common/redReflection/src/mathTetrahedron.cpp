/**
* Copyright (c) 2015 CD Projekt Red. All Rights Reserved.
*/
#include "build.h"
#include "mathCommon.h"
#include "mathTetrahedron.h"
#include "shapeRenderer.h"

void Tetrahedron::RenderSolid(red::IShapeRenderer& renderer) const
{
	const Uint16 indices[ 12 ] = {
		0, 1, 2,
		0, 2, 3,
		0, 3, 1,
		1, 3, 2
	};

	for( Uint32 i = 0; i < 12; i+=3 )
	{
		renderer.AddTriangle( m_points[ indices[ i + 2 ] ], m_points[ indices[ i + 1 ] ], m_points[ indices[ i  ] ] );
	}
}

void Tetrahedron::RenderWireframe(red::IShapeRenderer& renderer) const
{
	renderer.AddLine( m_points[ 0 ], m_points[ 1 ] );
	renderer.AddLine( m_points[ 0 ], m_points[ 2 ] );
	renderer.AddLine( m_points[ 0 ], m_points[ 3 ] );
	renderer.AddLine( m_points[ 2 ], m_points[ 1 ] );
	renderer.AddLine( m_points[ 3 ], m_points[ 1 ] );
	renderer.AddLine( m_points[ 3 ], m_points[ 2 ] );
}
