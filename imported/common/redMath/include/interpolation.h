/**
* Copyright (c) 2016 CD Projekt Red. All Rights Reserved.
*/

#pragma once

/**
* Copyright (c) 2016 CD Projekt Red. All Rights Reserved.
*/

#pragma once

#include "vector4.h"
#include "color.h"

namespace math
{

template<typename T>
class Interpolation
{
public:
	static T LinearSlow( const T& src, const T& dst, const red::Float t )
	{
		return src*(1.f-t) + dst*t;
	}

	static T Linear( const T& src, const T& dst, const red::Float t )
	{
		return src*(1.f-t) + dst*t;
	}

	static T CubicHermite( const T& p0, const T& t0, const T& t1, const T& p1, const red::Float t )
	{
		const red::Float t_cubed = t*t*t;
		const red::Float t_sq = t*t;
		return p0 * ( 2.f*t_cubed - 3.f*t_sq + 1.f) + t0 * ( t_cubed - 2.f*t_sq + t ) + p1 * ( -2.f*t_cubed + 3.f*t_sq ) + t1 * ( t_cubed - t_sq );
	}

	static T QuadraticBezier( const T& p0, const T& c0, const T& p1, const red::Float t )
	{
		const red::Float p0_coef = (1.f - t)*(1.f - t);
		const red::Float c0_coef = 2.f*t*(1.f - t);

		return ( p0 * p0_coef ) + ( c0 * c0_coef ) + p1 * ( t*t );
	}

	static T CubicBezier( const T& p0, const T& c0, const T& c1, const T& p1, const red::Float t )
	{
		const red::Float p0_coef = (1.f - t)*(1.f - t)*(1.f - t);
		const red::Float c0_coef = 3.f*t*(1.f - t)*(1.f - t);
		const red::Float c1_coef = 3.f*t*t*(1.f - t);

		return (p0 * p0_coef) + (c0 * c0_coef) + (c1 * c1_coef) + p1 * ( t*t*t );
	}
};

template<>
struct REDMATH_API Interpolation<Vector4>
{
	static Vector4 LinearSlow( const Vector4& src, const Vector4& dst, const Float t );
	static Vector4 Linear( const Vector4& src, const Vector4& dst, const Float t );
	static Vector4 CubicHermite( const Vector4& p0, const Vector4& p1, const Vector4& p2, const Vector4& p3, const Float t );
	static Vector4 QuadraticBezier( const Vector4& p0, const Vector4& c0, const Vector4& p1, const Float t );
	static Vector4 CubicBezier( const Vector4& p0, const Vector4& p1, const Vector4& p2, const Vector4& p3, const Float t );
};

template<>
struct REDMATH_API Interpolation<Color>
{
	static Color LinearSlow(const Color& src, const Color& dst, const Float t);
	static Color Linear(const Color& src, const Color& dst, const Float t);
	static Color CubicHermite(const Color& p0, const Color& p1, const Color& p2, const Color& p3, const Float t);
	static Color QuadraticBezier(const Color& p0, const Color& p1, const Color& p2, const Float t);
	static Color CubicBezier(const Color& p0, const Color& p1, const Color& p2, const Color& p3, const Float t);
};

extern REDMATH_API void Linear_SIMD( Float out[4], const Float src[4], const Float dst[4], const Float t[4] );
extern REDMATH_API void CubicHermite_SIMD( Float out[4], const Float A[4], const Float B[4], const Float C[4], const Float D[4], const Float t[4] );
extern REDMATH_API void QuadraticBezier_SIMD( Float out[4], const Float p0[4], const Float p1[4], const Float p2[4], const Float t[4] );
extern REDMATH_API void CubicBezier_SIMD( Float out[4], const Float p0[4], const Float p1[4], const Float p2[4], const Float p3[4], const Float t[4] );

}