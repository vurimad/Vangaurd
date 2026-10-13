/*
* Copyright (c) 2016 CD Projekt Red. All Rights Reserved.
*/

#pragma once

namespace red {

//////////////////////////////////////////////////////////////////////////

template < typename TElement >
RED_INLINE ArrayConstIterator< TElement >::ArrayConstIterator()
	: m_ptr( nullptr )
{}

template < typename TElement >
RED_INLINE ArrayConstIterator< TElement >::ArrayConstIterator( const ArrayConstIterator& it )
	: m_ptr( it.m_ptr )
{}

template < typename TElement >
RED_INLINE ArrayConstIterator< TElement >::ArrayConstIterator( const ElementType* ptr )
	: m_ptr( const_cast< ElementType* >( ptr ) )
{}

template < typename TElement >
RED_INLINE ArrayConstIterator< TElement >::operator typename ArrayConstIterator< TElement >::bool_operator() const
{ 
	return m_ptr != nullptr ? &BoolConversion::valid : 0;
}

template < typename TElement >
RED_INLINE Bool ArrayConstIterator< TElement >::operator!() const
{
	return m_ptr == nullptr;
}

template < typename TElement >
RED_INLINE typename ArrayConstIterator< TElement >::RefType ArrayConstIterator< TElement >::operator*() const
{
	return *this->m_ptr;
}

template < typename TElement >
RED_INLINE typename ArrayConstIterator< TElement >::PtrType ArrayConstIterator< TElement >::operator->() const
{
	return this->m_ptr;
}

template < typename TElement >
RED_INLINE typename ArrayConstIterator< TElement >::RefType ArrayConstIterator< TElement >::operator[]( DiffType count ) const
{
	return this->m_ptr[ count ];
}

template < typename TElement >
RED_INLINE ArrayConstIterator< TElement >& ArrayConstIterator< TElement >::operator++()
{
	this->m_ptr++;
	return *this;
}

template < typename TElement >
RED_INLINE ArrayConstIterator< TElement > ArrayConstIterator< TElement >::operator++(int)
{
	return ArrayConstIterator( this->m_ptr++ );
}

template < typename TElement >
RED_INLINE ArrayConstIterator< TElement >& ArrayConstIterator< TElement >::operator--()
{
	this->m_ptr--;
	return *this;
}

template < typename TElement >
RED_INLINE ArrayConstIterator< TElement > ArrayConstIterator< TElement >::operator--(int)
{
	return ArrayConstIterator( this->m_ptr-- );
}

template < typename TElement >
RED_INLINE ArrayConstIterator< TElement > ArrayConstIterator< TElement >::operator+( DiffType count ) const
{
	return ArrayConstIterator( this->m_ptr + count );
}

template < typename TElement >
RED_INLINE ArrayConstIterator< TElement > ArrayConstIterator< TElement >::operator-( DiffType count ) const
{
	return ArrayConstIterator( this->m_ptr - count );
}

template < typename TElement >
RED_INLINE ArrayConstIterator< TElement > operator+( typename ArrayConstIterator< TElement >::DiffType count, const ArrayConstIterator< TElement >& iter )
{
	return ArrayConstIterator< TElement >( iter.m_ptr + count );
}

template < typename TElement >
RED_INLINE ArrayConstIterator< TElement >& ArrayConstIterator< TElement >::operator+=( DiffType count )
{
	this->m_ptr += count;
	return *this;
}

template < typename TElement >
RED_INLINE ArrayConstIterator< TElement >& ArrayConstIterator< TElement >::operator-=( DiffType count )
{
	this->m_ptr -= count;
	return *this;
}

template < typename TElement >
RED_INLINE typename ArrayConstIterator< TElement >::DiffType ArrayConstIterator< TElement >::operator-( const ArrayConstIterator& it ) const
{
	return ArrayIteratorImpl::InternalDistance( it, *this );
}

template < typename TElement >
RED_INLINE Bool ArrayConstIterator< TElement >::operator==( const ArrayConstIterator& it ) const
{
	return this->m_ptr == it.m_ptr;
}

template < typename TElement >
RED_INLINE Bool ArrayConstIterator< TElement >::operator!=( const ArrayConstIterator& it ) const
{
	return this->m_ptr != it.m_ptr;
}

template < typename TElement >
RED_INLINE Bool ArrayConstIterator< TElement >::operator<( const ArrayConstIterator& it ) const
{
	return this->m_ptr < it.m_ptr;
}

template < typename TElement >
RED_INLINE Bool ArrayConstIterator< TElement >::operator<=( const ArrayConstIterator& it ) const
{
	return this->m_ptr <= it.m_ptr;
}

template < typename TElement >
RED_INLINE Bool ArrayConstIterator< TElement >::operator>( const ArrayConstIterator& it ) const
{
	return this->m_ptr > it.m_ptr;
}

template < typename TElement >
RED_INLINE Bool ArrayConstIterator< TElement >::operator>=( const ArrayConstIterator& it ) const
{
	return this->m_ptr >= it.m_ptr;
}

//////////////////////////////////////////////////////////////////////////

template < typename TElement >
RED_INLINE ArrayIterator< TElement >::ArrayIterator()
{}

template < typename TElement >
RED_INLINE ArrayIterator< TElement >::ArrayIterator( const ArrayIterator& it )
	: BaseClass( it )
{}

template < typename TElement >
RED_INLINE ArrayIterator< TElement >::ArrayIterator( ElementType* ptr )
	: BaseClass( ptr )
{}

template < typename TElement >
RED_INLINE typename ArrayIterator< TElement >::RefType ArrayIterator< TElement >::operator*() const
{
	return *this->m_ptr;
}

template < typename TElement >
RED_INLINE typename ArrayIterator< TElement >::PtrType ArrayIterator< TElement >::operator->() const
{
	return this->m_ptr;
}

template < typename TElement >
RED_INLINE typename ArrayIterator< TElement >::RefType ArrayIterator< TElement >::operator[]( DiffType count ) const
{
	return this->m_ptr[ count ];
}

template < typename TElement >
RED_INLINE ArrayIterator< TElement >& ArrayIterator< TElement >::operator++()
{
	this->m_ptr++;
	return *this;
}

template < typename TElement >
RED_INLINE ArrayIterator< TElement > ArrayIterator< TElement >::operator++(int)
{
	return ArrayIterator( this->m_ptr++ );
}

template < typename TElement >
RED_INLINE ArrayIterator< TElement >& ArrayIterator< TElement >::operator--()
{
	this->m_ptr--;
	return *this;
}

template < typename TElement >
RED_INLINE ArrayIterator< TElement > ArrayIterator< TElement >::operator--(int)
{
	return ArrayIterator( this->m_ptr-- );
}

template < typename TElement >
RED_INLINE ArrayIterator< TElement > ArrayIterator< TElement >::operator+( DiffType count ) const
{
	return ArrayIterator( this->m_ptr + count );
}

template < typename TElement >
RED_INLINE ArrayIterator< TElement > ArrayIterator< TElement >::operator-( DiffType count ) const
{
	return ArrayIterator( this->m_ptr - count );
}

template < typename TElement >
RED_INLINE ArrayIterator< TElement > operator+( typename ArrayIterator< TElement >::DiffType count, const ArrayIterator< TElement >& iter )
{
	return ArrayIterator< TElement >( iter.m_ptr + count );
}

template < typename TElement >
RED_INLINE ArrayIterator< TElement >& ArrayIterator< TElement >::operator+=( DiffType count )
{
	this->m_ptr += count;
	return *this;
}

template < typename TElement >
RED_INLINE ArrayIterator< TElement >& ArrayIterator< TElement >::operator-=( DiffType count )
{
	this->m_ptr -= count;
	return *this;
}

template < typename TElement >
RED_INLINE typename ArrayIterator< TElement >::DiffType ArrayIterator< TElement >::operator-( const ArrayIterator& it ) const
{
	return ArrayIteratorImpl::InternalDistance( it, *this );
}

template < typename TElement >
RED_INLINE typename ArrayIterator< TElement >::DiffType ArrayIterator< TElement >::operator-( const ArrayConstIterator< TElement >& it ) const
{
	return ArrayIteratorImpl::InternalDistance< ArrayConstIterator< TElement > >( it, *this );
}

//////////////////////////////////////////////////////////////////////////

template < typename TIterator >
RED_INLINE typename TIterator::DiffType ArrayIteratorImpl::InternalDistance( const TIterator& from, const TIterator& to )
{
	typedef typename TIterator::DiffType DiffType;
	const ptrdiff_t diff = to.m_ptr - from.m_ptr;
	RED_ASSERT( diff >= std::numeric_limits< DiffType >::lowest() && diff <= std::numeric_limits< DiffType >::max(), "Iterators distance exceeds iterator DiffType range." );
	return static_cast< DiffType >( diff );
}

template < typename TPtrDiff, typename TElement >
RED_INLINE TPtrDiff Distance( ArrayConstIterator< TElement > begin, ArrayConstIterator< TElement > end )
{
	const typename ArrayConstIterator< TElement >::DiffType diff = end - begin;
	RED_SYSTEM_ASSERT( diff >= std::numeric_limits< TPtrDiff >::lowest() && diff <= std::numeric_limits< TPtrDiff >::max(), "Distance cannot store value in specified type." );
	return static_cast< TPtrDiff >( diff );
}

} // red