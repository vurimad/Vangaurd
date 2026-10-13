/**
 * Copyright (c) 2008-16 CD Projekt Red. All Rights Reserved.
 */
#pragma once

#include <utility>
#include "../../../common/redSystem/include/typetraits.h"

namespace red {

namespace alg
{

//////////////////////////////////////////////////////////////////////////
// SortPredicate(s)

template < typename T1, typename T2, typename TSortPredicate = std::less< T1 > >
struct PairSortByKeyPredicate
{
	RED_INLINE Bool operator()( const std::pair< T1, T2 >& p1, const std::pair< T1, T2 >& p2 ) const
	{
		return m_predicate( p1.first, p2.first );
	}
private:
	TSortPredicate m_predicate;
};

template < typename T1, typename T2, typename TSortPredicate = std::less< T2 > >
struct PairSortByValuePredicate
{
	RED_INLINE Bool operator()( const std::pair< T1, T2 >& p1, const std::pair< T1, T2 >& p2 ) const
	{
		return m_predicate( p1.second, p2.second );
	}
private:
	TSortPredicate m_predicate;
};

template < typename T1, typename T2, typename TKeySortPredicate = std::less< T1 >, typename TValueSortPredicate = std::less< T2 > >
struct PairSortPredicate
{
	RED_INLINE Bool operator()( const std::pair< T1, T2 >& p1, const std::pair< T1, T2 >& p2 ) const
	{
		if ( m_keyPredicate( p1.first, p2.first ) )
		{
			return true;
		}
		if ( m_keyPredicate( p2.first, p1.first ) )
		{
			return false;
		}
		// pairs are equal on "first"
		return m_valuePredicate( p1.second, p2.second );
	}
private:
	TKeySortPredicate	m_keyPredicate;
	TValueSortPredicate	m_valuePredicate;
};

//////////////////////////////////////////////////////////////////////////
// EqualFunc(s)

template < typename T1, typename T2, typename TEqualFunc = DefaultEqualFunc< T1 > >
struct PairEqualKeyFunc
{
	static RED_INLINE Bool Equal( const std::pair< T1, T2 >& p1, const std::pair< T1, T2 >& p2 )
	{
		return TEqualFunc::Equal( p1.first, p2.first );
	}
};

template < typename T1, typename T2, typename TEqualFunc = DefaultEqualFunc< T2 > >
struct PairEqualValueFunc
{
	static RED_INLINE Bool Equal( const std::pair< T1, T2 >& p1, const std::pair< T1, T2 >& p2 )
	{
		return TEqualFunc::Equal( p1.second, p2.second );
	}
};

template < typename T1, typename T2, typename TKeyEqualFunc = DefaultEqualFunc< T1 >, typename TValueEqualFunc = DefaultEqualFunc< T2 > >
struct PairEqualFunc
{
	static RED_INLINE Bool Equal( const std::pair< T1, T2 >& p1, const std::pair< T1, T2 >& p2 )
	{
		return TKeyEqualFunc::Equal( p1.first, p2.first ) && TValueEqualFunc::Equal( p1.second, p2.second );
	}
};

} // alg
} // red

template < typename T1, typename T2 > struct TCopyableType< std::pair< T1, T2 > >
{
	enum { Value = TCopyableType< T1 >::Value && TCopyableType< T2 >::Value };
};
