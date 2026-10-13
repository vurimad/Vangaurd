/*
* Copyright (c) 2017 CD Projekt Red. All Rights Reserved.
*/

namespace red
{

template< typename UnderlyingType, typename TDefaultRefCountPolicy >
RED_FORCE_INLINE void RefCount<UnderlyingType, TDefaultRefCountPolicy>::Reset( UnderlyingType count )
{
	// #tbd: assert if non-zero, but could have been called after ctor with junk value
	m_count = count;
}

template< typename UnderlyingType, typename TDefaultRefCountPolicy >
template< typename U /*= TDefaultRefCountPolicy */>
RED_FORCE_INLINE Bool RefCount<UnderlyingType, TDefaultRefCountPolicy>::Unsafe_IsZero() const
{
	return U::IsZero( m_count );
}

template< typename UnderlyingType, typename TDefaultRefCountPolicy >
template< typename U /*= TDefaultRefCountPolicy */>
RED_FORCE_INLINE void RefCount<UnderlyingType, TDefaultRefCountPolicy>::AddRef()
{
	UnderlyingType newCount = U::Increment( m_count );
	RED_FATAL_ASSERT( newCount != 0, "refcount wraparound on increment!" );
}

template< typename UnderlyingType, typename TDefaultRefCountPolicy >
template< typename U /*= TDefaultRefCountPolicy */>
RED_FORCE_INLINE Bool RefCount<UnderlyingType, TDefaultRefCountPolicy>::Release()
{
	UnderlyingType newCount = U::Decrement( m_count );
	RED_FATAL_ASSERT( newCount != c_maxValue, "refcount wraparound on release!" );
	return newCount == 0;
}

}
