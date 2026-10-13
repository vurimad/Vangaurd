/*
* Copyright (c) 2016 CD Projekt Red. All Rights Reserved.
*/

#pragma once

//////////////////////////////////////////////////////////////////////////
// Simple wrapper for externally managed plain data array.
// It does not call constructors nor destructors so it should not be used with 
// types that depend on calling those.
//
// Example:
//
// Uint8 buffer[ 100 ];
// ArrayWrapper< Int32 > locArrayInt( buffer, 25, 25 )
// for ( Uint32 i = 0; i < locArrayInt.Size(); ++i )
// {
//     Int32 var = locArrayInt[ i ];
// }
//////////////////////////////////////////////////////////////////////////

namespace red
{

template < typename TElement, typename RuntimeDataLocation >
class ArrayWrapperBase : public RuntimeDataLocation
{
public:

	typedef TElement*			iterator;
	typedef const TElement*		const_iterator;

	// Get typed pointer to the wrapped buffer
	RED_INLINE TElement* TypedData() { return m_buffer; }
	// Get typed const pointer to the wrapped buffer
	RED_INLINE const TElement* TypedData() const { return m_buffer; }
	// Get number of elements in the array
	RED_INLINE Uint32 Size() const { return m_size;	}
	// Get maximum number of elements that can be stored in the array
	RED_INLINE Uint32 Capacity() const { return m_capacity;	}
	// Returns true if array contains 0 elements
	RED_INLINE Bool Empty() const { return Size() == 0; }

	// Get iterator pointing to the first element in array
	RED_INLINE iterator Begin() { return TypedData(); }
	// Get const_iterator pointing to the element next after the last one
	RED_INLINE iterator End() {	return TypedData() + Size(); }
	// Get iterator pointing to the first element in array
	RED_INLINE const_iterator Begin() const { return TypedData(); }
	// Get const_iterator pointing to the element next after the last one
	RED_INLINE const_iterator End() const { return TypedData() + Size(); }

	// Get reference to the i-th element of the array
	RED_INLINE TElement& operator[]( Uint32 i );
	// Get const reference to the i-th element of the array
	RED_INLINE const TElement& operator[]( Uint32 i ) const;
	// Get reference to the first element
	RED_INLINE TElement& Front();
	// Get const reference to the first element
	RED_INLINE const TElement& Front() const;
	// Get reference to the last element
	RED_INLINE TElement& Back();
	// Get const reference to the last element
	RED_INLINE const TElement& Back() const;

	// Returns true if 'arr' contains the same elements
	RED_INLINE Bool operator==( const ArrayWrapperBase& other ) const;
	// Returns true if 'arr' does not contain the same elements
	RED_INLINE Bool operator!=( const ArrayWrapperBase& other  ) const;

	// Add 'element' to the back of the array
	RED_INLINE void PushBack( const TElement& element );
	// Remove and return the last element from the array
	RED_INLINE TElement PopBack();
	// Insert 'element' at the specified 'index'
	RED_INLINE void InsertAt( Uint32 index, const TElement& element );
	// Remove element specified by 'index'
	RED_INLINE void RemoveAt( Uint32 index );

	// Get index of the first occurrence of 'element' in the array; returns -1 if 'element' was not found
	RED_INLINE Int32 GetIndex( const TElement& element ) const;
	// Returns true if 'element' is present in the array
	RED_INLINE Bool Exist( const TElement& element ) const;
	// Get pointer to the first occurrence of 'element' in the array; returns nullptr if 'element' was not found
	RED_INLINE TElement* FindPtr( const TElement& element );
	// Get const pointer to the first occurrence of 'element' in the array; returns nullptr if 'element' was not found
	RED_INLINE const TElement* FindPtr( const TElement& element ) const;

	// Remove all elements from the array
	RED_INLINE void Clear();
	// Change number of elements stored in the array
	RED_INLINE void Resize( Uint32 size );
	// Insert 'amount' number of uninitialized elements at the end of the array
	RED_INLINE void Grow( Uint32 amount );

protected:

	RED_INLINE ArrayWrapperBase( void* buffer, Uint32 capacity );
	RED_INLINE ArrayWrapperBase( void* buffer, Uint32 capacity, Uint32& sizeRef );

	TElement*		m_buffer;
	Uint32			m_capacity;
	using RuntimeDataLocation::m_size;

private:

	static_assert( std::is_pod< TElement >::value || TAllowUseAsPOD< TElement >::Value , "You can use ArrayWrapper only with POD or Math types" );
}; 

//////////////////////////////////////////////////////////////////////////

namespace ArrayWrapperInternal
{

	class InternalRuntimeLocation
	{
	protected:

		RED_INLINE InternalRuntimeLocation( Uint32 size ) : m_size( size ) {}

		Uint32 m_size;
	};

	class ExternalRuntimeLocation
	{
	protected:

		RED_INLINE ExternalRuntimeLocation( Uint32& size ) : m_size( size ) {}	

		Uint32& m_size;
	};

} // ArrayWrapperInternal

//////////////////////////////////////////////////////////////////////////

template < typename TElement >
class ArrayWrapper : public ArrayWrapperBase< TElement, ArrayWrapperInternal::InternalRuntimeLocation >
{
public:

	// Construct array wrapper that uses 'buffer' of given 'capacity'; initial number of elements is set to 'initSize'
	RED_INLINE ArrayWrapper( void* buffer, Uint32 capacity, Uint32 initSize = 0 )
		: ArrayWrapperBase< TElement, ArrayWrapperInternal::InternalRuntimeLocation >( buffer, capacity )
	{
		this->m_size = initSize;
	}
};

template < typename TElement >
class ArrayWrapperExt : public ArrayWrapperBase< TElement, ArrayWrapperInternal::ExternalRuntimeLocation >
{
public:

	// Construct array wrapper that uses 'buffer' of given 'capacity'; size of the array is stored in external variable referenced by 'sizeRef'
	RED_INLINE ArrayWrapperExt( void* buffer, Uint32 capacity, Uint32& sizeRef )
		: ArrayWrapperBase< TElement, ArrayWrapperInternal::ExternalRuntimeLocation >( buffer, capacity, sizeRef )
	{}
};

//////////////////////////////////////////////////////////////////////////

template < typename TElement, typename RuntimeDataLocation >
typename ArrayWrapperBase< TElement, RuntimeDataLocation >::iterator begin( ArrayWrapperBase< TElement, RuntimeDataLocation >& arr )
{
	return arr.Begin();
}

template < typename TElement, typename RuntimeDataLocation >
typename ArrayWrapperBase< TElement, RuntimeDataLocation >::iterator end( ArrayWrapperBase< TElement, RuntimeDataLocation >& arr )
{
	return arr.End();
}

template < typename TElement, typename RuntimeDataLocation >
typename ArrayWrapperBase< TElement, RuntimeDataLocation >::const_iterator begin( const ArrayWrapperBase< TElement, RuntimeDataLocation >& arr )
{
	return arr.Begin();
}

template < typename TElement, typename RuntimeDataLocation >
typename ArrayWrapperBase< TElement, RuntimeDataLocation >::const_iterator end( const ArrayWrapperBase< TElement, RuntimeDataLocation >& arr )
{
	return arr.End();
}

} // red

#include "arrayWrapper.hpp"

