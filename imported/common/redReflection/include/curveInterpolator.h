/**
* Copyright (c) 2016 CD Projekt Red. All Rights Reserved.
*/

#pragma once

#include "mathQuaternion.h"
#include "mathEulerAngles.h"
#include "mathVector4.h"

#include "singleChannelCurve.h"
#include "multiChannelCurve.h"

template< typename T >
class CurveInterpolator
{
public:
	CurveInterpolator( const TSingleChannelCurve<T>& data, const Bool loop = false )
		: m_numKeys( data.GetNumKeys() )
		, m_times( data.GetKeyTimesArray() )
		, m_values(data.GetKeyValuesArray())
		, m_loop(loop)
	{}

	CurveInterpolator( const TMultiChannelCurve<T>& data, const Uint32 channel, const Bool loop = false )
		: m_numKeys( data.GetNumKeys(channel) )
		, m_times( data.GetKeyTimesArray(channel) )
		, m_values(data.GetKeyValuesArray(channel))
		, m_loop(loop)
	{}

protected:
	Uint32			m_numKeys;
	const Float*	m_times;
	const void*		m_values;
	Bool			m_loop;
};

template< typename T >
class ConstantCurveInterpolator : public CurveInterpolator<T>
{
public:
	 ConstantCurveInterpolator( const TSingleChannelCurve<T>& data, const Bool loop = false )
		: CurveInterpolator<T>(data, loop)
	{}

	 ConstantCurveInterpolator( const TMultiChannelCurve<T>& data, const Uint32 channel, const Bool loop = false )
		: CurveInterpolator<T>(data, channel, loop)
	{}

	T EvalAt( const Float t );
};

template< typename T >
class LinearCurveInterpolator : public CurveInterpolator<T>
{
public:
	LinearCurveInterpolator( const TSingleChannelCurve<T>& data, const Bool loop = false )
		: CurveInterpolator<T>(data, loop)
	{}

	LinearCurveInterpolator( const TMultiChannelCurve<T>& data, const Uint32 channel, const Bool loop = false )
		: CurveInterpolator<T>(data, channel, loop)
	{}

	T EvalAt( const Float t );
};

template< typename T >
class CubicHermiteCurveInterpolator : public CurveInterpolator<T>
{
public:
	CubicHermiteCurveInterpolator( const TSingleChannelCurve<T>& data, const Bool loop = false )
		: CurveInterpolator<T>(data, loop)
	{}

	CubicHermiteCurveInterpolator( const TMultiChannelCurve<T>& data, const Uint32 channel, const Bool loop = false )
		: CurveInterpolator<T>(data, channel, loop)
	{}

	T EvalAt( const Float t );
};

template< typename T >
class QuadraticBezierCurveInterpolator : public CurveInterpolator<T>
{
public:
	QuadraticBezierCurveInterpolator( const TSingleChannelCurve<T>& data, const Bool loop = false )
		: CurveInterpolator<T>(data, loop)
	{}

	QuadraticBezierCurveInterpolator( const TMultiChannelCurve<T>& data, const Uint32 channel, const Bool loop = false )
		: CurveInterpolator<T>(data, channel, loop)
	{}

	T EvalAt( const Float t );
};

template< typename T >
class CubicBezierCurveInterpolator : public CurveInterpolator<T>
{
public:
	CubicBezierCurveInterpolator( const TSingleChannelCurve<T>& data, const Bool loop = false )
		: CurveInterpolator<T>(data, loop)
	{}

	CubicBezierCurveInterpolator( const TMultiChannelCurve<T>& data, const Uint32 channel, const Bool loop = false )
		: CurveInterpolator<T>(data, channel, loop)
	{}

	T EvalAt( const Float t );
};

class RED_REFLECTION_API QuaternionCurveInterpolator : public CurveInterpolator<Quaternion>
{
public:
	QuaternionCurveInterpolator( const TSingleChannelCurve<Quaternion>& data, const Bool loop = false )
		: CurveInterpolator<Quaternion>(data, loop)
	{}

	QuaternionCurveInterpolator( const TMultiChannelCurve<Quaternion>& data, const Uint32 channel, const Bool loop = false )
		: CurveInterpolator<Quaternion>(data, channel, loop)
	{}

	Quaternion EvalAt( const Float t );
};

class RED_REFLECTION_API EulerAnglesCurveInterpolator : public CurveInterpolator<EulerAngles>
{
public:
	EulerAnglesCurveInterpolator( const TSingleChannelCurve<EulerAngles>& data, const Bool loop = false )
		: CurveInterpolator<EulerAngles>(data, loop)
	{}

	EulerAnglesCurveInterpolator( const TMultiChannelCurve<EulerAngles>& data, const Uint32 channel, const Bool loop = false )
		: CurveInterpolator<EulerAngles>(data, channel, loop)
	{}

	EulerAngles EvalAt( const Float t );
};

class RED_REFLECTION_API SlowVectorCurveInterpolator : public CurveInterpolator<Vector4>
///
/// @note [william.mcvicar] This is only used as a comparison, between the SIMD and Non SIMD versions.
///
{
public:
	SlowVectorCurveInterpolator( const TSingleChannelCurve<Vector4>& data, const Bool loop = false )
		: CurveInterpolator<Vector4>(data, loop)
	{}

	SlowVectorCurveInterpolator( const TMultiChannelCurve<Vector4>& data, const Uint32 channel, const Bool loop = false )
		: CurveInterpolator<Vector4>(data, channel, loop)
	{}

	Vector4 EvalAt( const Float t );
};