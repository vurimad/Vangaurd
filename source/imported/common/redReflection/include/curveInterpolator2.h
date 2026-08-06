/**
* Copyright (c) 2016 CD Projekt Red. All Rights Reserved.
*/

#pragma once

#include "singleChannelCurve2.h"
#include "multiChannelCurve2.h"

namespace curve
{
	template< typename T >
	T InterpolateCurve_Constant( const TSingleChannelCurve<T>& data, Float factor );

	template< typename T >
	T InterpolateCurve_Constant( const TMultiChannelCurve<T>& data, const Uint32 channel, Float factor );

	template< typename T >
	T InterpolateCurve_Linear( const TSingleChannelCurve<T>& data, Float factor );

	template< typename T >
	T InterpolateCurve_Linear( const TMultiChannelCurve<T>& data, const Uint32 channel, Float factor );

	template< typename T >
	T InterpolateCurve_QuadraticBezier( const TSingleChannelCurve<T>& data, Float factor );

	template< typename T >
	T InterpolateCurve_QuadraticBezier( const TMultiChannelCurve<T>& data, const Uint32 channel, Float factor );

	template< typename T >
	T InterpolateCurve_CubicBezier( const TSingleChannelCurve<T>& data, Float factor );
	
	template< typename T >
	T InterpolateCurve_CubicBezier( const TMultiChannelCurve<T>& data, const Uint32 channel, Float factor );

    template< typename T >
	T InterpolateCurve_CubicHermite( const TSingleChannelCurve<T>& data, Float factor );
	
	template< typename T >
	T InterpolateCurve_CubicHermite( const TMultiChannelCurve<T>& data, const Uint32 channel, Float factor );

    template< typename T >
    T Interpolate(const TSingleChannelCurve<T>& data, Float factor);

    template< typename T >
    T Interpolate(const TMultiChannelCurve<T>& data, Uint32 channel, Float factor);

	namespace FOR_UNIT_TESTS
	{
		Int32 RED_REFLECTION_API test_get_key_frame( const Int32 numKeys, Float t );
		Float RED_REFLECTION_API test_scale_factor_between_0_and_1( const Float min, const Float max, Float in );
		Float RED_REFLECTION_API test_calc_factor( const Int32 numValues, Float in );

		void RED_REFLECTION_API test_calc_linear_factors( const Int32 numValues, Int32& src, Int32& dst, Float factor );
		void RED_REFLECTION_API test_calc_qbezier_factors( const Int32 numValues, Int32& p0, Int32& c0, Int32& p1, Float factor );
		void RED_REFLECTION_API test_calc_cbezier_factors( const Int32 numValues, Int32& p0, Int32& c0, Int32& c1, Int32& p1, Float factor );
	}
}