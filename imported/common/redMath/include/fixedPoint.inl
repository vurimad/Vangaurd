#pragma once

namespace math
{
	template< typename Base, Uint32 SHIFT >
	RED_INLINE FixedPoint<Base, SHIFT>::FixedPoint()
	{}
		
	template< typename Base, Uint32 SHIFT >
	constexpr FixedPoint<Base, SHIFT>::FixedPoint( Base value )
		: m_bits( value )
	{}

	template< typename Base, Uint32 SHIFT >
	constexpr FixedPoint<Base, SHIFT>::FixedPoint( const FixedPoint& value )
		: m_bits( value.m_bits )
	{}

	template< typename Base, Uint32 SHIFT >
	RED_INLINE FixedPoint<Base, SHIFT>::FixedPoint( Float value )
	{
		Float tmp = ((Float)value * ONE);
		tmp += (tmp >= 0.f) ? 0.5f : -0.5f;
		m_bits = (Base)tmp;
	}

	template< typename Base, Uint32 SHIFT >
	RED_INLINE FixedPoint<Base, SHIFT>::FixedPoint( Double value )
	{
		Double tmp = ((Double)value * ONE);
		tmp += (tmp >= 0.0) ? 0.5 : -0.5;
		m_bits = (Base)tmp;
	}

	template< typename Base, Uint32 SHIFT >
	RED_FORCE_INLINE Float FixedPoint<Base, SHIFT>::AsFloat()
	{
		return m_bits * ONE_RCP();
	}

	template< typename Base, Uint32 SHIFT >
	RED_FORCE_INLINE Float FixedPoint<Base, SHIFT>::AsFloat() const
	{
		return m_bits * ONE_RCP();
	}

	template< typename Base, Uint32 SHIFT >
	RED_INLINE Double FixedPoint<Base, SHIFT>::AsDouble()
	{
		return m_bits * (double)ONE_RCP();
	}

	template< typename Base, Uint32 SHIFT >
	RED_INLINE Double FixedPoint<Base, SHIFT>::AsDouble() const
	{
		return m_bits * (double)ONE_RCP();
	}

	template< typename Base, Uint32 SHIFT >
	RED_FORCE_INLINE FixedPoint<Base, SHIFT>& FixedPoint<Base, SHIFT>::operator+=( const FixedPoint& v )
	{
		m_bits += v.m_bits;
		return *this;
	}

	template< typename Base, Uint32 SHIFT >
	RED_FORCE_INLINE FixedPoint<Base, SHIFT>& FixedPoint<Base, SHIFT>::operator+=( Float v )
	{
		*this += FixedPoint( v );
		return *this;
	}

	template< typename Base, Uint32 SHIFT >
	RED_FORCE_INLINE FixedPoint<Base, SHIFT>& FixedPoint<Base, SHIFT>::operator-=( const FixedPoint& v )
	{
		m_bits -= v.m_bits;
		return *this;
	}

	template< typename Base, Uint32 SHIFT >
	RED_FORCE_INLINE FixedPoint<Base, SHIFT>& FixedPoint<Base, SHIFT>::operator-=( Float v )
	{
		*this -= FixedPoint( v );
		return *this;
	}

	template< typename Base, Uint32 SHIFT >
	RED_FORCE_INLINE FixedPoint<Base, SHIFT> FixedPoint<Base, SHIFT>::operator+( const FixedPoint& v ) const
	{
		return FixedPoint( m_bits + v.m_bits );
	}
	template< typename Base, Uint32 SHIFT >
	RED_FORCE_INLINE FixedPoint<Base, SHIFT> FixedPoint<Base, SHIFT>::operator-( const FixedPoint& v ) const
	{
		return FixedPoint( m_bits - v.m_bits );
	}
	template< typename Base, Uint32 SHIFT >
	RED_FORCE_INLINE FixedPoint<Base, SHIFT> FixedPoint<Base, SHIFT>::operator+( Float v ) const
	{
		return *this + FixedPoint( v );
	}
	template< typename Base, Uint32 SHIFT >
	RED_FORCE_INLINE FixedPoint<Base, SHIFT> FixedPoint<Base, SHIFT>::operator-( Float v ) const
	{
		return *this - FixedPoint( v );
	}
	template< typename Base, Uint32 SHIFT >
	RED_FORCE_INLINE FixedPoint<Base, SHIFT> FixedPoint<Base, SHIFT>::operator+( Double v ) const
	{
		return *this + FixedPoint( v );
	}
	
	template< typename Base, Uint32 SHIFT >
	RED_FORCE_INLINE FixedPoint<Base, SHIFT> FixedPoint<Base, SHIFT>::operator-( Double v ) const
	{
		return *this - FixedPoint( v );
	}

	template< typename Base, Uint32 SHIFT >
	RED_FORCE_INLINE Bool FixedPoint<Base, SHIFT>::operator==( const FixedPoint& v ) const
	{
		return m_bits == v.m_bits;
	}
	template< typename Base, Uint32 SHIFT >
	RED_FORCE_INLINE FixedPoint<Base, SHIFT> FixedPoint<Base, SHIFT>::operator-() const
	{
		return FixedPoint( -m_bits );
	}

	template< typename Base, Uint32 SHIFT >
	RED_FORCE_INLINE Bool FixedPoint<Base, SHIFT>::operator< ( const FixedPoint& v ) const
	{
		return m_bits < v.m_bits;
	}

	template< typename Base, Uint32 SHIFT >
	RED_FORCE_INLINE Bool FixedPoint<Base, SHIFT>::operator<= ( const FixedPoint& v ) const
	{
		return m_bits <= v.m_bits;
	}

	template< typename Base, Uint32 SHIFT >
	RED_FORCE_INLINE Bool FixedPoint<Base, SHIFT>::operator> ( const FixedPoint& v ) const
	{
		return m_bits > v.m_bits;
	}

	template< typename Base, Uint32 SHIFT >
	RED_FORCE_INLINE Bool FixedPoint<Base, SHIFT>::operator>= ( const FixedPoint& v ) const
	{
		return m_bits >= v.m_bits;
	}
}//
