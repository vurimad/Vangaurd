/**
* Copyright (c) 2014 CD Projekt Red. All Rights Reserved.
*/
#pragma once

namespace math
{
	namespace utils
	{

		// Solve quadratic equation in form of (ax^2 + bx + c == 0), solutions are given in x1 and x2. Returns false if there were no solutions.
		extern REDMATH_API Bool SolveQuadraticEquation( const Float a, const Float b, const Float c, Float& x1, Float& x2 );
		extern REDMATH_API Float BezierSmoothStep(const Float t);
		extern REDMATH_API Float SmoothstepInterpolation(const Float t);
		extern REDMATH_API Vector3 ComputeBaryCentric(const Vector3& p, const Vector3& p0, const Vector3& p1, const Vector3& p2);
		extern REDMATH_API Vector4 ComputeBaryCentric(const Vector3& p, const Vector3& p0, const Vector3& p1, const Vector3& p2, const Vector3& p3);
		extern REDMATH_API Bool IsPointOnSegment(const Vector3& p, const Vector3& a, const Vector3& b);
		extern REDMATH_API Vector4 ClosestPointOnTriangle( const Vector4& p0, const Vector4& p1, const Vector4& p2, const Vector4& p );
		
		// returns normalized value [0...1]
		// x <= edge0 -> 0.f
		// x > edge0 && x < edge1 -> linearstep
		// x >= edge1 -> 1.f
		inline Float LinearStep( Float x, Float edge0, Float edge1 )
		{
			RED_FATAL_ASSERT( edge0 < edge1 );
			return Clamp( (x - edge0) / (edge1 - edge0), 0.f, 1.f );
		}

		// returns normalized value [0...1]
		// x <= edge0 -> 0.f
		// x > edge0 && x < edge1 -> smoothstep
		// x >= edge1 -> 1.f
		inline Float SmoothStep( Float x, Float edge0, Float edge1 )
		{
			RED_FATAL_ASSERT( edge0 < edge1 );
			x = Clamp( (x - edge0) / (edge1 - edge0), 0.f, 1.f );
			return x *x * (3.f - 2.f * x);
		}

		template< typename T >
		T LowPassFilter( const T& raw, const T& current, Float rc, Float dt )
		{
			const Float a = dt / (rc + dt);
			return current * (1.f - a) + raw * a;
		}
	} // utils

	struct ScopedDenormGuard
	{
		ScopedDenormGuard()
		{
			m_prevState = _mm_getcsr();
			_mm_setcsr(_MM_MASK_MASK | _MM_FLUSH_ZERO_ON | _MM_DENORMALS_ZERO_ON );
		}

		~ScopedDenormGuard()
		{
			_mm_setcsr(m_prevState & ~_MM_EXCEPT_MASK);
		}

		Uint32 m_prevState;
	};

#if !defined( RED_PLATFORM_ORBIS ) && !defined( RED_PLATFORM_LINUX )
	struct ScopedFPExceptionGuard
	{
		ScopedFPExceptionGuard(Uint32 enableBits = _EM_OVERFLOW | _EM_ZERODIVIDE | _EM_INVALID)
		{
			_controlfp_s(&m_originalMask, _MCW_EM, _MCW_EM);
			enableBits &= _MCW_EM;
			_clearfp();
			_controlfp_s(0, ~enableBits, enableBits);
		}

		~ScopedFPExceptionGuard()
		{
			_controlfp_s(0, m_originalMask, _MCW_EM);
		}

		ScopedFPExceptionGuard(const ScopedFPExceptionGuard&) = delete;
		ScopedFPExceptionGuard& operator=(const ScopedFPExceptionGuard&) = delete;

	private:
		Uint32 m_originalMask;
	};
#endif
} // math

#if !defined( RED_CONFIGURATION_FINAL ) && !defined( RED_PLATFORM_ORBIS ) && !defined( RED_PLATFORM_LINUX )
#define MATH_SCOPE_DENORM_GUARD() math::ScopedDenormGuard RED_CONCATENATE2( s_varDenormGuard, __LINE__ )
#define MATH_SCOPE_FP_EXCEPTION_GUARD() math::ScopedFPExceptionGuard RED_CONCATENATE2( s_varFpGuard, __LINE__ )
#else
#define MATH_SCOPE_DENORM_GUARD() do {} while( (void)0,0 )
#define MATH_SCOPE_FP_EXCEPTION_GUARD() do {} while( (void)0,0 )
#endif