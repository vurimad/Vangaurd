/*
 * Copyright (c) 2016 CD Projekt Red. All Rights Reserved.
 */

#pragma once

#include <iterator>

namespace red
{
	template <typename Span>
	class ArraySpanIterator
	{
	public:
		using TElement = typename Span::value_type;
		using PtrType = TElement*;
		using RefType = TElement&;
		using DiffType = Int64;
		using Tag = ArrayIteratorTag;

        using iterator_category = std::random_access_iterator_tag;
	    using value_type = TElement;
	    using difference_type = DiffType;
	    using pointer = TElement*;
	    using reference = TElement&;

		constexpr ArraySpanIterator();
		constexpr ArraySpanIterator( const ArraySpanIterator& other );
		constexpr ArraySpanIterator( TElement* ptr, const Span* span );

		ArraySpanIterator& operator=( const ArraySpanIterator& other );

		RefType operator*() const;
		PtrType operator->() const;
		RefType operator[]( DiffType count ) const;

		ArraySpanIterator& operator++();
		ArraySpanIterator operator++(int);
		ArraySpanIterator& operator--();
		ArraySpanIterator operator--(int);

		ArraySpanIterator operator+( DiffType count ) const;
		friend ArraySpanIterator operator+( DiffType count, const ArraySpanIterator& iter );
		ArraySpanIterator operator-( DiffType count ) const;
		ArraySpanIterator& operator+=( DiffType count );
		ArraySpanIterator& operator-=( DiffType count );
		DiffType operator-( const ArraySpanIterator& it ) const;

		constexpr bool operator==( const ArraySpanIterator& other ) const;
		constexpr bool operator!=( const ArraySpanIterator& other ) const;
		constexpr bool operator<( const ArraySpanIterator& other ) const;
		constexpr bool operator<=( const ArraySpanIterator& other ) const;
		constexpr bool operator>( const ArraySpanIterator& other ) const;
		constexpr bool operator>=( const ArraySpanIterator& other ) const;

	private:
		TElement* m_ptr;
#if defined( RED_CHECKED_ITERATORS )
		const Span* m_span;
#endif
	};

} // namespace red

#include "arraySpanIterator.hpp"
