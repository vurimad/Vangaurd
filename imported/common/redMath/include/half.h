/**
* Copyright (c) 2026 RED Vanguard, All Rights Reserved.
*/

#pragma once

#include "float16compressor.h"

namespace math
{

	// 16-bit float
	struct Half
	{
		union
		{
			Uint16 value;

			struct
			{
				Uint16 mantissa : 10;
				Uint16 exponent : 5;
				Uint16 sign : 1;
			} components;
		};

		Half() = default;

		RED_FORCE_INLINE Half( const Half& other ) : value( other.value ) {}
		RED_FORCE_INLINE explicit Half( const Uint16 other ) : value( other ) {}
		RED_FORCE_INLINE static const Half Zero() { return Half( static_cast<Uint16>( 0 ) ); }

		RED_FORCE_INLINE Half& operator = ( const Half& other )
		{
			value = other.value;
			return *this;
		}

		RED_FORCE_INLINE explicit Half( const float x )
		{
			value = Float16Compressor::Compress( x );
		}

		RED_FORCE_INLINE Float ToFloat() const
		{
			return Float16Compressor::Decompress( value );
		}

		RED_FORCE_INLINE bool operator == ( const Half& other ) const
		{
			return value == other.value;
		}
	};

	// 2-element 16-bit float vector
	struct Half2
	{
		Half X, Y;

		Half2() = default;
		Half2( const Half2& ) = default;
		Half2& operator = ( const Half2& ) = default;

		RED_FORCE_INLINE Half2( Half x, Half y ) : X( x ), Y( y ) {}

		RED_FORCE_INLINE Half2( const Vector2 vec )
			: X( vec.X )
			, Y( vec.Y )
		{}

		RED_FORCE_INLINE bool operator == ( const Half2& other ) const
		{
			return X == other.X && Y == other.Y;
		}
	};

	// 3-element 16-bit float vector
	struct Half3
	{
		Half X, Y, Z;

		Half3() = default;
		Half3( const Half3& ) = default;
		Half3& operator = ( const Half3& ) = default;

		RED_FORCE_INLINE Half3( Half x, Half y, Half z ) : X( x ), Y( y ), Z(z) {}

		RED_FORCE_INLINE Half3( const Vector3 vec )
			: X( vec.X )
			, Y( vec.Y )
			, Z( vec.Z )
		{}

		RED_FORCE_INLINE bool operator == ( const Half3& other ) const
		{
			return X == other.X && Y == other.Y && Z == other.Z;
		}
	};

	// 4-element 16-bit float vector
	struct Half4
	{
		Half X, Y, Z, W;

		Half4() = default;
		Half4( const Half4& ) = default;
		Half4& operator = ( const Half4& ) = default;

		RED_FORCE_INLINE Half4( Half x, Half y, Half z, Half w ) : X( x ), Y( y ), Z( z ), W(w) {}

		RED_FORCE_INLINE Half4( const Vector4 vec )
		{
			const __m128i halfs = Float16Compressor::CompressSSE( vec.vec );
			_mm_storel_epi64( ( __m128i* )&X, halfs );
		}

		RED_FORCE_INLINE bool operator == ( const Half4& other ) const
		{
			return X == other.X && Y == other.Y && Z == other.Z && W == other.W;
		}

		RED_FORCE_INLINE const Vector4 ToVector4() const
		{
			return Vector4{ X.ToFloat(), Y.ToFloat(), Z.ToFloat(), W.ToFloat() };
		}
	};

	static_assert( sizeof( Half ) == 2, "Invalid math::Half size" );
	static_assert( sizeof( Half2 ) == 4, "Invalid math::Half size" );
	static_assert( sizeof( Half3 ) == 6, "Invalid math::Half size" );
	static_assert( sizeof( Half4 ) == 8, "Invalid math::Half size" );

} // math