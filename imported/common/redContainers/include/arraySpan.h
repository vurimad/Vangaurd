/*
 * Copyright (c) 2016 CD Projekt Red. All Rights Reserved.
 */

#pragma once

#include "arraySpanIterator.h"
#include "indexRange.h"

namespace red
{
	// Simple view class for contiguous data of the same type and size
	// Stores the contents as two pointers, [start, end), so a pointer
	// to the start of the sequence and a pointer to one past the end.
	//
	// To adapt this for a custom container look at the start of the hpp file.
	template <typename TElement>
	class ArraySpan
	{
	public:
		// Needed for converting between compatible ArraySpan's
		template <typename T>
		friend class ArraySpan;

		template <typename T>
		friend class ArraySpanIterator;

		typedef TElement value_type;

		typedef ArraySpanIterator< ArraySpan< TElement > > iterator;
		typedef ArraySpanIterator< ArraySpan< const TElement > > const_iterator;

	public:
		constexpr ArraySpan();

		// Construct from pointer and length
		constexpr ArraySpan( TElement* ptr, Uint32 count );

		// Construct from range of elements [begin, end)
		ArraySpan( TElement* begin, TElement* end );

		// Construct from DynArray, StaticArray and more compatible containers.
		// Based on GSL::span class, uses SFINAE to detect if Container has TypedData() method.
		template <typename Container, typename = typename std::enable_if_t<
			std::is_convertible<typename Container::ElementType*, TElement*>::value &&
			std::is_convertible<typename Container::ElementType*, decltype(std::declval<Container>().TypedData())>::value > >
		ArraySpan( Container& arr );

		template <typename Container, typename = typename std::enable_if_t< std::is_const<TElement>::value &&
			std::is_convertible<typename Container::ElementType*, TElement*>::value &&
			std::is_convertible<typename Container::ElementType*, decltype(std::declval<Container>().TypedData())>::value > >
		ArraySpan( const Container& arr );

		// Construct from c style static sized array
		template <Uint32 N>
		constexpr ArraySpan( TElement (&arr)[N] );

		// Construct from another ArraySpan of a convertible type
		template <typename T, typename = typename std::enable_if_t< std::is_convertible< T (*)[], TElement (*)[] >::value > >
		constexpr ArraySpan( const ArraySpan< T >& other );

		// Prevent construction from temporaries
		ArraySpan( TElement&& ) = delete;

		template <typename T, typename M>
		ArraySpan( DynArray< T >&& ) = delete;

		// Default copy constructors and assignment operators are ok
		constexpr ArraySpan( ArraySpan&& ) = default;
		constexpr ArraySpan( const ArraySpan& ) = default;

		ArraySpan& operator=( ArraySpan&& ) = default;
		ArraySpan& operator=( const ArraySpan& ) = default;

		~ArraySpan();

		void Swap( ArraySpan& other );

		constexpr bool Empty() const;

		constexpr Uint32 Count() const;

		// Returns the size in elements, same as Count()
		constexpr Uint32 Size() const;

		// Returns the size in bytes
		constexpr Uint32 SizeInBytes() const;

		constexpr TElement* Data() const;

		TElement& Front() const;
		TElement& Back() const;

		// Return index at a point, checks range for validity
		TElement& At( Uint32 index ) const;

		// Returns index at a point, no checks are performed
		TElement& operator[]( Uint32 index ) const;

		// Return ArraySpan of first count elements
		ArraySpan Left( Uint32 count ) const;

		// Return ArraySpan of last count elements
		ArraySpan Right( Uint32 count ) const;

		// Return ArraySpan from beginIndex, including elements up to but not including endIndex 
		ArraySpan Slice( Uint32 beginIndex, Uint32 endIndex ) const;

		// Returns ArraySpan from offset and length
		ArraySpan SubSpan( Uint32 offset, Uint32 count ) const;

		// Returns reference to first element and then removes it from the ArraySpan
		TElement& PopFront();

		// Returns reference to last element and then removes it from the ArraySpan
		TElement& PopBack();

		// Finds first element equal to (operator==) provided param and returns its address. Returns nullptr if none is found.
		TElement* FindPtr( const TElement& elem ) const;

		// Finds and returns address of the first element when func() returns true. Returns nullptr if none is found.
		template <typename Func>
		TElement* FindPtrIf( const Func& func ) const;

		template <typename Func>
		TElement* FindPtrIf( Func&& func ) const;

		// Determines if the element or a comparable element is inside this array span
		template < typename UElement >
		bool Contains( const UElement& elem ) const;

		// Returns a range which allows the user to perform range-based for loop through array indices
		IndexRange Indices() const;

		// Returns a range which allows the user to perform reverse range-based for loop through array indices
		ReverseIndexRange ReverseIndices() const;

		iterator begin();
		iterator end();
		const_iterator begin() const;
		const_iterator end() const;

	private:
		TElement* m_startPtr;			// Pointer to start of sequence
		TElement* m_endPtr;			// Pointer one past the end of the sequence
	};

	template < typename TElement >
	typename ArraySpan< TElement >::iterator begin(ArraySpan< TElement >& arr);

	template < typename TElement >
	typename ArraySpan< TElement >::iterator end(ArraySpan< TElement >& arr);

	template < typename TElement >
	typename ArraySpan< TElement >::const_iterator begin(const ArraySpan< TElement >& arr);

	template < typename TElement >
	typename ArraySpan< TElement >::const_iterator end(const ArraySpan< TElement >& arr);

	template< typename TElement >
	ArraySpan< TElement > MakeArraySpan( TElement* ptr, Uint32 count );
	
	template< typename TElement >
	ArraySpan< TElement > MakeArraySpan( TElement* begin, TElement* end );

	template <template<class> class Container, typename TElement >
	ArraySpan< TElement > MakeArraySpan( Container< TElement >& cont );

	template <template<class> class Container, typename TElement >
	decltype(auto) MakeArraySpan( const Container< TElement >& cont );

	template <template<class> class Iterator, typename TElement >
	decltype(auto) MakeArraySpan( Iterator< TElement > begin, Iterator< TElement > end );

} // namespace red

#include "arraySpan.hpp"
