#pragma once

namespace simd
{
	// TODO: merge with math::Matrix class ASAP (after E3 Demo 2018)
	union Mat44SSE {
		Float m[4][4];
		__m128 row[4];
	};

	// code taken from Fabian 'ryg' Giesen website :)
	inline __m128 LinearCombineSSE( const __m128 &a, const Mat44SSE& B )
	{
		__m128 result;
		result = _mm_mul_ps( _mm_shuffle_ps( a, a, 0x00 ), B.row[0] );
		result = _mm_add_ps( result, _mm_mul_ps( _mm_shuffle_ps( a, a, 0x55 ), B.row[1] ) );
		result = _mm_add_ps( result, _mm_mul_ps( _mm_shuffle_ps( a, a, 0xaa ), B.row[2] ) );
		result = _mm_add_ps( result, _mm_mul_ps( _mm_shuffle_ps( a, a, 0xff ), B.row[3] ) );
		return result;
	}

	inline Matrix MulSSE( const Matrix& a, const Matrix& b )
	{
		const Mat44SSE& aSSE = (const Mat44SSE&)a;
		const Mat44SSE& bSSE = (const Mat44SSE&)b;
		
		Matrix ret;
		Mat44SSE* retSSE = (Mat44SSE*)&ret;

		retSSE->row[0] = LinearCombineSSE( aSSE.row[0], bSSE );
		retSSE->row[1] = LinearCombineSSE( aSSE.row[1], bSSE );
		retSSE->row[2] = LinearCombineSSE( aSSE.row[2], bSSE );
		retSSE->row[3] = LinearCombineSSE( aSSE.row[3], bSSE );

		return ret;
	}
}
