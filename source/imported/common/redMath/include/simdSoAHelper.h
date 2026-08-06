#pragma once

namespace simd
{
	struct Vector4SoA
	{
		__m128 xxxx;
		__m128 yyyy;
		__m128 zzzz;
		__m128 wwww;
	};
	RED_INLINE void Load( Vector4SoA* soa, const math::Vector4& a, const math::Vector4& b, const math::Vector4& c, const math::Vector4& d )
	{
		/*
		xyzw xyzw xyzw xyzw
		tmp0 = ax|ay|bx|by
		tmp1 = cx|cy|dx|dy
		tmp2 = az|aw|bz|bw
		tmp3 = cz|cw|dz|dw
		ax|bx|cx|dx
		ay|by|cy|dy
		az|bz|cz|dz
		aw|bw|cw|dw
		*/

		const __m128& av = a.vec;
		const __m128& bv = b.vec;
		const __m128& cv = c.vec;
		const __m128& dv = d.vec;

		const __m128 tmp0 = _mm_shuffle_ps( av, bv, _MM_SHUFFLE( 1, 0, 1, 0 ) );
		const __m128 tmp1 = _mm_shuffle_ps( cv, dv, _MM_SHUFFLE( 1, 0, 1, 0 ) );
		const __m128 tmp2 = _mm_shuffle_ps( av, bv, _MM_SHUFFLE( 3, 2, 3, 2 ) );
		const __m128 tmp3 = _mm_shuffle_ps( cv, dv, _MM_SHUFFLE( 3, 2, 3, 2 ) );

		soa->xxxx = _mm_shuffle_ps( tmp0, tmp1, _MM_SHUFFLE( 2, 0, 2, 0 ) );
		soa->yyyy = _mm_shuffle_ps( tmp0, tmp1, _MM_SHUFFLE( 3, 1, 3, 1 ) );
		soa->zzzz = _mm_shuffle_ps( tmp2, tmp3, _MM_SHUFFLE( 2, 0, 2, 0 ) );
		soa->wwww = _mm_shuffle_ps( tmp2, tmp3, _MM_SHUFFLE( 3, 1, 3, 1 ) );
	}
	RED_INLINE void Store( math::Vector4* a, math::Vector4* b, math::Vector4* c, math::Vector4* d, const Vector4SoA& soa )
	{
		/*
		ax|bx|cx|dx
		ay|by|cy|dy
		az|bz|cz|dz
		aw|bw|cw|dw
		tmp0 = ax|bx|ay|by
		tmp1 = cx|dx|cy|dy
		tmp2 = az|bz|aw|bw
		tmp3 = cz|dz|cw|dw
		xyzw xyzw xyzw xyzw
		*/

		const __m128 tmp0 = _mm_shuffle_ps( soa.xxxx, soa.yyyy, _MM_SHUFFLE( 1, 0, 1, 0 ) );
		const __m128 tmp1 = _mm_shuffle_ps( soa.xxxx, soa.yyyy, _MM_SHUFFLE( 3, 2, 3, 2 ) );
		const __m128 tmp2 = _mm_shuffle_ps( soa.zzzz, soa.wwww, _MM_SHUFFLE( 1, 0, 1, 0 ) );
		const __m128 tmp3 = _mm_shuffle_ps( soa.zzzz, soa.wwww, _MM_SHUFFLE( 3, 2, 3, 2 ) );

		a->vec = _mm_shuffle_ps( tmp0, tmp2, _MM_SHUFFLE( 2, 0, 2, 0 ) );
		b->vec = _mm_shuffle_ps( tmp0, tmp2, _MM_SHUFFLE( 3, 1, 3, 1 ) );
		c->vec = _mm_shuffle_ps( tmp1, tmp3, _MM_SHUFFLE( 2, 0, 2, 0 ) );
		d->vec = _mm_shuffle_ps( tmp1, tmp3, _MM_SHUFFLE( 3, 1, 3, 1 ) );
	}


	struct MatrixSoA
	{
		Vector4SoA x;
		Vector4SoA y;
		Vector4SoA z;
		Vector4SoA w;
	};
	RED_INLINE void Load( MatrixSoA* soa, const Matrix& a, const Matrix& b, const Matrix& c, const Matrix& d )
	{
		Load( &soa->x, a.X, b.X, c.X, d.X );
		Load( &soa->y, a.Y, b.Y, c.Y, d.Y );
		Load( &soa->z, a.Z, b.Z, c.Z, d.Z );
		Load( &soa->w, a.W, b.W, c.W, d.W );
	}
	RED_INLINE void Store( Matrix* a, Matrix* b, Matrix* c, Matrix* d, const MatrixSoA& soa )
	{
		Store( &a->X, &b->X, &c->X, &d->X, soa.x );
		Store( &a->Y, &b->Y, &c->Y, &d->Y, soa.y );
		Store( &a->Z, &b->Z, &c->Z, &d->Z, soa.z );
		Store( &a->W, &b->W, &c->W, &d->W, soa.w );
	}

	RED_INLINE Vector4SoA Mul( const MatrixSoA& m, const Vector4SoA& v )
	{
		Vector4SoA result;
		result.xxxx = _mm_add_ps( _mm_add_ps( _mm_add_ps( _mm_mul_ps( m.x.xxxx, v.xxxx ), _mm_mul_ps( m.y.xxxx, v.yyyy ) ), _mm_mul_ps( m.z.xxxx, v.zzzz ) ), _mm_mul_ps( m.w.xxxx, v.wwww ) );
		result.yyyy = _mm_add_ps( _mm_add_ps( _mm_add_ps( _mm_mul_ps( m.x.yyyy, v.xxxx ), _mm_mul_ps( m.y.yyyy, v.yyyy ) ), _mm_mul_ps( m.z.yyyy, v.zzzz ) ), _mm_mul_ps( m.w.yyyy, v.wwww ) );
		result.zzzz = _mm_add_ps( _mm_add_ps( _mm_add_ps( _mm_mul_ps( m.x.zzzz, v.xxxx ), _mm_mul_ps( m.y.zzzz, v.yyyy ) ), _mm_mul_ps( m.z.zzzz, v.zzzz ) ), _mm_mul_ps( m.w.zzzz, v.wwww ) );
		result.wwww = _mm_add_ps( _mm_add_ps( _mm_add_ps( _mm_mul_ps( m.x.wwww, v.xxxx ), _mm_mul_ps( m.y.wwww, v.yyyy ) ), _mm_mul_ps( m.z.wwww, v.zzzz ) ), _mm_mul_ps( m.w.wwww, v.wwww ) );

		return result;
	}

	RED_INLINE MatrixSoA Mul( const MatrixSoA& a, const MatrixSoA& b )
	{
		MatrixSoA result;
		result.x = Mul( b, a.x );
		result.y = Mul( b, a.y );
		result.z = Mul( b, a.z );
		result.w = Mul( b, a.w );

		return result;
	}
}//
