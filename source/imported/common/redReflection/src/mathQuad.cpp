/**
* Copyright (c) 2015 CD Projekt Red. All Rights Reserved.
*/
#include "build.h"
#include "mathCommon.h"
#include "mathQuad.h"
#include "shapeRenderer.h"

void Quad::RenderSolid(red::IShapeRenderer& renderer) const
{
	renderer.AddTriangle( m_points[ 0 ], m_points[ 1 ], m_points[ 2 ] );
	renderer.AddTriangle( m_points[ 0 ], m_points[ 2 ], m_points[ 3 ] );
}

void Quad::RenderWireframe(red::IShapeRenderer& renderer) const
{
	renderer.AddLine( m_points[0], m_points[1] );
	renderer.AddLine( m_points[1], m_points[2] );
	renderer.AddLine( m_points[2], m_points[3] );
	renderer.AddLine( m_points[3], m_points[0] );
}
