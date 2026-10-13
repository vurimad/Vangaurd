/*
* Copyright (c) 2016 CD Projekt Red. All Rights Reserved.
*/

#pragma once

namespace red {

template < typename TContainer > 
struct CheckedConstIterator
{
	typedef typename TContainer::ElementType		ElementType;
	typedef const ElementType*						PtrType;
	typedef const ElementType&						RefType;
	typedef Int64									DiffType;
	typedef ArrayIteratorTag						Tag;

	// STL Compatability
	typedef std::random_access_iterator_tag			iterator_category;
	typedef ElementType								value_type;
	typedef DiffType								difference_type;
	typedef PtrType									pointer;
	typedef RefType									reference;

	RED_INLINE CheckedConstIterator();
	RED_INLINE CheckedConstIterator( const TContainer* container, PtrType ptr );
	RED_INLINE CheckedConstIterator( const CheckedConstIterator& it );

	RED_INLINE explicit operator bool() const;
	RED_INLINE Bool operator!() const;

	RED_INLINE RefType operator*() const;
	RED_INLINE PtrType operator->() const;
	RED_INLINE RefType operator[]( DiffType count ) const;
	RED_INLINE CheckedConstIterator& operator++();
	RED_INLINE CheckedConstIterator operator++(int);
	RED_INLINE CheckedConstIterator& operator--();
	RED_INLINE CheckedConstIterator operator--(int);
	RED_INLINE CheckedConstIterator operator+( DiffType count ) const;
	RED_INLINE CheckedConstIterator operator-( DiffType count ) const;
	RED_INLINE friend CheckedConstIterator operator+( DiffType count, const CheckedConstIterator& iter );
	RED_INLINE CheckedConstIterator& operator+=( DiffType count );
	RED_INLINE CheckedConstIterator& operator-=( DiffType count );
	RED_INLINE DiffType operator-( const CheckedConstIterator& it ) const;
	RED_INLINE Bool operator==( const CheckedConstIterator& it ) const;
	RED_INLINE Bool operator!=( const CheckedConstIterator& it ) const;
	RED_INLINE Bool operator<( const CheckedConstIterator& it ) const;
	RED_INLINE Bool operator<=( const CheckedConstIterator& it ) const;
	RED_INLINE Bool operator>( const CheckedConstIterator& it ) const;
	RED_INLINE Bool operator>=( const CheckedConstIterator& it ) const;

protected:

	RED_INLINE Bool IsValid() const;
	RED_INLINE Bool IsValidOrEnd() const;
	RED_INLINE Bool IsNull() const;

	TContainer*		m_container;
	PtrType			m_ptr;
};

//////////////////////////////////////////////////////////////////////////

template < typename TContainer > 
struct CheckedIterator : public CheckedConstIterator< TContainer >
{
	typedef typename TContainer::ElementType		ElementType;
	typedef ElementType*							PtrType;
	typedef ElementType&							RefType;
	typedef Int64									DiffType;

	// STL Compatability
	typedef std::random_access_iterator_tag			iterator_category;
	typedef ElementType								value_type;
	typedef DiffType								difference_type;
	typedef PtrType									pointer;
	typedef RefType									reference;

	RED_INLINE CheckedIterator();
	RED_INLINE CheckedIterator( TContainer* container, PtrType ptr );
	RED_INLINE CheckedIterator( const CheckedIterator& it );

	RED_INLINE RefType operator*() const;
	RED_INLINE PtrType operator->() const;
	RED_INLINE RefType operator[]( DiffType count ) const;
	RED_INLINE CheckedIterator& operator++();
	RED_INLINE CheckedIterator operator++(int);
	RED_INLINE CheckedIterator& operator--();
	RED_INLINE CheckedIterator operator--(int);
	RED_INLINE CheckedIterator operator+( DiffType count ) const;
	RED_INLINE CheckedIterator operator-( DiffType count ) const;
	RED_INLINE friend CheckedIterator operator+( DiffType count, const CheckedIterator& iter );
	RED_INLINE CheckedIterator& operator+=( DiffType count );
	RED_INLINE CheckedIterator& operator-=( DiffType count );
	RED_INLINE DiffType operator-( const CheckedIterator& it ) const;
	RED_INLINE DiffType operator-( const CheckedConstIterator< TContainer >& it ) const; // while this line seems to be redundant it is required for VS to use it - cit instead of ptr - ptr
																						 // (clang compiles the code even without it)
private:

	typedef CheckedConstIterator< TContainer >	BaseClass;
};

} // red

#include "checkedIterator.hpp"
