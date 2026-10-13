/*
* Copyright (c) 2015-16 CD Projekt Red. All Rights Reserved.
*/

#pragma once

#include "policies.h"
#include "arrayImplUtils.h"

namespace red {

//////////////////////////////////////////////////////////////////////////

template< typename TElement, Uint32 MaxSize >
RED_INLINE StaticArray< TElement, MaxSize >::StaticArray()
	: m_size( 0 )
{
}

template< typename TElement, Uint32 MaxSize >
RED_INLINE StaticArray< TElement, MaxSize >::StaticArray( const StaticArray& other )
	: m_size( 0 )
{
	ArrayImplUtils::Copy( other.Begin(), other.End(), *this );
}

template< typename TElement, Uint32 MaxSize >
template < Uint32 OtherMaxSize >
RED_INLINE StaticArray< TElement, MaxSize >::StaticArray( const StaticArray< TElement, OtherMaxSize >& other )
	: m_size( 0 )
{
	ArrayImplUtils::Copy( other.Begin(), other.End(), *this );
}

template< typename TElement, Uint32 MaxSize >
RED_INLINE StaticArray< TElement, MaxSize >::StaticArray( StaticArray&& other )
	: m_size( 0 )
{
	MoveInternal( std::forward< StaticArray >( other ) );
}

template< typename TElement, Uint32 MaxSize >
RED_INLINE StaticArray< TElement, MaxSize >::StaticArray( std::initializer_list< TElement > initializerList )
	: m_size( 0 )
{
	RED_FATAL_ASSERT( initializerList.size() <= MaxSize, "Initializer list is bigger than MaxSize" );

	typedef typename policies::CopyConstructorExecutorSelector< TElement >::Type CopyConstructorExecutor;

	m_size = static_cast< Uint32 >( initializerList.size() );
	CopyConstructorExecutor::Execute( TypedData(), initializerList.begin(), m_size );
}

template< typename TElement, Uint32 MaxSize >
RED_INLINE StaticArray< TElement, MaxSize >::StaticArray( Uint32 size )
	: m_size( 0 )
{
	Resize( size );
}

template< typename TElement, Uint32 MaxSize >
template< typename DefaultValueType >
RED_INLINE StaticArray< TElement, MaxSize >::StaticArray( Uint32 size, const DefaultValueType & value )
	: m_size( 0 )
{
	m_size = size;
	auto dst = TypedData();
	while(size-- > 0)
	{
		::new(dst++) TElement( value );
	}
}

template< typename TElement, Uint32 MaxSize >
RED_INLINE StaticArray< TElement, MaxSize >::~StaticArray()
{
	typedef typename policies::DestructorExecutorSelector< TElement >::Type	DestructorExecutor;

	DestructorExecutor::Execute( TypedData(), m_size );
	m_size = 0;
}

//////////////////////////////////////////////////////////////////////////

template< typename TElement, Uint32 MaxSize >
RED_INLINE StaticArray< TElement, MaxSize >& StaticArray< TElement, MaxSize >::operator=( const StaticArray& other )
{
	if ( &other != this )
	{
		ArrayImplUtils::Copy( other.Begin(), other.End(), *this );
	}
	return *this;
}

template< typename TElement, Uint32 MaxSize >
template < Uint32 OtherMaxSize >
RED_INLINE StaticArray< TElement, MaxSize >& StaticArray< TElement, MaxSize >::operator=( const StaticArray< TElement, OtherMaxSize >& other )
{
	ArrayImplUtils::Copy( other.Begin(), other.End(), *this );
	return *this;
}

template< typename TElement, Uint32 MaxSize >
RED_INLINE StaticArray< TElement, MaxSize >& StaticArray< TElement, MaxSize >::operator=( StaticArray&& other )
{
	if ( &other != this )
	{
		MoveInternal( std::forward< StaticArray >( other ) );
	}
	return *this;
}

template< typename TElement, Uint32 MaxSize >
RED_INLINE StaticArray< TElement, MaxSize >& StaticArray< TElement, MaxSize >::operator=( std::initializer_list< TElement > initializerList )
{
	ArrayImplUtils::Copy( initializerList.begin(), initializerList.end(), *this );
	return *this;
}

template< typename TElement, Uint32 MaxSize >
RED_INLINE void StaticArray< TElement, MaxSize >::MoveInternal( StaticArray&& other )
{
	typedef typename policies::MoveConstructorExecutorSelector< TElement >::Type		MoveConstructorExecutor;
	typedef typename policies::DestructorExecutorSelector< TElement >::Type				DestructorExecutor;
	typedef typename policies::MoveAssignmentExecutorSelector< TElement >::Type			MoveAssignmentExecutor;

	TElement* const data = this->TypedData();
	TElement* const otherData = other.TypedData();
	const Uint32 otherSize = other.Size();
	if ( otherSize >= this->m_size )
	{
		MoveAssignmentExecutor::Execute( data, otherData, this->m_size );
		// temporary fix, because of C4996 (see policies.h for details)
		// std::move( arrData, arrData + this->m_size, data );
		MoveConstructorExecutor::Execute( data + this->m_size, otherData + this->m_size, otherSize - this->m_size );
		DestructorExecutor::Execute( otherData, otherSize );
	}
	else
	{
		MoveAssignmentExecutor::Execute( data, otherData, otherSize );
		// temporary fix, because of C4996 (see policies.h for details)
		// std::move( arrData, arrData + this->m_size, data );
		DestructorExecutor::Execute( data + otherSize, this->m_size - otherSize );
		DestructorExecutor::Execute( otherData, otherSize );
	}
	this->m_size = otherSize;
	other.m_size = 0;
}

//////////////////////////////////////////////////////////////////////////

template < typename TElement, Uint32 MaxSize >
RED_INLINE TElement& StaticArray< TElement, MaxSize >::operator[]( Uint32 i )
{
	RED_FATAL_ASSERT( i < m_size, "Array: Out of bounds. Cannot access item %i as the array is only size %u", i, m_size );
	return TypedData()[ i ];
}

template < typename TElement, Uint32 MaxSize >
RED_INLINE const TElement& StaticArray< TElement, MaxSize >::operator[]( Uint32 i ) const
{
	RED_FATAL_ASSERT( i < m_size, "Array: Out of bounds. Cannot access item %i as the array is only size %u", i, m_size );
	return TypedData()[ i ];
}

template < typename TElement, Uint32 MaxSize >
RED_INLINE TElement& StaticArray< TElement, MaxSize >::Front()
{
	RED_FATAL_ASSERT( m_size > 0, "Array: Cannot access frist item - array is empty!" );
	return TypedData()[ 0 ];
}

template < typename TElement, Uint32 MaxSize >
RED_INLINE const TElement& StaticArray< TElement, MaxSize >::Front() const
{
	RED_FATAL_ASSERT( m_size > 0, "Array: Cannot access frist item - array is empty!" );
	return TypedData()[ 0 ];
}

template < typename TElement, Uint32 MaxSize >
RED_INLINE TElement& StaticArray< TElement, MaxSize >::Back()
{
	RED_FATAL_ASSERT( m_size > 0, "Array: Cannot access last item - array is empty!" );
	return TypedData()[ m_size - 1 ];
}

template < typename TElement, Uint32 MaxSize >
RED_INLINE const TElement& StaticArray< TElement, MaxSize >::Back() const
{
	RED_FATAL_ASSERT( m_size > 0, "Array: Cannot access last item - array is empty!" );
	return TypedData()[ m_size - 1 ];
}

//////////////////////////////////////////////////////////////////////////

template < typename TElement, Uint32 MaxSize >
template < Uint32 OtherMaxSize >
RED_INLINE Bool StaticArray< TElement, MaxSize >::operator==( const StaticArray< TElement, OtherMaxSize >& other ) const
{
	typedef typename policies::ComparePolicySelector< TElement >::Type ComparePolicy;

	return ( m_size == other.Size() && ComparePolicy::Equal( TypedData(), other.TypedData(), m_size ) );
}

template < typename TElement, Uint32 MaxSize >
template < Uint32 OtherMaxSize >
RED_INLINE Bool StaticArray< TElement, MaxSize >::operator!=( const StaticArray< TElement, OtherMaxSize >& other ) const
{
	return !( *this == other );
}

//////////////////////////////////////////////////////////////////////////

template < typename TElement, Uint32 MaxSize >
RED_INLINE void StaticArray< TElement, MaxSize >::PushBack( const TElement& element )
{	
	typedef typename policies::CopyConstructorExecutorSelector< TElement >::Type CopyConstructorExecutor;

	GrowNoConstruct( 1 );
	CopyConstructorExecutor::Execute( TypedData() + m_size - 1, &element );
}

template < typename TElement, Uint32 MaxSize >
RED_INLINE void StaticArray< TElement, MaxSize >::PushBack( TElement&& element )
{	
	typedef typename policies::MoveConstructorExecutorSelector< TElement >::Type MoveConstructorExecutor;

	GrowNoConstruct( 1 );
	MoveConstructorExecutor::Execute( TypedData() + m_size - 1, &element );
}

template < typename TElement, Uint32 MaxSize >
template < Uint32 OtherMaxSize >
RED_INLINE void StaticArray< TElement, MaxSize >::PushBack( const StaticArray< TElement, OtherMaxSize >& arr )
{	
	typedef typename policies::CopyConstructorExecutorSelector< TElement >::Type CopyConstructorExecutor;

	const Uint32 arrSize = arr.Size();
	GrowNoConstruct( arrSize );
	CopyConstructorExecutor::Execute( TypedData() + m_size - arrSize, arr.TypedData(), arrSize );
}

template < typename TElement, Uint32 MaxSize >
RED_INLINE void StaticArray< TElement, MaxSize >::PushBack( const ArraySpan< const TElement >& arr )
{
	typedef typename policies::CopyConstructorExecutorSelector< TElement >::Type CopyConstructorExecutor;
	const Uint32 arrSize = arr.Size();
	GrowNoConstruct( arrSize );
	CopyConstructorExecutor::Execute( TypedData() + m_size - arrSize, arr.Data(), arrSize );
}

template < typename TElement, Uint32 MaxSize >
RED_INLINE TElement StaticArray< TElement, MaxSize >::PopBack()
{
	RED_FATAL_ASSERT( m_size > 0, "Array: Cannot access last item - array is empty!" );	

	typedef typename policies::DestructorExecutorSelector< TElement >::Type DestructorExecutor;

	--m_size;
	TElement* const data = TypedData() + m_size;
	TElement elem = std::move( *data );
	DestructorExecutor::Execute( data );
	return elem;
}

//////////////////////////////////////////////////////////////////////////

template < typename TElement, Uint32 MaxSize >
RED_INLINE typename StaticArray< TElement, MaxSize >::Result StaticArray< TElement, MaxSize >::Insert( const_iterator it, const TElement& element )
{
	RED_FATAL_ASSERT( it >= Begin() && it <= End(), "Iterator is outside array range" );
	return InsertAt( static_cast< Uint32 >( it - Begin() ), element );
}

template < typename TElement, Uint32 MaxSize >
RED_INLINE typename StaticArray< TElement, MaxSize >::Result StaticArray< TElement, MaxSize >::Insert( const_iterator it, TElement&& element )
{
	RED_FATAL_ASSERT( it >= Begin() && it <= End(), "Iterator is outside array range" );
	return InsertAt( static_cast< Uint32 >( it - Begin() ), std::forward< TElement >( element ) );
}

template < typename TElement, Uint32 MaxSize >
RED_INLINE typename StaticArray< TElement, MaxSize >::Result StaticArray< TElement, MaxSize >::InsertAt( const Uint32 index, const TElement& element )
{
	RED_FATAL_ASSERT( index <= m_size, "Index is outside array range" );

	typedef typename policies::CopyConstructorExecutorSelector< TElement >::Type CopyConstructorExecutor;

	if ( index == m_size )
	{
		PushBack( element );
		return Result::Success( iterator( RED_CHECKED_ITERATOR_THIS TypedData() + m_size - 1 ) );
	}
	else
	{
		ArrayImplUtils::MoveForwardsAt( *this, index );
		TElement* const data = TypedData() + index;
		CopyConstructorExecutor::Execute( data, &element );
		return Result::Success( iterator( RED_CHECKED_ITERATOR_THIS data ) );
	}
}

template < typename TElement, Uint32 MaxSize >
RED_INLINE typename StaticArray< TElement, MaxSize >::Result StaticArray< TElement, MaxSize >::InsertAt( const Uint32 index, TElement&& element )
{
	RED_FATAL_ASSERT( index <= m_size, "Index is outside array range" );

	typedef typename policies::MoveConstructorExecutorSelector< TElement >::Type MoveConstructorExecutor;

	if ( index == m_size )
	{
		PushBack( std::forward< TElement >( element ) );
		return Result::Success( iterator( RED_CHECKED_ITERATOR_THIS TypedData() + m_size - 1 ) );
	}
	else
	{
		ArrayImplUtils::MoveForwardsAt( *this, index );
		TElement* const data = TypedData() + index;
		MoveConstructorExecutor::Execute( data, &element );
		return Result::Success( iterator( RED_CHECKED_ITERATOR_THIS data ) );
	}
}

//////////////////////////////////////////////////////////////////////////

template < typename TElement, Uint32 MaxSize >
template < typename... Args >
RED_INLINE TElement& StaticArray< TElement, MaxSize >::EmplaceBack( Args&&... args )
{	
	GrowNoConstruct( 1 );
	return *( ::new ( TypedData() + m_size - 1 ) TElement( std::forward< Args >( args )... ) );
}

template < typename TElement, Uint32 MaxSize >
template < typename... Args >
RED_INLINE typename StaticArray< TElement, MaxSize >::Result StaticArray< TElement, MaxSize >::Emplace( const_iterator it, Args&&... args )
{	
	RED_FATAL_ASSERT( it >= Begin() && it <= End(), "Iterator is outside array range" );
	return EmplaceAt( static_cast< Uint32 >( it - Begin() ), std::forward< Args >( args )... );
}

template < typename TElement, Uint32 MaxSize >
template < typename... Args >
RED_INLINE typename StaticArray< TElement, MaxSize >::Result StaticArray< TElement, MaxSize >::EmplaceAt( const Uint32 index, Args&&... args )
{	
	RED_FATAL_ASSERT( index <= m_size, "Index is outside array range" );

	if ( index == m_size )
	{
		EmplaceBack( std::forward< Args >( args )... );
		return Result::Success( iterator( RED_CHECKED_ITERATOR_THIS TypedData() + m_size - 1 ) );
	}
	else
	{
		ArrayImplUtils::MoveForwardsAt( *this, index );
		TElement* data = TypedData() + index;
		::new ( data ) TElement( std::forward< Args >( args )... );
		return Result::Success( iterator( RED_CHECKED_ITERATOR_THIS data ) );
	}
}

//////////////////////////////////////////////////////////////////////////

template < typename TElement, Uint32 MaxSize >
RED_INLINE typename StaticArray< TElement, MaxSize >::Result StaticArray< TElement, MaxSize >::Remove( const_iterator it )
{
	RED_FATAL_ASSERT( it >= Begin() && it < End(), "Iterator is outside array range" );
	return RemoveAt( static_cast< Uint32 >( it - Begin() ) );
}

template < typename TElement, Uint32 MaxSize >
RED_INLINE typename StaticArray< TElement, MaxSize >::Result StaticArray< TElement, MaxSize >::Remove( const_iterator first, const_iterator last )
{
	return RemoveAt( static_cast< Uint32 >( first - Begin() ), static_cast< Uint32 >( last - Begin() ) );
}

template < typename TElement, Uint32 MaxSize >
RED_INLINE typename StaticArray< TElement, MaxSize >::Result StaticArray< TElement, MaxSize >::RemoveReorder( const_iterator it )
{
	RED_FATAL_ASSERT( it >= Begin() && it < End(), "Iterator is outside array range" );
	return RemoveAtReorder( static_cast< Uint32 >( it - Begin() ) );
}

template < typename TElement, Uint32 MaxSize >
RED_INLINE typename StaticArray< TElement, MaxSize >::Result StaticArray< TElement, MaxSize >::Remove( const TElement& element )
{
	TElement* const data = TypedData();
	for ( Uint32 i = 0; i < m_size; ++i )
	{
		if ( data[ i ] == element )
		{
			return RemoveAt( i );
		}
	}
	return Result::Failure();
}

template < typename TElement, Uint32 MaxSize >
RED_INLINE typename StaticArray< TElement, MaxSize >::Result StaticArray< TElement, MaxSize >::RemoveReorder( const TElement& element )
{
	TElement* const data = TypedData();
	for ( Uint32 i = 0; i < m_size; ++i )
	{
		if ( data[ i ] == element )
		{
			return RemoveAtReorder( i );
		}
	}
	return Result::Failure();
}

template < typename TElement, Uint32 MaxSize >
RED_INLINE typename StaticArray< TElement, MaxSize >::Result StaticArray< TElement, MaxSize >::RemoveAt( const Uint32 index )
{
	RED_FATAL_ASSERT( index < m_size, "Index is outside array range" );

	TElement* const data = TypedData() + index;
	ArrayImplUtils::MoveBackwards( data, 1, static_cast< Uint32 >( m_size - index - 1 ) );
	--m_size;
	return Result::Success( iterator( RED_CHECKED_ITERATOR_THIS data ) );
}

template < typename TElement, Uint32 MaxSize >
RED_INLINE typename StaticArray< TElement, MaxSize >::Result StaticArray< TElement, MaxSize >::RemoveAt( const Uint32 first, const Uint32 last )
{
	if ( first < last )
	{
		RED_FATAL_ASSERT( first < m_size && last <= m_size, "Index is outside array range" );

		const Uint32 num = last - first;
		if ( num == m_size )
		{
			Clear();
			return Result::Success( Begin() );
		}
		else
		{
			TElement* const data = TypedData() + first;
			ArrayImplUtils::MoveBackwards( data, num, static_cast< Uint32 >( m_size - first - num ) );
			m_size -= num;
			return Result::Success( iterator( RED_CHECKED_ITERATOR_THIS data ) );
		}
	}
	return Result::Failure();
}

template < typename TElement, Uint32 MaxSize >
RED_INLINE typename StaticArray< TElement, MaxSize >::Result StaticArray< TElement, MaxSize >::RemoveAtReorder( const Uint32 index )
{
	RED_FATAL_ASSERT( index < m_size, "Index is outside array range" );

	typedef typename policies::DestructorExecutorSelector< TElement >::Type DestructorExecutor;

	TElement* const data = TypedData() + index;
	TElement* const back = TypedData() + m_size - 1;
	if ( data < back )
	{
		*data = std::move( *back );
	}
	DestructorExecutor::Execute( back );
	--m_size;
	return Result::Success( iterator( RED_CHECKED_ITERATOR_THIS data ) );
}

//////////////////////////////////////////////////////////////////////////

template < typename TElement, Uint32 MaxSize >
RED_INLINE Int32 StaticArray< TElement, MaxSize >::GetIndex( const TElement& element ) const
{
	const_iterator i = std::find( Begin(), End(), element );
	if ( i != End() )
	{
		return static_cast< Int32 >( i - Begin() );
	}
	return INVALID_INDEX;
}

template < typename TElement, Uint32 MaxSize >
RED_INLINE Bool StaticArray< TElement, MaxSize >::Exist( const TElement& element ) const
{
	return std::find( Begin(), End(), element ) != End();
}

template < typename TElement, Uint32 MaxSize >
RED_INLINE TElement* StaticArray< TElement, MaxSize >::FindPtr( const TElement& element )
{
	iterator it = std::find( Begin(), End(), element );
	if ( it != End() )
	{
		return it.operator->();
	}
	return nullptr;
}

template< typename TElement, Uint32 MaxSize >
RED_INLINE const TElement* StaticArray< TElement, MaxSize >::FindPtr( const TElement& element ) const
{
	const_iterator it = std::find( Begin(), End(), element );
	if ( it != End() )
	{
		return it.operator->();
	}
	return nullptr;
}

//////////////////////////////////////////////////////////////////////////

template< typename TElement, Uint32 MaxSize >
RED_INLINE void StaticArray< TElement, MaxSize >::Clear()
{
	typedef typename policies::DestructorExecutorSelector< TElement >::Type DestructorExecutor;

	DestructorExecutor::Execute( TypedData(), m_size );
	m_size = 0;
}

template< typename TElement, Uint32 MaxSize >
RED_INLINE void StaticArray< TElement, MaxSize >::Resize( Uint32 size )
{	
	RED_FATAL_ASSERT( size <= MaxSize, "Cannot resize StaticArray over MaxSize." );

	typedef typename policies::ConstructorExecutorSelector< TElement >::Type	ConstructorExecutor;
	typedef typename policies::DestructorExecutorSelector< TElement >::Type		DestructorExecutor;

	if ( size != m_size )
	{
		const Uint32 oldSize = m_size;
		m_size = size;
		if ( oldSize > m_size )
		{
			DestructorExecutor::Execute( TypedData() + size, oldSize - size );
		}
		else
		{
			ConstructorExecutor::Execute( TypedData() + oldSize, m_size - oldSize );
		}
	}
}
template< typename TElement, Uint32 MaxSize >
RED_INLINE void StaticArray< TElement, MaxSize >::Resize( Uint32 size, const TElement& element )
{
	RED_FATAL_ASSERT( size <= MaxSize, "Cannot resize StaticArray over MaxSize." );

	typedef typename policies::CopyConstructorExecutorSelector< TElement >::Type	CopyConstructorExecutor;
	typedef typename policies::DestructorExecutorSelector< TElement >::Type		DestructorExecutor;

	if ( size != m_size )
	{
		const Uint32 oldSize = m_size;
		m_size = size;
		if ( oldSize > m_size )
		{
			DestructorExecutor::Execute( TypedData() + size, oldSize - size );
		}
		else
		{
			CopyConstructorExecutor::Execute( TypedData() + oldSize, element, m_size - oldSize );
		}
	}
}


template< typename TElement, Uint32 MaxSize >
RED_INLINE void StaticArray< TElement, MaxSize >::Grow( Uint32 amount )
{	
	Resize( m_size + amount );
}

template< typename TElement, Uint32 MaxSize >
RED_INLINE void StaticArray< TElement, MaxSize >::GrowNoConstruct( Uint32 amount )
{
	RED_FATAL_ASSERT( m_size + amount <= MaxSize, "Cannot resize StaticArray over MaxSize." );
	m_size += amount;
}

template< typename TElement, Uint32 MaxSize >
RED_INLINE void StaticArray< TElement, MaxSize >::ResizeBuffer( Uint32 capacity )
{
	RED_FATAL_ASSERT( capacity <= MaxSize, "Cannot resize StaticArray over MaxSize." );
}

} // red