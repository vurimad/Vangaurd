/*
 * Copyright (c) 2016 CD Projekt Red. All Rights Reserved.
 */

#pragma once

#if defined( RED_CHECKED_ITERATORS )

#define CHECKED_FATAL_ASSERT( cond, ... ) RED_FATAL_ASSERT( cond, ##__VA_ARGS__ )
#define CHECKED_VALIDATE_ITERATOR( iter ) \
	do { \
		RED_FATAL_ASSERT( ( iter ).m_span != nullptr, "Invalid span for span iterator!" ); \
		RED_FATAL_ASSERT( ( iter ).m_span->m_startPtr <= ( iter ).m_ptr && ( iter ).m_ptr <= ( iter ).m_span->m_endPtr, \
			"Pointer outside the range of the span! m_span = 0x%p, m_ptr = 0x%p, m_startPtr = 0x%p, m_endPtr = 0x%p", \
			( iter ).m_span, ( iter ).m_ptr, ( iter ).m_span->m_startPtr, ( iter ).m_span->m_endPtr ); \
	} while ( false )
#define CHECKED_SPAN( span ) span

#else // ! defined( RED_CHECKED_ITERATORS )

#define CHECKED_FATAL_ASSERT( ... )
#define CHECKED_VALIDATE_ITERATOR( iter ) 
#define CHECKED_SPAN( span ) nullptr

#endif

namespace red
{
template <typename Span>
constexpr ArraySpanIterator< Span >::ArraySpanIterator()
	: m_ptr( nullptr )
#if defined( RED_CHECKED_ITERATORS )
	, m_span( nullptr )
#endif
{}

template <typename Span>
constexpr ArraySpanIterator< Span >::ArraySpanIterator( const ArraySpanIterator& iter )
	: m_ptr( iter.m_ptr )
#if defined( RED_CHECKED_ITERATORS )
	, m_span( iter.m_span )
#endif
{}

template <typename Span>
constexpr ArraySpanIterator< Span >::ArraySpanIterator( TElement* ptr, const Span* span )
	: m_ptr( ptr )
#if defined( RED_CHECKED_ITERATORS )
	, m_span( span )
#endif
{
	RED_TOUCH( span );
}

template <typename Span>
RED_INLINE ArraySpanIterator< Span >& ArraySpanIterator< Span >::operator=( const ArraySpanIterator& other )
{
	m_ptr = other.m_ptr;
#if defined( RED_CHECKED_ITERATORS )
	m_span = other.m_span;
#endif
	return *this;
}

template <typename Span>
RED_INLINE typename ArraySpanIterator< Span >::RefType ArraySpanIterator< Span >::operator*() const
{
	CHECKED_VALIDATE_ITERATOR( *this );
	CHECKED_FATAL_ASSERT( m_ptr < m_span->m_endPtr,
		"Attempting to access past the end of the span! m_span = 0x%p, m_ptr = 0x%p, m_startPtr = 0x%p, m_endPtr = 0x%p",
		m_span, m_ptr, m_span->m_startPtr, m_span->m_endPtr );

	return *m_ptr;
}

template <typename Span>
RED_INLINE typename ArraySpanIterator< Span >::PtrType ArraySpanIterator< Span >::operator->() const
{
	CHECKED_VALIDATE_ITERATOR( *this );
	CHECKED_FATAL_ASSERT( m_ptr < m_span->m_endPtr,
		"Attempting to access past the end of the span! m_span = 0x%p, m_ptr = 0x%p, m_startPtr = 0x%p, m_endPtr = 0x%p",
		m_span, m_ptr, m_span->m_startPtr, m_span->m_endPtr );

	return m_ptr;
}

template <typename Span>
RED_INLINE typename ArraySpanIterator< Span >::RefType ArraySpanIterator< Span >::operator[]( DiffType count ) const
{
	CHECKED_VALIDATE_ITERATOR( *this );
	CHECKED_FATAL_ASSERT( m_ptr + count < m_span->m_endPtr,
		"Attempting to access past the end of the span! m_span = 0x%p, m_ptr = 0x%p, m_startPtr = 0x%p, m_endPtr = 0x%p, count = %lli",
		m_span, m_ptr, m_span->m_startPtr, m_span->m_endPtr, count );

	return *(m_ptr + count);
}

template <typename Span>
RED_INLINE ArraySpanIterator< Span >& ArraySpanIterator< Span >::operator++()
{
	CHECKED_VALIDATE_ITERATOR( *this );
	CHECKED_FATAL_ASSERT( m_ptr < m_span->m_endPtr,
		"Attempting to incremenet iterator past the end of the span! m_span = 0x%p, m_ptr = 0x%p, m_startPtr = 0x%p, m_endPtr = 0x%p",
		m_span, m_ptr, m_span->m_startPtr, m_span->m_endPtr );

	++m_ptr;

	return *this;
}

template <typename Span>
RED_INLINE ArraySpanIterator< Span > ArraySpanIterator< Span >::operator++(int)
{
	CHECKED_VALIDATE_ITERATOR( *this );
	CHECKED_FATAL_ASSERT( m_ptr < m_span->m_endPtr,
		"Attempting to increment the iterator past the end of the span! m_span = 0x%p, m_ptr = 0x%p, m_startPtr = 0x%p, m_endPtr = 0x%p",
		m_span, m_ptr, m_span->m_startPtr, m_span->m_endPtr );

	ArraySpanIterator iter( *this );
	++m_ptr;

	return iter;
}

template <typename Span>
RED_INLINE ArraySpanIterator< Span >& ArraySpanIterator< Span >::operator--()
{
	CHECKED_VALIDATE_ITERATOR( *this );
	CHECKED_FATAL_ASSERT( m_span->m_startPtr < m_ptr,
		"Attempting to decrement the iterator past the start of the span! m_span = 0x%p, m_ptr = 0x%p, m_startPtr = 0x%p, m_endPtr = 0x%p",
		m_span, m_ptr, m_span->m_startPtr, m_span->m_endPtr );

	--m_ptr;

	return *this;
}

template <typename Span>
RED_INLINE ArraySpanIterator< Span > ArraySpanIterator< Span >::operator--(int)
{
	CHECKED_VALIDATE_ITERATOR( *this );
	CHECKED_FATAL_ASSERT( m_span->m_startPtr < m_ptr,
		"Attempting to decrement the iterator past the start of the span! m_span = 0x%p, m_ptr = 0x%p, m_startPtr = 0x%p, m_endPtr = 0x%p",
		m_span, m_ptr, m_span->m_startPtr, m_span->m_endPtr );

	ArraySpanIterator iter( *this );
	--m_ptr;

	return iter;
}

template <typename Span>
RED_INLINE ArraySpanIterator< Span > ArraySpanIterator< Span >::operator+( DiffType count ) const
{
	CHECKED_VALIDATE_ITERATOR( *this );
	CHECKED_FATAL_ASSERT( m_ptr + count <= m_span->m_endPtr,
		"Attempting to access past the end of the span! m_span = 0x%p, m_ptr = 0x%p, m_startPtr = 0x%p, m_endPtr = 0x%p, count = %lli",
		m_span, m_ptr, m_span->m_startPtr, m_span->m_endPtr, count );

	return ArraySpanIterator( m_ptr + count, CHECKED_SPAN( m_span ) );
}

template <typename Span>
RED_INLINE ArraySpanIterator< Span > operator+( typename ArraySpanIterator< Span >::DiffType count, const ArraySpanIterator< Span >& iter )
{
	return iter + count;
}

template <typename Span>
RED_INLINE ArraySpanIterator< Span > ArraySpanIterator< Span >::operator-( DiffType count ) const
{
	CHECKED_VALIDATE_ITERATOR( *this );
	CHECKED_FATAL_ASSERT( m_span->m_startPtr <= m_ptr - count,
		"Attempting to access past the start of the span! m_span = 0x%p, m_ptr = 0x%p, m_startPtr = 0x%p, m_endPtr = 0x%p, count = %lli",
		m_span, m_ptr, m_span->m_startPtr, m_span->m_endPtr, count );

	return ArraySpanIterator( m_ptr - count, CHECKED_SPAN( m_span ) );
}

template <typename Span>
RED_INLINE ArraySpanIterator< Span >& ArraySpanIterator< Span >::operator+=( DiffType count )
{
	CHECKED_VALIDATE_ITERATOR( *this );
	CHECKED_FATAL_ASSERT( m_ptr + count <= m_span->m_endPtr,
		"Attempting to access past the end of the span! m_span = 0x%p, m_ptr = 0x%p, m_startPtr = 0x%p, m_endPtr = 0x%p, count = %lli",
		m_span, m_ptr, m_span->m_startPtr, m_span->m_endPtr, count );

	m_ptr += count;

	return *this;
}

template <typename Span>
RED_INLINE ArraySpanIterator< Span >& ArraySpanIterator< Span >::operator-=( DiffType count )
{
	CHECKED_VALIDATE_ITERATOR( *this );
	CHECKED_FATAL_ASSERT( m_span->m_startPtr <= m_ptr - count,
		"Attempting to access past the start of the span! m_span = 0x%p, m_ptr = 0x%p, m_startPtr = 0x%p, m_endPtr = 0x%p, count = %lli",
		m_span, m_ptr, m_span->m_startPtr, m_span->m_endPtr, count );

	m_ptr -= count;

	return *this;
}

template <typename Span>
RED_INLINE typename ArraySpanIterator< Span >::DiffType ArraySpanIterator< Span >::operator-( const ArraySpanIterator& it ) const
{
	CHECKED_FATAL_ASSERT( m_span == it.m_span, "Must be subtracting iterators from the same span! m_span = 0x%p, it.m_span = 0x%p", m_span, it.m_span );
	CHECKED_VALIDATE_ITERATOR( *this );
	CHECKED_VALIDATE_ITERATOR( it );

	return static_cast< DiffType >( m_ptr - it.m_ptr );
}

template <typename Span>
constexpr bool ArraySpanIterator< Span >::operator==( const ArraySpanIterator& other ) const
{
	return m_ptr == other.m_ptr;
}

template <typename Span>
constexpr bool ArraySpanIterator< Span >::operator!=( const ArraySpanIterator& other ) const
{
	return m_ptr != other.m_ptr;
}

template <typename Span>
constexpr bool ArraySpanIterator< Span >::operator<( const ArraySpanIterator& other ) const
{
	return m_ptr < other.m_ptr;
}

template <typename Span>
constexpr bool ArraySpanIterator< Span >::operator<=( const ArraySpanIterator& other ) const
{
	return m_ptr <= other.m_ptr;
}

template <typename Span>
constexpr bool ArraySpanIterator< Span >::operator>( const ArraySpanIterator& other ) const
{
	return m_ptr > other.m_ptr;
}

template <typename Span>
constexpr bool ArraySpanIterator< Span >::operator>=( const ArraySpanIterator& other ) const
{
	return m_ptr >= other.m_ptr;
}

template < typename TPtrDiff, typename TElement >
RED_INLINE TPtrDiff Distance( ArraySpanIterator< TElement > begin, ArraySpanIterator< TElement > end )
{
	const typename ArraySpanIterator< TElement >::DiffType diff = end - begin;
	RED_SYSTEM_ASSERT( diff >= std::numeric_limits< TPtrDiff >::lowest() && diff <= std::numeric_limits< TPtrDiff >::max(), "Distance cannot store value in specified type." );
	return static_cast< TPtrDiff >( diff );
}

} // namespace red

#undef CHECKED_FATAL_ASSERT
#undef CHECKED_VALIDATE_ITERATOR
