/**
* Copyright (c) 2015 CD Projekt Red. All Rights Reserved.
*/
#include "build.h"
#include "mathCommon.h"
#include "mathOrientedBox.h"
#include "shapeRenderer.h"

void GenerateVertices(Vector4 * p, Vector4 position, Vector4 e1, Vector4 e2, Vector4 e3)
{
	p[0] = position;	
	p[1] = position + e1;
	p[2] = position + e2;
	p[3] = position + e1 + e2;
	p[4] = position + e3;
	p[5] = position + e3 + e1;
	p[6] = position + e3 + e2;
	p[7] = position + e3 + e1 + e2;
}

void OrientedBox::RenderSolid(red::IShapeRenderer& renderer) const
{
	Vector4 e1 = math::OrientedBox::GetEdge1();
	Vector4 e2 = math::OrientedBox::GetEdge2();
	Vector4 e3 = math::OrientedBox::GetEdge3();

	Vector4 vertices[8];

	GenerateVertices(vertices, m_position, e1, e2, e3);

	// Create the indices
	const Uint16 indices[ 36 ] = {
		0, 1, 2, 1, 3, 2,				// top
		5, 4, 6, 5, 6, 7,				// bottom
		0, 2, 4, 4, 2, 6,				// left
		1, 5, 3, 3, 5, 7,				// right
		2, 3, 6, 3, 7, 6,				// front
		0, 4, 1, 1, 4, 5				// back
	};

	for( Uint32 i = 0; i < 36; i+=3 )
	{
		renderer.AddTriangle( vertices[ indices[ i ] ], vertices[ indices[ i + 1 ] ], vertices[ indices[ i + 2 ] ] );
	}
}

void OrientedBox::RenderWireframe(red::IShapeRenderer& renderer) const
{
	Vector4 e1 = math::OrientedBox::GetEdge1();
	Vector4 e2 = math::OrientedBox::GetEdge2();
	Vector4 e3 = math::OrientedBox::GetEdge3();

	Vector4 vertices[8];

	GenerateVertices(vertices, m_position, e1, e2, e3);

	renderer.AddLine( vertices[0], vertices[1] );
	renderer.AddLine( vertices[0], vertices[2] );
	renderer.AddLine( vertices[1], vertices[3] );
	renderer.AddLine( vertices[2], vertices[3] );

	renderer.AddLine( vertices[4], vertices[5] );
	renderer.AddLine( vertices[4], vertices[6] );
	renderer.AddLine( vertices[5], vertices[7] );
	renderer.AddLine( vertices[6], vertices[7] );

	renderer.AddLine( vertices[0], vertices[4] );
	renderer.AddLine( vertices[1], vertices[5] );
	renderer.AddLine( vertices[2], vertices[6] );
	renderer.AddLine( vertices[3], vertices[7] );
}
