#include "build.h"
#include "curveInterpolator.h"
#include "mathVector2.h"
#include "mathColor.h"

#include "../../../common/redMath/include/interpolation.h"

namespace helper
{
	RED_INLINE Int32 clamp_key( Int32 key, const Int32 max, const Bool loop = false )
	{
		return ( loop ) ? key % max : Clamp<Int32>( key, 0, max );
	}

	RED_INLINE Float scale_t( Float t, Float _min, Float _max )
	{
		return ( _max - _min ) > 0 ? ( t - _min ) / ( _max - _min ) : 1.f;
	}

	Int32 interpolation_search(const Float* times, Int32 numKeys, Float key)
	///
	///	Modified interpolation search. Returns the floor index of the value closest to the key being searched for or -1 if the value
	///	is outside the range of the curve
	///
	{
		Int32 low  = 0;
		Int32 high = numKeys - 1;
		Int32 mid;

		if( times[0] > key || key > times[numKeys-1] )
		{
			return -1;
		}

		while (times[high] != times[low] && key >= times[low] && key <= times[high]) 
		{
			mid = low + Int32(scale_t(key, times[low], times[high]) * (high - low));

			if (times[mid] < key)
			{
				low = mid + 1;
			}
			else if (key < times[mid])
			{
				high = mid - 1;
			}
			else
			{
				return mid;
			}
		}

		if( key >= times[high] )
		{
			return high;
		}
		else if( key >= times[low] )
		{
			return low;
		}
		else
		{
			return mid;
		}
	}

	void CalcLinearFactors( const Int32 numKeys, const Float* timeArray, Float at, Int32& outFirstKey, Int32& outSecondKey, Float& outT, Bool loop = false )
	{
		at			 = Clamp<Float>(at, timeArray[0], timeArray[numKeys-1]);
		outFirstKey  = clamp_key(interpolation_search(timeArray, numKeys, at), numKeys-1);
		outSecondKey = clamp_key(outFirstKey + 1, numKeys-1);
		outT         = scale_t(at, timeArray[outFirstKey], timeArray[outSecondKey]);
	}

	void CalcCubicHermiteFactors( const Int32 numKeys, const Float* timeArray, Float at, Int32& outFirstKey, Int32& outSecondKey, Int32& outThirdKey, Int32& outFourthKey, Float& outT, Bool loop = false )
	{
		at			 = Clamp<Float>(at, timeArray[0], timeArray[numKeys-1]);
		outFirstKey  = clamp_key( interpolation_search(timeArray, numKeys, at), numKeys-1);
		outFirstKey  = outFirstKey - (outFirstKey%3);
		outSecondKey = clamp_key(outFirstKey  + 1, numKeys-1, loop);
		outThirdKey  = clamp_key(outSecondKey + 1, numKeys-1, loop);
		outFourthKey = clamp_key(outThirdKey  + 1, numKeys-1, loop);
		outT		 = scale_t(at, timeArray[outFirstKey], timeArray[outFourthKey]);
	}

	void CalcQuadraticBezierFactors( const Int32 numKeys, const Float* timeArray, Float at, Int32& outFirstKey, Int32& outSecondKey, Int32& outThirdKey, Float& outT, Bool loop = false )
	{
		at			 = Clamp<Float>(at, timeArray[0], timeArray[numKeys-1]);
		outFirstKey  = clamp_key( interpolation_search(timeArray, numKeys, at), numKeys-1 );
		outFirstKey  = outFirstKey - (outFirstKey%2);
		outSecondKey = clamp_key(outFirstKey  + 1, numKeys-1, loop);
		outThirdKey  = clamp_key(outSecondKey + 1, numKeys-1, loop);
		outT		 = scale_t(at, timeArray[outFirstKey], timeArray[outThirdKey]);
	}

	void CalcCubicBezierFactors( const Int32 numKeys, const Float* timeArray, Float at, Int32& outFirstKey, Int32& outSecondKey, Int32& outThirdKey, Int32& outFourthKey, Float& outT, Bool loop = false )
	{
		at			 = Clamp<Float>(at, timeArray[0], timeArray[numKeys-1]);
		outFirstKey  = clamp_key( interpolation_search(timeArray, numKeys, at), numKeys-1 );
		outFirstKey  = outFirstKey - (outFirstKey%3);
		outSecondKey = clamp_key(outFirstKey  + 1, numKeys-1, loop);
		outThirdKey  = clamp_key(outSecondKey + 1, numKeys-1, loop);
		outFourthKey = clamp_key(outThirdKey  + 1, numKeys-1, loop);
		outT		 = scale_t(at, timeArray[outFirstKey], timeArray[outFourthKey]);
	}
}

template< typename T >
const T EvalCurve_Constant( const Int32 numKeys, const Float* times, const T* values, const Float at, Bool loop = false  )
{
	Float clampedAt = Clamp<Float>( at, times[ 0 ], times[ numKeys - 1 ] );
	const Int32 index = helper::clamp_key( helper::interpolation_search( times, numKeys, clampedAt ), numKeys );
	return values[index];
}

template< typename T >
const T EvalCurve_Linear( const Int32 numKeys, const Float* times, const T* values, const Float at, Bool loop = false  )
{
	Int32 src, dst;
	Float t;

	helper::CalcLinearFactors( numKeys, times, at, src, dst, t, loop );
	return math::Interpolation<T>::Linear( values[src], values[dst], t );
}

template< typename T >
const T EvalCurve_LinearSlow( const Int32 numKeys, const Float* times, const T* values, const Float at, Bool loop = false  )
{
	Int32 src, dst;
	Float t;

	helper::CalcLinearFactors( numKeys, times, at, src, dst, t, loop );
	return math::Interpolation<T>::LinearSlow( values[src], values[dst], t );
}

template<typename T>
const T EvalCurve_CubicHermite(const Int32 numKeys, const Float* times, const T* values, const Float at, Bool loop = false )
{
	Int32 p0, t0, t1, p1;
	Float t;

	helper::CalcCubicHermiteFactors( numKeys, times, at, p0, t0, t1, p1, t, loop );
	return math::Interpolation<T>::CubicHermite( values[p0], values[t0], values[t1], values[p1], t );
}

template<typename T>
const T EvalCurve_QuadradicBezier(const Int32 numKeys, const Float* times, const T* values, const Float at, Bool loop = false )
{
	Int32 p0, t0, p1;
	Float t;

	helper::CalcQuadraticBezierFactors( numKeys, times, at, p0, t0, p1, t, loop );
	return math::Interpolation<T>::QuadraticBezier( values[p0], values[t0], values[p1], t );
}

template<typename T>
const T EvalCurve_CubicBezier(const Int32 numKeys, const Float* times, const T* values, const Float at, Bool loop = false )
{
	Int32 p0, t0, t1, p1;
	Float t;

	helper::CalcCubicBezierFactors( numKeys, times, at, p0, t0, t1, p1, t, loop );
	return math::Interpolation<T>::CubicBezier( values[p0], values[t0], values[t1], values[p1], t );
}

const Quaternion EvalCurve_Quaternion(const Int32 numKeys, const Float* times, const Quaternion* values, const Float at, Bool loop = false )
{
	Int32 q0, q1;
	Float t;

	helper::CalcLinearFactors( numKeys, times, at, q0, q1, t, loop );
	return Quaternion::Slerp(values[q0], values[q1], t);
}

const EulerAngles EvalCurve_EulerAngles(const Int32 numKeys, const Float* times, const EulerAngles* values, const Float at, Bool loop = false )
{
	Int32 e0, e1;
	Float t;

	helper::CalcLinearFactors( numKeys, times, at, e0, e1, t, loop );
	return EulerAngles::Interpolate( values[e0], values[e1], t );
}

template<>
RED_REFLECTION_API Float ConstantCurveInterpolator<Float>::EvalAt( const Float t )
{
	return EvalCurve_Constant( m_numKeys, m_times, (const Float*)m_values, t, m_loop );
}

template<>
RED_REFLECTION_API Color ConstantCurveInterpolator<Color>::EvalAt(const Float t)
{
	return EvalCurve_Constant(m_numKeys, m_times, (const Color*)m_values, t, m_loop);
}

template<>
RED_REFLECTION_API Vector2 ConstantCurveInterpolator<Vector2>::EvalAt( const Float t )
{
	return EvalCurve_Constant( m_numKeys, m_times, (const Vector2*)m_values, t, m_loop );
}

template<>
RED_REFLECTION_API Vector3 ConstantCurveInterpolator<Vector3>::EvalAt( const Float t )
{
	return EvalCurve_Constant( m_numKeys, m_times, (const Vector3*)m_values, t, m_loop );
}

template<>
RED_REFLECTION_API Vector4 ConstantCurveInterpolator<Vector4>::EvalAt(const Float t)
{
	return EvalCurve_Constant(m_numKeys, m_times, (const Vector4*)m_values, t, m_loop);
}

template<>
RED_REFLECTION_API HDRColor ConstantCurveInterpolator<HDRColor>::EvalAt( const Float t )
{
	return EvalCurve_Constant( m_numKeys, m_times, (const HDRColor*)m_values, t, m_loop );
}

template<>
RED_REFLECTION_API Float LinearCurveInterpolator<Float>::EvalAt( const Float t )
{
	return EvalCurve_Linear( m_numKeys, m_times, (const Float*)m_values, t, m_loop );
}

template<>
RED_REFLECTION_API Color LinearCurveInterpolator<Color>::EvalAt(const Float t)
{
	return EvalCurve_Linear(m_numKeys, m_times, (const math::Color*)m_values, t, m_loop);
}

template<>
RED_REFLECTION_API Vector2 LinearCurveInterpolator<Vector2>::EvalAt( const Float t )
{
	return EvalCurve_Linear( m_numKeys, m_times, (const Vector2*)m_values, t, m_loop );
}

template<>
RED_REFLECTION_API Vector3 LinearCurveInterpolator<Vector3>::EvalAt( const Float t )
{
	return EvalCurve_Linear( m_numKeys, m_times, (const Vector3*)m_values, t, m_loop );
}

template<>
RED_REFLECTION_API Vector4 LinearCurveInterpolator<Vector4>::EvalAt( const Float t )
{
	return EvalCurve_Linear( m_numKeys, m_times, (const Vector4*)m_values, t, m_loop );
}

template<>
RED_REFLECTION_API HDRColor LinearCurveInterpolator<HDRColor>::EvalAt( const Float t )
{
	return EvalCurve_Linear( m_numKeys, m_times, (const HDRColor*)m_values, t, m_loop );
}

template<>
RED_REFLECTION_API Float CubicHermiteCurveInterpolator<Float>::EvalAt( const Float t )
{
	return EvalCurve_CubicHermite( m_numKeys, m_times, (const Float*)m_values, t, m_loop );
}

template<>
RED_REFLECTION_API Vector2 CubicHermiteCurveInterpolator<Vector2>::EvalAt( const Float t )
{
	return EvalCurve_CubicHermite( m_numKeys, m_times, (const Vector2*)m_values, t, m_loop );
}

template<>
RED_REFLECTION_API Vector3 CubicHermiteCurveInterpolator<Vector3>::EvalAt( const Float t )
{
	return EvalCurve_CubicHermite( m_numKeys, m_times, (const Vector3*)m_values, t, m_loop );
}

template<>
RED_REFLECTION_API Vector4 CubicHermiteCurveInterpolator<Vector4>::EvalAt( const Float t )
{
	return EvalCurve_CubicHermite( m_numKeys, m_times, (const Vector4*)m_values, t, m_loop );
}

template<>
RED_REFLECTION_API HDRColor CubicHermiteCurveInterpolator<HDRColor>::EvalAt( const Float t )
{
	return EvalCurve_CubicHermite( m_numKeys, m_times, (const HDRColor*)m_values, t, m_loop );
}

template<>
RED_REFLECTION_API Float QuadraticBezierCurveInterpolator<Float>::EvalAt( const Float t )
{
	return EvalCurve_QuadradicBezier( m_numKeys, m_times, (const Float*)m_values, t, m_loop );
}

template<>
RED_REFLECTION_API Vector2 QuadraticBezierCurveInterpolator<Vector2>::EvalAt( const Float t )
{
	return EvalCurve_QuadradicBezier( m_numKeys, m_times, (const Vector2*)m_values, t, m_loop );
}

template<>
RED_REFLECTION_API Vector3 QuadraticBezierCurveInterpolator<Vector3>::EvalAt( const Float t )
{
	return EvalCurve_QuadradicBezier( m_numKeys, m_times, (const Vector3*)m_values, t, m_loop );
}

template<>
RED_REFLECTION_API Vector4 QuadraticBezierCurveInterpolator<Vector4>::EvalAt( const Float t )
{
	return EvalCurve_QuadradicBezier( m_numKeys, m_times, (const Vector4*)m_values, t, m_loop );
}

template<>
RED_REFLECTION_API HDRColor QuadraticBezierCurveInterpolator<HDRColor>::EvalAt( const Float t )
{
	return EvalCurve_QuadradicBezier( m_numKeys, m_times, (const HDRColor*)m_values, t, m_loop );
}

template<>
RED_REFLECTION_API Float CubicBezierCurveInterpolator<Float>::EvalAt( const Float t )
{
	return EvalCurve_CubicBezier( m_numKeys, m_times, (const Float*)m_values, t, m_loop );
}

template<>
RED_REFLECTION_API Vector2 CubicBezierCurveInterpolator<Vector2>::EvalAt( const Float t )
{
	return EvalCurve_CubicBezier( m_numKeys, m_times, (const Vector2*)m_values, t, m_loop );
}

template<>
RED_REFLECTION_API Vector3 CubicBezierCurveInterpolator<Vector3>::EvalAt( const Float t )
{
	return EvalCurve_CubicBezier( m_numKeys, m_times, (const Vector3*)m_values, t, m_loop );
}

template<>
RED_REFLECTION_API Vector4 CubicBezierCurveInterpolator<Vector4>::EvalAt( const Float t )
{
	return EvalCurve_CubicBezier( m_numKeys, m_times, (const Vector4*)m_values, t, m_loop );
}

template<>
RED_REFLECTION_API HDRColor CubicBezierCurveInterpolator<HDRColor>::EvalAt( const Float t )
{
	return EvalCurve_CubicBezier( m_numKeys, m_times, (const HDRColor*)m_values, t, m_loop );
}

Quaternion QuaternionCurveInterpolator::EvalAt( const Float t )
{
	return EvalCurve_Quaternion( m_numKeys, m_times, (const Quaternion*)m_values, t, m_loop );
}

EulerAngles EulerAnglesCurveInterpolator::EvalAt(const Float t)
{
	return EvalCurve_EulerAngles( m_numKeys, m_times, (const EulerAngles*)m_values, t, m_loop );
}

Vector4 SlowVectorCurveInterpolator::EvalAt( const Float t )
{
	return EvalCurve_LinearSlow(m_numKeys, m_times, (const Vector4*)m_values, t, m_loop );
}