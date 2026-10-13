/**
* Copyright (c) 2026 RED Vanguard, All Rights Reserved.
*/
#include "build.h"
#include "mathCommon.h"
#include "mathSphere.h"
#include "scriptStackFrame.h"
#include "shapeRenderer.h"

void Sphere::funcIntersectRay( IScriptable*, CScriptStackFrame& stack, void* result, const rtti::IType* resultType )
{
	GET_PARAMETER( Sphere, sphere, Sphere( Vector4::ZEROS(), 0.0f ) );
	GET_PARAMETER( Vector4, orign, Vector4::ZEROS() );
	GET_PARAMETER( Vector4, dir, Vector4::ZEROS() );
	GET_PARAMETER_REF( ::Vector4, enterPoint, Vector4::ZEROS() );
	GET_PARAMETER_REF( Vector4, exitPoint, Vector4::ZEROS() );
	FINISH_PARAMETERS;

	RETURN_INT( sphere.IntersectRay( orign, dir, enterPoint, exitPoint ) );
}

void Sphere::funcIntersectEdge( IScriptable*, CScriptStackFrame& stack, void* result, const rtti::IType* resultType )
{
	GET_PARAMETER( Sphere, sphere, Sphere( Vector4::ZEROS(), 0.0f ) );
	GET_PARAMETER( Vector4, a, Vector4::ZEROS() );
	GET_PARAMETER( Vector4, b, Vector4::ZEROS() );
	GET_PARAMETER_REF( Vector4, ip0, Vector4::ZEROS() );
	GET_PARAMETER_REF( Vector4, ip1, Vector4::ZEROS() );
	FINISH_PARAMETERS;

	RETURN_INT( sphere.IntersectEdge( a, b, ip0, ip1 ) );
}

void Sphere::RenderSolid(red::IShapeRenderer& renderer, const Bool swap /*= false*/) const
{
	const auto vertices = GenerateDebugVertices();
	
	// add sides
	for( Uint32 y = 0; y < s_horizontalLines - 2; ++y) 
	{
		Uint32 strideOffset = y * s_verticalLines;
		Uint32 strideOffsetTop = ( y + 1 ) * s_verticalLines;


		for( Uint32 x = 1; x <= s_verticalLines; ++x) 
		{
			Uint32 leftIndex = x + strideOffset ;
			Uint32 rightIndex = x % s_verticalLines + 1 + strideOffset;
			Uint32 topIndexLeft = x + strideOffsetTop;
			Uint32 topIndexRight = x % s_verticalLines + 1 + strideOffsetTop;

			if ( swap )
			{
				renderer.AddTriangle( vertices[ rightIndex ], vertices[ topIndexLeft ], vertices[ leftIndex ]  );
				renderer.AddTriangle( vertices[ topIndexRight  ], vertices[ topIndexLeft ], vertices[ rightIndex ] );
			}
			else
			{
				renderer.AddTriangle( vertices[ leftIndex ], vertices[ topIndexLeft ], vertices[ rightIndex ] );
				renderer.AddTriangle( vertices[ rightIndex ], vertices[ topIndexLeft ], vertices[ topIndexRight  ] );
			}
		}
	}

	// add bottom and top triangles
	Uint32 strideOffset =  ( s_horizontalLines - 2 ) * s_verticalLines;

	for( Uint32 x = 1; x <= s_verticalLines; ++x) 
	{
		Uint32 leftIndex = x ;
		Uint32 rightIndex = x % s_verticalLines + 1;

		//add bottom triangles
		if ( swap )
			renderer.AddTriangle( vertices[ rightIndex ], vertices[ leftIndex ], vertices[ 0 ] );
		else
			renderer.AddTriangle( vertices[ 0 ], vertices[ leftIndex ], vertices[ rightIndex ] );

		leftIndex = x + strideOffset;
		rightIndex = x % s_verticalLines + 1 + strideOffset ;

		//add top triangles
		if ( swap )
			renderer.AddTriangle( vertices[ leftIndex ], vertices[ rightIndex ], vertices.Back() );
		else
			renderer.AddTriangle( vertices.Back(), vertices[ rightIndex ], vertices[ leftIndex ] );
	}
}

void Sphere::RenderWireframe(red::IShapeRenderer& renderer) const
{
	const auto vertices = GenerateDebugVertices();

	// add sides
	for( Uint32 y = 0; y < s_horizontalLines - 2; ++y) 
	{
		Uint32 strideOffset = y * s_verticalLines;
		Uint32 strideOffsetTop = ( y + 1 ) * s_verticalLines;


		for( Uint32 x = 1; x <= s_verticalLines; ++x) 
		{
			Uint32 leftIndex = x + strideOffset ;
			Uint32 rightIndex = x % s_verticalLines + 1 + strideOffset;
			Uint32 topIndexLeft = x + strideOffsetTop;
			Uint32 topIndexRight = x % s_verticalLines + 1 + strideOffsetTop;

			renderer.AddLine( vertices[ leftIndex], vertices[ topIndexLeft ] );
			renderer.AddLine( vertices[ leftIndex ], vertices[ rightIndex ] );

			renderer.AddLine( vertices[ rightIndex ], vertices[ topIndexRight ] );
			renderer.AddLine( vertices[ topIndexLeft ], vertices[ topIndexRight] );
		}
	}

	// add top triangles
	Uint32 strideOffset =  ( s_horizontalLines - 2 ) * s_verticalLines;

	for( Uint32 x = 1; x <= s_verticalLines; ++x) 
	{
		Uint32 leftIndex = x ;

		renderer.AddLine( vertices[ 0 ], vertices[ leftIndex ] );

		leftIndex = x + strideOffset;

		renderer.AddLine( vertices.Back(), vertices[ leftIndex ] );
	}
}



red::StaticArray< Vector4, Sphere::s_nbGeneratedDebugVertices > Sphere::GenerateDebugVertices() const
{
	red::StaticArray< Vector4, s_nbGeneratedDebugVertices > returnValue( s_nbGeneratedDebugVertices );
	GenerateVertices( returnValue.TypedData() );
	return returnValue;
}

void Sphere::GenerateVertices( Vector4* arrayToFill ) const
{
	Float radius = math::Sphere::GetRadius();
	const Vector4 center = math::Sphere::GetCenter();

	const Int32 horizontalSideLines = s_horizontalLines / 2;
	const Int32 verticalSideLines = s_verticalLines / 2;

	static Float ySin[ s_horizontalLines + 1 ];
	static Float xSin[ s_verticalLines ];
	static Float yCos[ s_horizontalLines + 1 ];
	static Float xCos[ s_verticalLines ];
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
			xSin[ index ] = MSin( u );
			xCos[ index++ ]  = MCos( u );
		}

		index = 0;
		isCacheReady = true;
	}

	Float cosY		= yCos[ 0 ];
	Float sinY		= ySin[ 0 ];
	Float cosX = xCos[ 0 ];
	Float sinX = xSin[ 0 ];

	// lower hemisphere top point
	arrayToFill[ index++ ] = center + Vector4(  cosX * cosY,  sinX * cosY,  sinY  ) * radius;
	for( Int32 y = 1; y < s_horizontalLines; ++y) 
	{
		cosY		= yCos[ y ];
		sinY		= ySin[ y ];

		for( Int32 x = 0; x < s_verticalLines; ++x) 
		{
			cosX = xCos[ x ];
			sinX = xSin[ x ];
			arrayToFill[ index++ ] = center + Vector4(  cosX * cosY,  sinX * cosY,  sinY  ) * radius;
		}
	}
	cosY		= yCos[ s_horizontalLines ];
	sinY		= ySin[ s_horizontalLines ];

	// upper hemisphere top point
	arrayToFill[ index++ ] = center + Vector4(  cosX * cosY,  sinX * cosY,  sinY  ) * radius;


}