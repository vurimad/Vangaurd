/**
* Copyright (c) 2016 CD Projekt Red. All Rights Reserved.
*/

#pragma once

#include "curveInterpolator.h"

//#include "singleChannelCurve.h"
//#include "multiChannelCurve.h"

namespace CurveDataEvaluator
{
	template< typename T >
	static T EvalAt( const TSingleChannelCurve<T>& curve, Float t )
	{
		T val;
		switch (curve.GetInterpolationType())
		{
		case EInterpolationType::EIT_Constant:
		{
			val = ConstantCurveInterpolator< T >(curve).EvalAt(t);
		}
		break;
		case EInterpolationType::EIT_Linear:
		{
			val = LinearCurveInterpolator< T >(curve).EvalAt(t);
		}
		break;
		case EInterpolationType::EIT_BezierQuadratic:
		{
			val = QuadraticBezierCurveInterpolator< T >(curve).EvalAt(t);
		}
		break;
		case EInterpolationType::EIT_BezierCubic:
		{
			val = CubicBezierCurveInterpolator< T >(curve).EvalAt(t);
		}
		break;
		case EInterpolationType::EIT_Hermite:
		{
			val = CubicHermiteCurveInterpolator< T >(curve).EvalAt(t);
		}
		break;
		default:
		{
			RED_ASSERT(false, "Trying to evaluate invalid curve type.");
		}
		break;
		}

		return val;
	}

	template< typename T >
	static T EvalAt( const TMultiChannelCurve<T>& curve, Uint32 channel, Float t )
	{
		T val;
		switch ( curve.GetInterpolationType() )
		{
		case EInterpolationType::EIT_Constant:
		{
			val = ConstantCurveInterpolator< T >( curve, channel ).EvalAt( t );
		}
		break;
		case EInterpolationType::EIT_Linear:
		{
			val = LinearCurveInterpolator< T >( curve, channel ).EvalAt( t );
		}
		break;
		case EInterpolationType::EIT_BezierQuadratic:
		{
			val = QuadraticBezierCurveInterpolator< T >( curve, channel ).EvalAt( t );
		}
		break;
		case EInterpolationType::EIT_BezierCubic:
		{
			val = CubicBezierCurveInterpolator< T >( curve, channel ).EvalAt( t );
		}
		break;
		case EInterpolationType::EIT_Hermite:
		{
			val = CubicHermiteCurveInterpolator< T >( curve, channel ).EvalAt( t );
		}
		break;
		default:
		{
			RED_ASSERT( false, "Trying to evaluate invalid curve type." );
		}
		break;
		}

		return val;
	}
};