/**
* Copyright (c) 2017 CD Projekt Red. All Rights Reserved.
*/
#pragma once

namespace math
{
	// Base - underlying type. Must be integer and BITCOUNT > SHIFT
	template< typename Base, Uint32 SHIFT >
	struct FixedPoint
	{
		using BaseType = Base;
		static constexpr Base BITCOUNT = sizeof( Base ) * 8;
		static_assert(BITCOUNT > SHIFT, "BITCOUNT must be greater than SHIFT");
		static constexpr Base ONE = Base( 1 ) << SHIFT;
		static RED_INLINE constexpr float ONE_RCP() { return (float)(1.f / float( ONE )); }

		Base m_bits = Base(0);

		// ---
		RED_INLINE FixedPoint();
		constexpr FixedPoint( Base value );
		constexpr FixedPoint( const FixedPoint& value );

		RED_INLINE FixedPoint( Float value );
		RED_INLINE FixedPoint( Double value );

		RED_INLINE Float  AsFloat();
		RED_INLINE Float  AsFloat() const;
		RED_INLINE Double AsDouble();
		RED_INLINE Double AsDouble() const;

		RED_FORCE_INLINE FixedPoint& operator += ( const FixedPoint& v );
		RED_FORCE_INLINE FixedPoint& operator += ( Float v );
		RED_FORCE_INLINE FixedPoint& operator -= ( const FixedPoint& v );
		RED_FORCE_INLINE FixedPoint& operator -= ( Float v );

		RED_FORCE_INLINE FixedPoint operator + ( const FixedPoint& v ) const;
		RED_FORCE_INLINE FixedPoint operator - ( const FixedPoint& v ) const;
		RED_FORCE_INLINE FixedPoint operator + ( Float v )  const;
		RED_FORCE_INLINE FixedPoint operator - ( Float v )  const;
		RED_FORCE_INLINE FixedPoint operator + ( Double v ) const;
		RED_FORCE_INLINE FixedPoint operator - ( Double v ) const;

		RED_FORCE_INLINE Bool		 operator ==( const FixedPoint& v ) const;
		RED_FORCE_INLINE FixedPoint operator - () const;

		RED_FORCE_INLINE Bool		 operator <  ( const FixedPoint& v ) const;
		RED_FORCE_INLINE Bool		 operator <= ( const FixedPoint& v ) const;
		RED_FORCE_INLINE Bool		 operator >  ( const FixedPoint& v ) const;
		RED_FORCE_INLINE Bool		 operator >= ( const FixedPoint& v ) const;

		static constexpr FixedPoint ZERO() { return FixedPoint( Base( 0 ) ); }
	};
}

#include "fixedPoint.inl"