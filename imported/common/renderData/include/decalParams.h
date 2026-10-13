/**
* Copyright (c) 2019 CD Projekt Red. All Rights Reserved.
*/
#pragma once
#include "../../redMath/include/random.h"

enum class RenderDecalNormalsBlendingMode : Uint8
{
	AlphaBlending,
	Reorient,
};

RTTI_DECLARE_ENUM( RenderDecalNormalsBlendingMode );

enum class RenderDecalOrderPriority : Uint8
{
	Priority0,
	Priority1,
	Priority2,
	Priority3,
};

RTTI_DECLARE_ENUM( RenderDecalOrderPriority );

namespace DecalUtils
{

	static Float CalculateAutohideDistance( const Box& box, const Float alpha = 1.0f, Vector3 decalProjectionVector = Vector3( 0.0f, 1.0f, 0.f ) )
	{
		// now, take only XY plane, we do not care how deep the projection is, we only care about the size of the rectangle the decal is projecting onto
		Vector2 boxSize = box.CalcSize().AsVector2();

		const Float projectionPlaneDiagonal = boxSize.Mag();

		const Float minimumAutohideDistance = 25.0f;

		Float autohideDistanceMultiplier = 6.f;

		Float verticalFactor = Abs( decalProjectionVector.Dot( Vector3::EZ() ) );

		if ( projectionPlaneDiagonal > 0.4f ) 
		{
			autohideDistanceMultiplier = 12.f;
		}

		if ( projectionPlaneDiagonal > 2.f )
		{
			autohideDistanceMultiplier = 16.f + ( 16.f * ( 1.0f - verticalFactor ) );
		}

		if ( projectionPlaneDiagonal > 8.f )
		{
			autohideDistanceMultiplier = 24.f + ( 24.f * ( 1.0f - verticalFactor ) );;
		}

		const Float distance = Max( autohideDistanceMultiplier * projectionPlaneDiagonal, minimumAutohideDistance );

		// lower the distance based on alpha value
		const Float aphaScaling = math::Lerp( math::Clamp( alpha, 0.0f, 1.0f ), 0.5f, 1.0f );

		return distance * aphaScaling;
	}

	static constexpr Uint8 NumDecalBuckets = 4;
	static constexpr Uint8 DecalRangeStart = 16;
	static constexpr Uint8 DecalRangeEnd = 255;
	static constexpr Uint8 DecalRangeSize = ( DecalRangeEnd - DecalRangeStart + 1 ) / NumDecalBuckets;
	static Uint16 CalculateDecalOrder( Uint32 orderPriority )
	{
		/*
		Split the range [0, 255] into NumDecalBuckets even buckets, depending on the Priority.
		Assumptions:
		- the ranges are mutually exclusive
		- higher prioriy (eg. Priority0 is higher than Priority1) means range with lower values
		*/

		const Uint32 result = DecalRangeStart + math::DefaultRandom().Get< Uint32 >( DecalRangeSize ) + DecalRangeSize * (Uint32)orderPriority;
		RED_ASSERT( result <= DecalRangeEnd );
		return (Uint16)result;
	}

	static Uint16 CalculateParticleTrailOrder()
	{
		/*
		The range [0, 255] contains indices for decals and particle trails (skidmarks), skidmarks should be rendered on top, so they occupy range [0; DecalRangeStart-1]
		*/
		const Uint32 result =  + math::DefaultRandom().Get< Uint32 >( DecalRangeStart - 1 );
		RED_ASSERT( 0 <= result && result < DecalRangeStart );
		return (Uint16)result;
	}
}
