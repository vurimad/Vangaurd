/*
* Copyright (c) 2016 CD Projekt Red. All Rights Reserved.
*/

#pragma once

namespace red {

//////////////////////////////////////////////////////////////////////////

template < typename TElement, typename TSortPredicate >
RED_INLINE Set< TElement, TSortPredicate >::Set( const red::memory::Pool& pool )
	: m_elements( pool )
{}

template < typename TElement, typename TSortPredicate >
RED_INLINE Set< TElement, TSortPredicate >::Set( Uint32 initialCapacity, const red::memory::Pool& pool )
	: m_elements( pool )
{
	m_elements.Reserve( initialCapacity );
}

template < typename TElement, typename TSortPredicate >
RED_INLINE Set< TElement, TSortPredicate >::Set( const Set& other )
	: m_elements( other.m_elements )
{}

template < typename TElement, typename TSortPredicate >
RED_INLINE Set< TElement, TSortPredicate >::Set( Set&& other )
	: m_elements( std::forward< ElementsType >( other.m_elements ) )
{}

template < typename TElement, typename TSortPredicate >
RED_INLINE Set< TElement, TSortPredicate >::Set( std::initializer_list< TElement > initializerList, const red::memory::Pool& pool )
	: m_elements( pool )
{
	// to preserve uniqueness
	m_elements.Reserve( static_cast< Uint32 >( initializerList.size() ) );
	for ( const TElement& element : initializerList )
	{
		Insert( element );
	}
}

template < typename TElement, typename TSortPredicate >
RED_INLINE Set< TElement, TSortPredicate >::~Set()
{}

//////////////////////////////////////////////////////////////////////////

template < typename TElement, typename TSortPredicate >
RED_INLINE Set< TElement, TSortPredicate >& Set< TElement, TSortPredicate >::operator=( const Set& other )
{
	m_elements = other.m_elements;
	return *this;
}

template < typename TElement, typename TSortPredicate >
RED_INLINE Set< TElement, TSortPredicate >& Set< TElement, TSortPredicate >::operator=( Set&& other )
{
	m_elements = std::forward< ElementsType >( other.m_elements );
	return *this;
}

template < typename TElement, typename TSortPredicate >
RED_INLINE Set< TElement, TSortPredicate >& Set< TElement, TSortPredicate >::operator=( std::initializer_list< TElement > initializerList )
{
	// to preserve uniqueness
	Set( initializerList ).Swap( *this );
	return *this;
}

template < typename TElement, typename TSortPredicate >
RED_INLINE void Set< TElement, TSortPredicate >::Swap( Set& other )
{
	m_elements.Swap( other.m_elements );
}

//////////////////////////////////////////////////////////////////////////

template < typename TElement, typename TSortPredicate >
template < typename TComparableType >
RED_INLINE const TElement& Set< TElement, TSortPredicate >::GetRef( const TComparableType& element, const TElement& defaultValue /* = TElement() */ )
{
	m_elements.MakeClean();
	SortPredicate predicate;
	iterator it = std::lower_bound( m_elements.Begin(), m_elements.End(), element, predicate );
	if ( it == m_elements.End() || predicate( element, *it ) )
	{
		it = m_elements.Insert( it, defaultValue ).Iterator();
	}
	return *it;
}

template < typename TElement, typename TSortPredicate >
RED_INLINE void Set< TElement, TSortPredicate >::GetElements( DynArray< TElement >& elements ) const
{
	elements = m_elements;
}

template < typename TElement, typename TSortPredicate >
RED_INLINE ArraySpan< const TElement > Set< TElement, TSortPredicate >::Elements() const
{
	return m_elements;
}

//////////////////////////////////////////////////////////////////////////

template < typename TElement, typename TSortPredicate >
RED_INLINE Bool Set< TElement, TSortPredicate >::operator==( const Set& other ) const
{
	return m_elements == other.m_elements;
}

template < typename TElement, typename TSortPredicate >
RED_INLINE Bool Set< TElement, TSortPredicate >::operator!=( const Set& other ) const
{
	return m_elements != other.m_elements;
}

//////////////////////////////////////////////////////////////////////////

template < typename TElement, typename TSortPredicate >
RED_INLINE typename Set< TElement, TSortPredicate >::Result Set< TElement, TSortPredicate >::Insert( const TElement& element )
{
	return m_elements.InsertUnique( element );
}

template < typename TElement, typename TSortPredicate >
RED_INLINE typename Set< TElement, TSortPredicate >::Result Set< TElement, TSortPredicate >::Insert( TElement&& element )
{
	return m_elements.InsertUnique( std::move( element ) );
}

template < typename TElement, typename TSortPredicate >
RED_INLINE void Set< TElement, TSortPredicate >::InsertUnsorted( const TElement& element )
{
	m_elements.PushBack( element );
}

template < typename TElement, typename TSortPredicate >
RED_INLINE void Set< TElement, TSortPredicate >::InsertUnsorted( TElement&& element )
{
	m_elements.PushBack( std::move( element ) );
}

template < typename TElement, typename TSortPredicate >
RED_INLINE void Set< TElement, TSortPredicate >::InsertUnsorted( const red::DynArray< TElement >& elements )
{
	m_elements.PushBack( elements );
}

template < typename TElement, typename TSortPredicate >
template < typename... Args >
RED_INLINE void Set< TElement, TSortPredicate >::EmplaceUnsorted( Args&&... args )
{
	m_elements.EmplaceBack( std::forward< Args >( args )... );
}

template < typename TElement, typename TSortPredicate >
template < typename TComparableType >
RED_INLINE typename Set< TElement, TSortPredicate >::Result Set< TElement, TSortPredicate >::Remove( const TComparableType& element )
{
	typename ElementsType::iterator it = m_elements.Find( element );
	if ( it != m_elements.End() )
	{
		return m_elements.Remove( it );
	}
	return Result::Failure();
}

template < typename TElement, typename TSortPredicate >
RED_INLINE typename Set< TElement, TSortPredicate >::Result Set< TElement, TSortPredicate >::Remove( iterator it )
{
	return m_elements.Remove( it );
}

template < typename TElement, typename TSortPredicate >
template < typename TPredicate >
RED_INLINE void Set< TElement, TSortPredicate >::RemoveIf( TPredicate predicate )
{
	typename ElementsType::iterator it = std::remove_if( m_elements.Begin(), m_elements.End(), predicate );
	m_elements.Remove( it, m_elements.End() );
}

//////////////////////////////////////////////////////////////////////////

template < typename TElement, typename TSortPredicate >
RED_INLINE void Set< TElement, TSortPredicate >::Clear()
{
	m_elements.Clear();
}

template < typename TElement, typename TSortPredicate >
RED_INLINE void Set< TElement, TSortPredicate >::Reserve( Uint32 newCapacity )
{
	m_elements.Reserve( newCapacity );
}

template < typename TElement, typename TSortPredicate >
RED_INLINE void Set< TElement, TSortPredicate >::Shrink()
{
	m_elements.Shrink();
}

//////////////////////////////////////////////////////////////////////////

template < typename TElement, typename TSortPredicate >
template < typename TComparableType >
RED_INLINE typename Set< TElement, TSortPredicate >::iterator Set< TElement, TSortPredicate >::Find( const TComparableType& element ) const
{
	return m_elements.Find( element );
}

template < typename TElement, typename TSortPredicate >
template < typename TComparableType >
RED_INLINE const TElement* Set< TElement, TSortPredicate >::FindPtr( const TComparableType& element ) const
{
	return m_elements.FindPtr( element );
}

template < typename TElement, typename TSortPredicate >
template < typename TComparableType >
RED_INLINE Bool Set< TElement, TSortPredicate >::Exist( const TComparableType& element ) const
{
	return m_elements.Exist( element );
}

//////////////////////////////////////////////////////////////////////////

template < typename TElement, typename TSortPredicate >
void Set< TElement, TSortPredicate >::Union( const Set& other )
{
	SortPredicate predicate;
	ElementsType newElements( GetPool() );
	newElements.Reserve( m_elements.Size() + other.m_elements.Size() );
	typename ElementsType::iterator it = m_elements.Begin();
	typename ElementsType::iterator itEnd = m_elements.End();
	typename ElementsType::const_iterator jt = other.m_elements.Begin();
	typename ElementsType::const_iterator jtEnd = other.m_elements.End();
	while ( it != itEnd && jt != jtEnd )
	{
		if ( predicate( *it, *jt ) )
		{
			newElements.PushBack( *it );
			it++;
		}
		else if ( predicate( *jt, *it ) )
		{
			newElements.PushBack( *jt );
			jt++;
		}
		else
		{
			newElements.PushBack( *it );
			it++;
			jt++;
		}
	}
	while ( it != itEnd )
	{
		newElements.PushBack( *it );
		it++;
	}
	while ( jt != jtEnd )
	{
		newElements.PushBack( *jt );
		jt++;
	}
	newElements.Shrink();
	m_elements.Swap( newElements );
}

template < typename TElement, typename TSortPredicate >
void Set< TElement, TSortPredicate >::Intersection( const Set& other )
{
	SortPredicate predicate;
	ElementsType newElements;
	newElements.Reserve( m_elements.Size() + other.m_elements.Size() );
	typename ElementsType::iterator it = m_elements.Begin();
	typename ElementsType::iterator itEnd = m_elements.End();
	typename ElementsType::const_iterator jt = other.m_elements.Begin();
	typename ElementsType::const_iterator jtEnd = other.m_elements.End();
	while ( it != itEnd && jt != jtEnd )
	{
		if ( predicate( *it, *jt ) )
		{
			it++;
		}
		else if ( predicate( *jt, *it ) )
		{
			jt++;
		}
		else
		{
			newElements.PushBack( *it );
			it++;
			jt++;
		}
	}
	newElements.Shrink();
	m_elements.Swap( newElements );
}

template < typename TElement, typename TSortPredicate >
void Set< TElement, TSortPredicate >::Difference( const Set& other )
{
	SortPredicate predicate;
	ElementsType newElements{ GetPool() };
	newElements.Reserve( m_elements.Size() );
	typename ElementsType::iterator it = m_elements.Begin();
	typename ElementsType::iterator itEnd = m_elements.End();
	typename ElementsType::const_iterator jt = other.m_elements.Begin();
	typename ElementsType::const_iterator jtEnd = other.m_elements.End();
	while ( it != itEnd && jt != jtEnd )
	{
		if ( predicate( *it, *jt ) )
		{
			newElements.PushBack( *it );
			it++;
		}
		else if ( predicate( *jt, *it ) )
		{
			jt++;
		}
		else
		{
			it++;
			jt++;
		}
	}
	while ( it != itEnd )
	{
		newElements.PushBack( *it );
		it++;
	}
	newElements.Shrink();
	m_elements.Swap( newElements );
}

//////////////////////////////////////////////////////////////////////////

template < typename TElement, typename TSortPredicate >
RED_INLINE void Set< TElement, TSortPredicate >::SetPool( const red::memory::Pool& pool )
{
	m_elements.SetPool( pool );
}

template < typename TElement, typename TSortPredicate >
RED_INLINE const red::memory::Pool& Set< TElement, TSortPredicate >::GetPool() const
{
	return m_elements.GetPool();
}

} // red