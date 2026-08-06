/**
* Copyright (c) 2015 CD Projekt Red. All Rights Reserved.
*/
#include "build.h"
#include "mathCommon.h"
#include "mathCylinder.h"
#include "shapeRenderer.h"

void Cylinder::RenderSolid(red::IShapeRenderer& renderer) const
{
	const Uint16 numPoints = 30;
	const Uint16 totalVertices = ( numPoints + 1 ) * 2 + 2;

	Vector4 vertices[ totalVertices ];

	GenerateVertices(numPoints, vertices);

	Vector4 center2 = math::Cylinder::GetPosition2();
	Vector4 center1 = math::Cylinder::GetPosition(); 

	// Generate triangles
	for ( Uint16 i = 0; i < numPoints; ++i )
	{
		Uint16 bottomLeft	=  i * 2;
		Uint16 topLeft		=  i * 2 + 1;
		Uint16 bottomRight	= ( i * 2 + 2) % totalVertices;
		Uint16 topRight		= ( i * 2 + 3) % totalVertices;

		// Sides
		renderer.AddTriangle( 
			vertices[ bottomLeft	], 
			vertices[ bottomRight	], 
			vertices[ topLeft		] );

		renderer.AddTriangle( 
			vertices[ topRight		], 
			vertices[ topLeft		], 
			vertices[ bottomRight	] );

		// Bottoms
		renderer.AddTriangle( 
			vertices[ bottomLeft	], 
			center1, 
			vertices[ bottomRight	] );

		renderer.AddTriangle( 
			vertices[ topLeft		],
			vertices[ topRight		],
			center2 
			);
	}
}

void Cylinder::RenderWireframe(red::IShapeRenderer& renderer) const
{
	const Uint16 numPoints = 30;
	const Uint16 totalVertices = ( numPoints + 1 ) * 2 + 2;

	Vector4 vertices[ totalVertices ];

	GenerateVertices(numPoints, vertices);

	Vector4 center2 = math::Cylinder::GetPosition2();
	Vector4 center1 = math::Cylinder::GetPosition(); 

	for ( Uint16 i = 0; i < numPoints; ++i )
	{
		// Side lines														  ____
		renderer.AddLine( vertices[ i * 2 + 2 ], vertices[ i * 2	 ] ); // |   /
		renderer.AddLine( vertices[ i * 2	  ], vertices[ i * 2 + 1 ] ); // |  /
		renderer.AddLine( vertices[ i * 2 + 1 ], vertices[ i * 2 + 2 ] ); // | / 
		renderer.AddLine( vertices[ i * 2 + 1 ], vertices[ i * 2 + 3 ] ); // |/__ 

		// Bottom lines
		renderer.AddLine( vertices[ i * 2	  ], vertices[ totalVertices - 2 ] );
		renderer.AddLine( vertices[ i * 2 + 1 ], vertices[ totalVertices - 1 ] );
	}
}

void Cylinder::GenerateVertices(const Uint16 numPoints, Vector4* vertices) const
{
	Vector4 center2 = math::Cylinder::GetPosition2();
	Vector4 center1 = math::Cylinder::GetPosition(); 
	Vector4 norm = math::Cylinder::GetNormal();
	Float radius = math::Cylinder::GetRadius();

	Plane p( norm, Vector4::ZERO_3D_POINT() );
	Vector4 v( 1, 1, 1 );
	v = p.Project( v );
	if ( v.SquareMag3() <= std::numeric_limits<Float>::epsilon() )
	{
		v = Vector4( 1, -2, 1 );
		v = p.Project( v );
	}
	v.Normalize3();

	Float qSin = MSin( RED_PI / (Float)numPoints );
	Float qCos = MCos( RED_PI / (Float)numPoints );
	math::Quaternion quat( norm.X * qSin, norm.Y * qSin, norm.Z * qSin, qCos );
	Matrix m = quat.ToMatrix();
	m.Transpose();

	Uint32 index = 0;
	for ( Uint32 i = 0; i <= numPoints; ++i )
	{
		vertices[ index++ ] = center1 + v * radius;
		vertices[ index++ ] = center2 + v * radius;
		v = m.TransformVector( v );
	}

	vertices[ index++ ] =  center1;
	vertices[ index++ ] =  center2;
}
