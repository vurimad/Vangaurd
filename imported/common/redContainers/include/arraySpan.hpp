/*
 * Copyright (c) 2016 CD Projekt Red. All Rights Reserved.
 */

#pragma once

namespace red
{
	template <typename TElement>
	constexpr ArraySpan< TElement >::ArraySpan()
		: m_startPtr( nullptr )
		, m_endPtr( nullptr )
	{}

	template <typename TElement>
	constexpr ArraySpan< TElement >::ArraySpan( TElement* ptr, Uint32 count )
		: m_startPtr( ptr )
		, m_endPtr( ptr + count )
	{}

	template <typename TElement>
	RED_INLINE ArraySpan< TElement >::ArraySpan( TElement* begin, TElement* end )
		: m_startPtr( begin )
		, m_endPtr( end )
	{
		RED_FATAL_ASSERT( begin != nullptr, "Beginning of range cannot be null" );
		RED_FATAL_ASSERT( end != nullptr, "End of range cannot be null" );
		RED_FATAL_ASSERT( m_startPtr <= m_endPtr, "Invalid range for an array" );
	}

	template <typename TElement>
	template <typename Container, typename>
	RED_INLINE ArraySpan< TElement >::ArraySpan( Container& arr )
		: m_startPtr( GetStartPtr( arr ) )
		, m_endPtr( GetEndPtr( arr ) )
	{
		RED_FATAL_ASSERT( m_startPtr <= m_endPtr, "Invalid range for an array" );
	}

	template <typename TElement>
	template <typename Container, typename>
	RED_INLINE ArraySpan< TElement >::ArraySpan( const Container& arr )
		: m_startPtr( GetStartPtr( arr ) )
		, m_endPtr( GetEndPtr( arr ) )
	{
		RED_FATAL_ASSERT( m_startPtr <= m_endPtr, "Invalid range for an array" );
	}

	template <typename TElement>
	template <Uint32 N>
	constexpr ArraySpan< TElement >::ArraySpan( TElement(&arr)[N] )
		: m_startPtr( &arr[0] )
		, m_endPtr( &arr[N] )
	{}

	template <typename TElement>
	template <typename T, typename>
	constexpr ArraySpan< TElement >::ArraySpan( const ArraySpan< T >& other )
		: m_startPtr( other.m_startPtr )
		, m_endPtr( other.m_endPtr )
	{
	}

	template <typename TElement>
	ArraySpan< TElement >::~ArraySpan()
	{}

	template <typename TElement>
	RED_INLINE void ArraySpan< TElement >::Swap( ArraySpan& other )
	{
		std::swap( m_startPtr, other.m_startPtr );
		std::swap( m_endPtr, other.m_endPtr );
	}

	template <typename TElement>
	constexpr Bool ArraySpan< TElement >::Empty() const
	{
		return m_startPtr == m_endPtr;
	}

	template <typename TElement>
	constexpr Uint32 ArraySpan< TElement >::Count() const
	{
		return static_cast<Uint32>( m_endPtr - m_startPtr );
	}

	template <typename TElement>
	constexpr Uint32 ArraySpan< TElement >::Size() const
	{
		return Count();
	}

	template <typename TElement>
	constexpr Uint32 ArraySpan< TElement >::SizeInBytes() const
	{
		return Size() * sizeof(TElement);
	}

	template <typename TElement>
	constexpr TElement* ArraySpan< TElement >::Data() const
	{
		return m_startPtr;
	}

	template <typename TElement>
	RED_INLINE TElement& ArraySpan< TElement >::Front() const
	{
		RED_FATAL_ASSERT( Count() > 0, "Cannot get front element from empty span!" );
		return *m_startPtr;
	}

	template <typename TElement>
	RED_INLINE TElement& ArraySpan< TElement >::Back() const
	{
		RED_FATAL_ASSERT( Count() > 0, "Cannot get back element from empty span!" );
		return *(m_endPtr - 1);
	}

	template <typename TElement>
	RED_INLINE TElement& ArraySpan< TElement >::At( Uint32 index ) const
	{
		RED_FATAL_ASSERT( index < Count(), "Index out of range!" );
		return m_startPtr[ index ];
	}

	template <typename TElement>
	RED_INLINE TElement& ArraySpan< TElement >::operator[]( Uint32 index ) const
	{
		RED_FATAL_ASSERT( index < Count(), "Index out of range!" );
		return m_startPtr[ index ];
	}

	template <typename TElement>
	RED_INLINE ArraySpan< TElement > ArraySpan< TElement >::Left( Uint32 count ) const
	{
		RED_FATAL_ASSERT( count <= Count(), "Count contains more items than in view" );
		return ArraySpan( m_startPtr, m_startPtr + count );
	}

	template <typename TElement>
	RED_INLINE ArraySpan< TElement > ArraySpan< TElement >::Right( Uint32 count ) const
	{
		RED_FATAL_ASSERT( count <= Count(), "Count contains more items than in view" );
		return ArraySpan( m_endPtr - count, m_endPtr );
	}

	template <typename TElement>
	RED_INLINE ArraySpan< TElement > ArraySpan< TElement >::Slice( Uint32 beginIndex, Uint32 endIndex ) const
	{
		RED_FATAL_ASSERT( beginIndex <= Count(), "Begin Index out of range" );
		RED_FATAL_ASSERT( endIndex <= Count(), "End Index out of range" );
		RED_FATAL_ASSERT( beginIndex <= endIndex, "Wrong order of Indexes" );
		return ArraySpan( m_startPtr + beginIndex, m_startPtr + endIndex );
	}

	template <typename TElement>
	RED_INLINE ArraySpan< TElement > ArraySpan< TElement >::SubSpan( Uint32 offset, Uint32 count ) const
	{
		RED_FATAL_ASSERT( offset + count <= Count(), "Offset and count map to items outside this view" );
		return ArraySpan( m_startPtr + offset, m_startPtr + offset + count );
	}

	template <typename TElement>
	RED_INLINE TElement& ArraySpan< TElement >::PopFront()
	{
		TElement& result = Front();
		m_startPtr += 1;
		return result;
	}

	template <typename TElement>
	RED_INLINE TElement& ArraySpan< TElement >::PopBack()
	{
		TElement& result = Back();
		m_endPtr -= 1;
		return result;
	}

	template <typename TElement>
	TElement* ArraySpan< TElement >::FindPtr( const TElement& element ) const
	{
		auto foundIter = std::find( begin(), end(), element );
		return (foundIter != end()) ? &(*foundIter) : nullptr;
	}

	template <typename TElement>
	template <typename Func>
	TElement* ArraySpan< TElement >::FindPtrIf( const Func& func ) const
	{
		auto foundIter = std::find_if( begin(), end(), func );
		return (foundIter != end()) ? &(*foundIter) : nullptr;
	}

	template <typename TElement>
	template <typename Func>
	TElement* ArraySpan< TElement >::FindPtrIf( Func&& func ) const
	{
		auto foundIter = std::find_if( begin(), end(), std::move( func ) );
		return (foundIter != end()) ? &(*foundIter) : nullptr;
	}

	template <typename TElement>
	template <typename UElement>
	bool ArraySpan< TElement >::Contains( const UElement& elem ) const
	{
		return std::find( begin(), end(), elem ) != end();
	}

    template <typename TElement>
    IndexRange ArraySpan< TElement >::Indices() const
    {
        return IndexRange( 0, Size() );
    }

    template <typename TElement>
	ReverseIndexRange ArraySpan< TElement >::ReverseIndices() const
    {
        return ReverseIndexRange( Size(), 0 );
    }

	template <typename TElement>
	RED_INLINE typename ArraySpan< TElement >::iterator ArraySpan< TElement >::begin()
	{
		return iterator( m_startPtr, this );
	}

	template <typename TElement>
	RED_INLINE typename ArraySpan< TElement >::iterator ArraySpan< TElement >::end()
	{
		return iterator( m_endPtr, this );
	}

	template <typename TElement>
	RED_INLINE typename ArraySpan< TElement >::const_iterator ArraySpan< TElement >::begin() const
	{
		return const_iterator( m_startPtr, reinterpret_cast< const ArraySpan< typename std::add_const< TElement >::type > * >( this ) );
	}

	template <typename TElement>
	RED_INLINE typename ArraySpan< TElement >::const_iterator ArraySpan< TElement >::end() const
	{
		return const_iterator( m_endPtr, reinterpret_cast< const ArraySpan< typename std::add_const< TElement >::type > * >( this ) );
	}

	template < typename TElement >
	RED_INLINE typename ArraySpan< TElement >::iterator begin(ArraySpan< TElement >& arr)
	{
		return arr.begin();
	}

	template < typename TElement >
	RED_INLINE typename ArraySpan< TElement >::iterator end(ArraySpan< TElement >& arr)
	{
		return arr.end();
	}

	template < typename TElement >
	RED_INLINE typename ArraySpan< TElement >::const_iterator begin(const ArraySpan< TElement >& arr)
	{
		return arr.begin();
	}

	template < typename TElement >
	RED_INLINE typename ArraySpan< TElement >::const_iterator end(const ArraySpan< TElement >& arr)
	{
		return arr.end();
	}
	
	template< typename TElement >
	RED_INLINE ArraySpan< TElement > MakeArraySpan( TElement* ptr, Uint32 count )
	{
		return ArraySpan< TElement >( ptr, count );
	}

	template< typename TElement >
	RED_INLINE ArraySpan< TElement > MakeArraySpan( TElement* begin, TElement* end )
	{
		return ArraySpan< TElement >( begin, end );
	}

		template < template<class> class Container, typename TElement >
	RED_INLINE ArraySpan< TElement > MakeArraySpan( Container< TElement >& arr )
	{
		return ArraySpan< TElement >( arr );
	}

	template < template<class> class Container, typename TElement >
	RED_INLINE decltype(auto) MakeArraySpan( const Container< TElement >& arr )
	{
		return ArraySpan< std::add_const_t< TElement > >( arr );
	}

	template <template<class> class Iterator, typename TElement >
	RED_INLINE decltype(auto) MakeArraySpan( Iterator<TElement> begin, Iterator<TElement> end )
	{
#ifdef RED_CHECKED_ITERATORS
		// Dereferencing CheckedIterator::End() asserts
		if( begin == end )
		{
			return ArraySpan < std::remove_pointer_t<decltype( std::declval< Iterator<TElement> >().operator->() ) > >();
		}
		else
		{
			return ArraySpan< std::remove_pointer_t<decltype( std::declval< Iterator<TElement> >().operator->() )> >( begin.operator->(), begin.operator->() + std::distance( begin, end ) );
		}
#else
		return ArraySpan< std::remove_pointer_t<decltype( std::declval< Iterator<TElement> >().operator->() )> >( begin.operator->(), end.operator->() );
#endif
	}

} // namespace red