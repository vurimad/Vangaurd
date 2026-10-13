/*
* Copyright (c) 2016 CD Projekt Red. All Rights Reserved.
*/

#pragma once

#include "containersCommon.h"

namespace red {

template < typename TElement > 
struct ArrayConstIterator
{
	typedef TElement						ElementType;
	typedef const ElementType*				PtrType;
	typedef const ElementType&				RefType;
	typedef Int64							DiffType;
	typedef ArrayIteratorTag				Tag;

	// STL Compatability
	typedef std::random_access_iterator_tag	iterator_category;
	typedef ElementType						value_type;
	typedef DiffType						difference_type;
	typedef PtrType							pointer;
	typedef RefType							reference;

	RED_INLINE ArrayConstIterator();
	RED_INLINE ArrayConstIterator( const ArrayConstIterator& it );
	RED_INLINE explicit ArrayConstIterator( const ElementType* ptr );

	struct BoolConversion{ Uint32 valid; };
	typedef Uint32 BoolConversion::*bool_operator;

	RED_INLINE operator bool_operator () const;
	RED_INLINE Bool operator!() const;

	RED_INLINE RefType operator*() const;
	RED_INLINE PtrType operator->() const;
	RED_INLINE RefType operator[]( DiffType count ) const;
	RED_INLINE ArrayConstIterator& operator++();
	RED_INLINE ArrayConstIterator operator++(int);
	RED_INLINE ArrayConstIterator& operator--();
	RED_INLINE ArrayConstIterator operator--(int);
	RED_INLINE ArrayConstIterator operator+( DiffType count ) const;
	RED_INLINE ArrayConstIterator operator-( DiffType count ) const;
	RED_INLINE friend ArrayConstIterator operator+( DiffType count, const ArrayConstIterator& iter );
	RED_INLINE ArrayConstIterator& operator+=( DiffType count );
	RED_INLINE ArrayConstIterator& operator-=( DiffType count );
	RED_INLINE DiffType operator-( const ArrayConstIterator& it ) const;
	RED_INLINE Bool operator==( const ArrayConstIterator& it ) const;
	RED_INLINE Bool operator!=( const ArrayConstIterator& it ) const;
	RED_INLINE Bool operator<( const ArrayConstIterator& it ) const;
	RED_INLINE Bool operator<=( const ArrayConstIterator& it ) const;
	RED_INLINE Bool operator>( const ArrayConstIterator& it ) const;
	RED_INLINE Bool operator>=( const ArrayConstIterator& it ) const;

protected:

	ElementType*	m_ptr;

	friend class ArrayIteratorImpl;
};

//////////////////////////////////////////////////////////////////////////

template < typename TElement > 
struct ArrayIterator : public ArrayConstIterator< TElement >
{
	typedef TElement						ElementType;
	typedef ElementType*					PtrType;
	typedef ElementType&					RefType;
	typedef Int64							DiffType;

	// STL Compatability
	typedef std::random_access_iterator_tag	iterator_category;
	typedef ElementType						value_type;
	typedef DiffType						difference_type;
	typedef PtrType							pointer;
	typedef RefType							reference;

	RED_INLINE ArrayIterator();
	RED_INLINE ArrayIterator( const ArrayIterator& it );
	RED_INLINE explicit ArrayIterator( ElementType* ptr );

	RED_INLINE RefType operator*() const;
	RED_INLINE PtrType operator->() const;
	RED_INLINE RefType operator[]( DiffType count ) const;
	RED_INLINE ArrayIterator& operator++();
	RED_INLINE ArrayIterator operator++(int);
	RED_INLINE ArrayIterator& operator--();
	RED_INLINE ArrayIterator operator--(int);
	RED_INLINE ArrayIterator operator+( DiffType count ) const;
	RED_INLINE ArrayIterator operator-( DiffType count ) const;
	RED_INLINE friend ArrayIterator operator+( DiffType count, const ArrayIterator& iter );
	RED_INLINE ArrayIterator& operator+=( DiffType count );
	RED_INLINE ArrayIterator& operator-=( DiffType count );
	RED_INLINE DiffType operator-( const ArrayIterator& it ) const;
	RED_INLINE DiffType operator-( const ArrayConstIterator< TElement >& it ) const; // while this line seems to be redundant it is required for VS to use it - cit instead of ptr - ptr
																			         // (clang compiles the code even without it)
private:

	typedef ArrayConstIterator< TElement >	BaseClass;

	friend class ArrayIteratorImpl;
};

//////////////////////////////////////////////////////////////////////////

class ArrayIteratorImpl
{
public:

	template < typename TIterator >
	RED_INLINE static typename TIterator::DiffType InternalDistance( const TIterator& from, const TIterator& to );
};

//////////////////////////////////////////////////////////////////////////

template < typename TArray >
struct ArrayReverseIteration
{
	TArray&			m_arr;
	RED_INLINE ArrayReverseIteration( TArray& arr )
		: m_arr( arr )
	{}
};

template < typename TArray >
struct ArrayReverseConstIteration
{
	const TArray&	m_arr;
	RED_INLINE ArrayReverseConstIteration( const TArray& arr )
		: m_arr( arr )
	{}
};

//////////////////////////////////////////////////////////////////////////

template< typename TArray >
RED_INLINE typename TArray::reverse_iterator begin( ArrayReverseIteration< TArray >& arr )
{
	return arr.m_arr.RBegin();
}

template< typename TArray >
RED_INLINE typename TArray::reverse_iterator end( ArrayReverseIteration< TArray >& arr )
{
	return arr.m_arr.REnd();
}

template< typename TArray >
RED_INLINE typename TArray::reverse_const_iterator begin( ArrayReverseConstIteration< TArray >& arr )
{
	return arr.m_arr.RBegin();
}

template< typename TArray >
RED_INLINE typename TArray::reverse_const_iterator end( ArrayReverseConstIteration< TArray >& arr )
{
	return arr.m_arr.REnd();
}

} // red

#include "arrayIterator.hpp"
