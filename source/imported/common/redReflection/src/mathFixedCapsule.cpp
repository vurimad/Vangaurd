/**
* Copyright (c) 2015 CD Projekt Red. All Rights Reserved.
*/
#include "build.h"
#include "mathCommon.h"
#include "mathFixedCapsule.h"
#include "shapeRenderer.h"

void FixedCapsule::RenderSolid(red::IShapeRenderer& renderer) const
{
	constexpr Uint32 horizontalSideLines = s_horizontalLines / 2;
	constexpr Uint32 verticalSideLines = s_verticalLines / 2;

	constexpr Uint32 size = s_nbGeneratedDebugVertices / 2;
	Vector4 verticesUpper[ size ];
	Vector4 verticesLower[ size ];

	GenerateVertices( verticesUpper, verticesLower);
	renderer.Reserve(s_nbGeneratedDebugVertices);

	Uint32 halfSphereTriangles = ( size - 2 );
	for( Uint32 y = 0; y < horizontalSideLines; ++y)
	{
		Uint32 strideOffset = y * s_verticalLines;
		Uint32 strideOffsetTop = ( y + 1 ) * s_verticalLines;
		for( Uint32 x = 0; x < s_verticalLines; ++x)
		{
			Uint32 leftIndex = x + strideOffset;
			Uint32 rightIndex = (x + 1) % s_verticalLines + strideOffset;
			Uint32 topIndexLeft = x + strideOffsetTop;
			Uint32 topIndexRight = (x + 1) % s_verticalLines + strideOffsetTop;
			Bool topCase = (topIndexLeft > halfSphereTriangles );
			topIndexLeft = topCase ? (size - 1) : topIndexLeft;

			renderer.AddTriangle( verticesUpper[ leftIndex ], verticesUpper[ topIndexLeft ], verticesUpper[ rightIndex ] );
			renderer.AddTriangle( verticesLower[ rightIndex ], verticesLower[ topIndexLeft ], verticesLower[ leftIndex ] );

			if( !topCase )
			{
				renderer.AddTriangle( verticesUpper[ rightIndex ], verticesUpper[ topIndexLeft ], verticesUpper[ topIndexRight ] );
				renderer.AddTriangle( verticesLower[ topIndexRight ], verticesLower[ topIndexLeft ], verticesLower[ rightIndex ] );
			}
		}
	}
	// here?
	for( Uint32 i = 0; i < s_verticalLines; ++i)
	{
		Uint32 leftIndex =  i;
		Uint32 rightIndex = (i + 1);
		rightIndex = rightIndex % s_verticalLines;

		renderer.AddTriangle( verticesUpper[ leftIndex ], verticesLower[ rightIndex ] , verticesLower[ leftIndex ]);
		renderer.AddTriangle( verticesLower[ rightIndex ], verticesUpper[ leftIndex ] , verticesUpper[ rightIndex ]);
	}
}

void FixedCapsule::RenderWireframe(red::IShapeRenderer& renderer) const
{
	constexpr Uint32 horizontalSideLines = s_horizontalLines / 2;
	constexpr Uint32 verticalSideLines = s_verticalLines / 2;

	constexpr Uint32 size = s_nbGeneratedDebugVertices / 2;
	Vector4 verticesUpper[ size ];
	Vector4 verticesLower[ size ];

	GenerateVertices( verticesUpper, verticesLower);
	renderer.Reserve(s_nbGeneratedDebugVertices);

	Uint32 halfSphereTriangles = ( size - 2 );
	for( Uint32 y = 0; y < horizontalSideLines; ++y) 
	{
		Uint32 strideOffset = y * s_verticalLines;
		Uint32 strideOffsetTop = ( y + 1 ) * s_verticalLines;
		for( Uint32 x = 0; x < s_verticalLines; ++x) 
		{
			Uint32 leftIndex = x + strideOffset;
			Uint32 rightIndex = (x + 1) % s_verticalLines + strideOffset;
			Uint32 topIndexLeft = x + strideOffsetTop;
			Bool topCase = (topIndexLeft > halfSphereTriangles );
			topIndexLeft = topCase ? (size - 1) : topIndexLeft;

			renderer.AddLine( verticesUpper[ leftIndex ], verticesUpper[ topIndexLeft ] );
			renderer.AddLine( verticesUpper[ leftIndex ], verticesUpper[ rightIndex ] );
			renderer.AddLine( verticesLower[ leftIndex ], verticesLower[ topIndexLeft ] );
			renderer.AddLine( verticesLower[ leftIndex ], verticesLower[ rightIndex ] );
		}
	}

	const Uint32 sideVertices = s_verticalLines;

	// Prepare midpoints
	Vector4 prevVertices[ sideVertices ];
	Vector4 nextVertices[ sideVertices ];
	Vector4 stepVertices[ sideVertices ];

	Float distanceHor = (verticesUpper[0] - verticesLower[0]).Mag3();
	Float distanceVert = (verticesUpper[0] - verticesUpper[1]).Mag3();

	Float linesCalculated = distanceVert > 0.f ? (distanceHor / distanceVert) : 0.f;
	Float sideLinesf =  Min( Max( linesCalculated, 1.0f ), 15.0f);

	for( Uint32 i = 0; i < s_verticalLines; ++i)
	{
		stepVertices[ i ] = ( verticesUpper[ i ] - verticesLower[ i ] ) / (sideLinesf) ;
	}
	Uint32 sideLines = (Uint32)sideLinesf;
	for( Uint32 j = 0; j < sideLines; ++j )
	{
		if( j == 0 )
		{
			for( Uint32 i = 0; i < s_verticalLines; ++i)
			{
				prevVertices[i] = verticesUpper[ i ];
				nextVertices[i] = prevVertices[i] - stepVertices[i];
			}
		}
		else if( j == sideLines - 1 )
		{
			for( Uint32 i = 0; i < s_verticalLines; ++i)
			{
				nextVertices[i] = verticesLower[ i ];
			}
		}
		else
		{
			for( Uint32 i = 0; i < s_verticalLines; ++i)
			{
				nextVertices[i] = prevVertices[ i ] - stepVertices[i];;
			}
		}

		for( Uint32 i = 0; i < s_verticalLines; ++i)
		{
			Uint32 leftIndex = i;
			Uint32 rightIndex = (i + 1) % s_verticalLines;

			renderer.AddLine( prevVertices[ leftIndex ], prevVertices[ rightIndex ] );
			renderer.AddLine(  nextVertices[ rightIndex ], nextVertices[ leftIndex ] );
			renderer.AddLine( prevVertices[ leftIndex ], nextVertices[ leftIndex ] );
			renderer.AddLine(  prevVertices[ rightIndex ], nextVertices[ rightIndex ] );
		}
		red::Memcpy( prevVertices, nextVertices, sideVertices * sizeof( Vector4 ) );
	}
}

red::StaticArray< Vector4, FixedCapsule::s_nbGeneratedDebugVertices > FixedCapsule::GenerateDebugVertices() const
{
	red::StaticArray< Vector4, s_nbGeneratedDebugVertices > returnValue( s_nbGeneratedDebugVertices );
	GenerateVertices( returnValue.TypedData(), returnValue.TypedData() + s_nbGeneratedDebugVertices / 2 );
	return returnValue;
}

void FixedCapsule::GenerateVertices( Vector4* verticesUpper, Vector4* verticesLower) const
{
	Float radius = math::FixedCapsule::GetRadius();

	const Vector4 pointA = math::FixedCapsule::CalcPointA();
	const Vector4 pointB = math::FixedCapsule::CalcPointB();

	constexpr Int32 horizontalSideLines = s_horizontalLines / 2;
	constexpr Int32 verticalSideLines = s_verticalLines / 2;

	static Float ySin[ s_horizontalLines + 1 ];
	static Float xSin[ s_verticalLines  ];
	static Float yCos[ s_horizontalLines + 1 ];
	static Float xCos[ s_verticalLines  ];
	static Bool isCacheReady = false;

	Uint32 index = 0;

	if( !isCacheReady )
	{
		Float stepY = RED_PI / (Float)s_horizontalLines;
		Float stepX = 2.0f * RED_PI / (Float)s_verticalLines;

		for( Int32 y = -horizontalSideLines; y <= horizontalSideLines; ++y) 
		{
			Float v = y * stepY;
			yCos[ index ]		= MCos( v );
			ySin[ index++ ]		= MSin( v );
		}

		index = 0;
		for( Int32 x = -verticalSideLines; x < verticalSideLines; ++x) 
		{
			Float u = x * stepX;
			xSin[ index ] =  MSin( u );
			xCos[ index++ ]  = MCos( u );
		}

		index = 0;
		isCacheReady = true;
	}

	// Upper hemisphere - indexes from verticesUpper[0...s_verticalLines] hold inner ring
	for( Int32 y = horizontalSideLines; y < s_horizontalLines; ++y) 
	{
		Float cosY		= yCos[ y ];
		Float sinY		= ySin[ y ];

		for( Int32 x = 0; x < s_verticalLines; ++x) 
		{
			Float cosX = xCos[ x ];
			Float sinX =   xSin[ x ];
			verticesUpper[ index++ ] = pointB + Vector4(  cosX * cosY,  sinX * cosY,  sinY  ) * radius;
		}
	}

	// Top point
	Float cosX = xCos[ 0 ];
	Float sinX =   xSin[ 0 ];
	Float cosYP		= yCos[ s_horizontalLines ];
	Float sinYP		= ySin[ s_horizontalLines ];

	verticesUpper[ index++ ] = pointB + Vector4(  cosX * cosYP,  sinX * cosYP,  sinYP  ) * radius;

	index = 0;

	// Lower hemisphere - indexes from verticesLower[0...s_verticalLines] hold inner ring
	for( Int32 y = horizontalSideLines; y > 0; --y) 
	{
		Float cosY		= yCos[ y ];
		Float sinY		= ySin[ y ];
		Float cosYP		= yCos[ y - 1 ];
		Float sinYP		= ySin[ y - 1 ];

		for( Int32 x = 0; x < s_verticalLines; ++x) 
		{
			Float cosX = xCos[ x ];
			Float sinX = xSin[ x ];

			verticesLower[ index++ ] = pointA + Vector4(  cosX * cosY,  sinX * cosY,  sinY  ) * radius;
		}
	}

	// Add top hemisphere vertex
	cosYP		= yCos[ 0 ];
	sinYP		= ySin[ 0 ];

	verticesLower[ index++ ] = pointA + Vector4(  cosX * cosYP,  sinX * cosYP,  sinYP  ) * radius;
}