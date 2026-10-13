#pragma once

#include "simdQuad.h"

namespace simd
{
	// TODO: merge with math::Box class ASAP (after E3 Demo 2018)
	struct Box
	{
		Quad min;
		Quad max;
	};

	inline Box ToSimd( const math::Box& scalar )
	{
		Box result;
		result.min = _mm_load_ps( scalar.Min.AsFloat() );
		result.max = _mm_load_ps( scalar.Max.AsFloat() );

		return result;
	}

	inline math::Box ToScalar( const simd::Box& box )
	{
		math::Box result;
		result.Min = math::Vector4( (const Float*)&box.min );
		result.Max = math::Vector4( (const Float*)&box.max );
		return result;
	}

	inline Box InitBox( const Quad& pos, const Quad& radius )
	{
		Box box;
		box.min = _mm_sub_ps( pos, radius );
		box.max = _mm_add_ps( pos, radius );
		return box;
	}

	inline Box AddBox( const simd::Box& a, const simd::Box& b )
	{
		Box box;
		box.min = _mm_min_ps( a.min, b.min );
		box.max = _mm_max_ps( a.max, b.max );
		return box;
	}


}
