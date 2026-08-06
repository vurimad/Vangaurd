/*
* Copyright (c) 2015-16 CD Projekt Red. All Rights Reserved.
*/

#pragma once

#include "policies.h"
#include "arrayImplUtils.h"
#include "arraySpan.h"

namespace red 
{

//////////////////////////////////////////////////////////////////////////

	template < typename TElement >
	RED_INLINE DynArray< TElement >::DynArray( const red::memory::Pool& pool )
		: DynamicBuffer( pool )
		, m_size( 0 )
	{
	}

	template < typename TElement >
	DynArray< TElement >::DynArray( const DynArray& other )
		: DynamicBuffer( other.GetPool() )
		, m_size( 0 )
	{
		ArrayImplUtils::Copy( other.Begin(), other.End(), *this );
	}

	template < typename TElement >
	DynArray< TElement >::DynArray( const DynArray& other, const red::memory::Pool& pool )
		: DynamicBuffer( pool )
		, m_size( 0 )
	{
		ArrayImplUtils::Copy( other.Begin(), other.End(), *this );
	}

	template < typename TElement >
	DynArray< TElement >::DynArray( DynArray&& other )
	{
		m_buffer = other.m_buffer;
		m_capacity = other.m_capacity;
		m_size = other.m_size;

		other.m_capacity = 0;
		other.m_size = 0;
		// put back other array's pool information
		red::Memcpy( &other.m_buffer, GetPoolStoragePtr( sizeof( TElement ) ), sizeof( red::memory::Pool ) );
	}

	template < typename TElement >
	DynArray< TElement >::DynArray( std::initializer_list< TElement > initializerList, const red::memory::Pool& pool )
		: DynamicBuffer( pool )
		, m_size( 0 )
	{
		typedef typename policies::CopyConstructorExecutorSelector< TElement >::Type CopyConstructorExecutor;

		const Uint32 size = static_cast< Uint32 >( initializerList.size() );
		ResizeBuffer( size );
		CopyConstructorExecutor::Execute( TypedData(), initializerList.begin(), size );
		m_size = size;
	}

	template < typename TElement >
	DynArray< TElement >::DynArray( Uint32 size, const red::memory::Pool& pool )
		: DynamicBuffer( pool )
		, m_size( 0 )
	{
		Resize( size );
	}

	template < typename TElement >
	DynArray< TElement >::DynArray( const red::ArraySpan< const TElement >& span, const red::memory::Pool& pool )
		: DynamicBuffer( pool )
		, m_size( 0 )
	{
		ArrayImplUtils::Copy( span.begin(), span.end(), *this );
	}

	template < typename TElement >
	RED_INLINE DynArray< TElement >::~DynArray()
	{
		if( m_capacity )
		{
			typedef typename policies::DestructorExecutorSelector< TElement >::Type	DestructorExecutor;
			DestructorExecutor::Execute( TypedData(), m_size );
			m_size = 0;
			ReleaseBuffer( sizeof( TElement ), __alignof( TElement ) );
		}
	}

	//////////////////////////////////////////////////////////////////////////

	template < typename TElement >
	DynArray< TElement >& DynArray< TElement >::operator=( const DynArray& other )
	{
		if ( &other != this )
		{
			ArrayImplUtils::Copy( other.Begin(), other.End(), *this );
		}
		return *this;
	}

	template < typename TElement >
	DynArray< TElement >& DynArray< TElement >::operator=( DynArray&& other )
	{
		DynArray( std::move( other ) ).Swap( *this );
		return *this;
	}

	template < typename TElement >
	DynArray< TElement >& DynArray< TElement >::operator=( const red::ArraySpan< const TElement >& span )
	{
		ArrayImplUtils::Copy( span.begin(), span.end(), *this );
		return *this;
	}

	template < typename TElement >
	DynArray< TElement >& DynArray< TElement >::operator=( std::initializer_list< TElement > initializerList )
	{
		ArrayImplUtils::Copy( initializerList.begin(), initializerList.end(), *this );
		return *this;
	}

	template < typename TElement >
	RED_INLINE void DynArray< TElement >::Swap( DynArray& other )
	{
		using std::swap;
		swap( m_buffer, other.m_buffer );
		swap( m_capacity, other.m_capacity );
		swap( m_size, other.m_size );
	}

	//////////////////////////////////////////////////////////////////////////

	template < typename TElement >
	RED_INLINE TElement& DynArray< TElement >::operator[]( Uint32 i )
	{
		RED_FATAL_ASSERT( i < m_size, "Array: Out of bounds. Cannot access item %i as the array is only size %u", i, m_size );
		return reinterpret_cast< TElement* >( m_buffer )[ i ];
	}

	template < typename TElement >
	RED_INLINE const TElement& DynArray< TElement >::operator[]( Uint32 i ) const
	{
		RED_FATAL_ASSERT( i < m_size, "Array: Out of bounds. Cannot access item %i as the array is only size %u", i, m_size );
		return reinterpret_cast< TElement* >( m_buffer )[ i ];
	}

	template < typename TElement >
	RED_INLINE TElement& DynArray< TElement >::Front()
	{
		RED_FATAL_ASSERT( m_size > 0, "Array: Cannot access frist item - array is empty!" );
		return reinterpret_cast< TElement* >( m_buffer )[ 0 ];
	}

	template < typename TElement >
	RED_INLINE const TElement& DynArray< TElement >::Front() const
	{
		RED_FATAL_ASSERT( m_size > 0, "Array: Cannot access frist item - array is empty!" );
		return reinterpret_cast< TElement* >( m_buffer )[ 0 ];
	}

	template < typename TElement >
	RED_INLINE TElement& DynArray< TElement >::Back()
	{
		RED_FATAL_ASSERT( m_size > 0, "Array: Cannot access last item - array is empty!" );
		return reinterpret_cast< TElement* >( m_buffer )[ m_size - 1 ];
	}

	template < typename TElement >
	RED_INLINE const TElement& DynArray< TElement >::Back() const
	{
		RED_FATAL_ASSERT( m_size > 0, "Array: Cannot access last item - array is empty!" );
		return reinterpret_cast< TElement* >( m_buffer )[ m_size - 1 ];
	}

	//////////////////////////////////////////////////////////////////////////

	template < typename TElement >
	Bool DynArray< TElement >::operator==( const DynArray& other ) const
	{
		typedef typename policies::ComparePolicySelector< TElement >::Type ComparePolicy;

		return ( m_size == other.m_size && ComparePolicy::Equal( TypedData(), other.TypedData(), m_size ) );
	}

	template < typename TElement >
	Bool DynArray< TElement >::operator!=( const DynArray& other ) const
	{
		return !( *this == other );
	}

	//////////////////////////////////////////////////////////////////////////

	template < typename TElement >
	void DynArray< TElement >::PushBack( const TElement& element )
	{	
		typedef typename policies::CopyConstructorExecutorSelector< TElement >::Type CopyConstructorExecutor;

		const bool arrayOwnElememt = red::memory::AddressOf( &element ) - red::memory::AddressOf( m_buffer ) < DataSize();
		const Int64 index = red::Distance< Int64 >( const_cast< const TElement* >( TypedData() ), &element );
		GrowNoConstruct( 1 );
		if( arrayOwnElememt )
		{
			// ctremblay: !!WARNING!! pushing back element from the same array, buffer could be reallocated so the reference may not be valid anymore
			TElement* const data = TypedData();
			CopyConstructorExecutor::Execute( data + m_size - 1, data + index );
		}
		else
		{
			CopyConstructorExecutor::Execute( TypedData() + m_size - 1, &element );
		}
	}

	template < typename TElement >
	void DynArray< TElement >::PushBack( TElement&& element )
	{	
		typedef typename policies::MoveConstructorExecutorSelector< TElement >::Type MoveConstructorExecutor;

		const bool arrayOwnElememt = red::memory::AddressOf( &element ) - red::memory::AddressOf( m_buffer ) < DataSize();
		const Int64 index = red::Distance< Int64 >( TypedData(), &element );

		GrowNoConstruct( 1 );
		if( arrayOwnElememt )
		{
			// ctremblay: !!WARNING!! pushing back element from the same array, buffer could be reallocated so the reference may not be valid anymore
			TElement* const data = TypedData();
		
			MoveConstructorExecutor::Execute( data + m_size - 1, data + index );
		}
		else
		{
			MoveConstructorExecutor::Execute( TypedData() + m_size - 1, &element );
		}
	}

	template < typename TElement >
	void DynArray< TElement >::PushBack( const DynArray& arr )
	{	
		typedef typename policies::CopyConstructorExecutorSelector< TElement >::Type CopyConstructorExecutor;

		const Uint32 arrSize = arr.Size();
		GrowNoConstruct( arrSize );
		CopyConstructorExecutor::Execute( TypedData() + m_size - arrSize, arr.TypedData(), arrSize );
	}

	template < typename TElement >
	void DynArray< TElement >::PushBack( const ArraySpan< const TElement >& arr )
	{
		typedef typename policies::CopyConstructorExecutorSelector< TElement >::Type CopyConstructorExecutor;

		const Uint32 arrSize = arr.Size();
		GrowNoConstruct( arrSize );
		CopyConstructorExecutor::Execute( TypedData() + m_size - arrSize, arr.Data(), arrSize );
	}

	template < typename TElement >
	void DynArray< TElement >::PushBackUnchecked( const TElement& element )
	{	
		typedef typename policies::CopyConstructorExecutorSelector< TElement >::Type CopyConstructorExecutor;

		m_size++;
		CopyConstructorExecutor::Execute( TypedData() + m_size - 1, &element );
	}

	template < typename TElement >
	void DynArray< TElement >::PushBackUnchecked( TElement&& element )
	{	
		typedef typename policies::MoveConstructorExecutorSelector< TElement >::Type MoveConstructorExecutor;

		m_size++;
		MoveConstructorExecutor::Execute( TypedData() + m_size - 1, &element );
	}

	template < typename TElement >
	TElement DynArray< TElement >::PopBack()
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

	template < typename TElement >
	typename DynArray< TElement >::Result DynArray< TElement >::Insert( const_iterator it, const TElement& element )
	{
		RED_FATAL_ASSERT( it >= Begin() && it <= End(), "Iterator is outside array range" );
		return InsertAt( static_cast< Uint32 >( it - Begin() ), element );
	}

	template < typename TElement >
	typename DynArray< TElement >::Result DynArray< TElement >::Insert( const_iterator it, TElement&& element )
	{
		RED_FATAL_ASSERT( it >= Begin() && it <= End(), "Iterator is outside array range" );
		return InsertAt( static_cast< Uint32 >( it - Begin() ), std::forward< TElement >( element ) );
	}

	template < typename TElement >
	typename DynArray< TElement >::Result DynArray< TElement >::Insert( const_iterator it, const red::ArraySpan< TElement >& elements )
	{
		RED_FATAL_ASSERT( it >= Begin() && it <= End(), "Iterator is outside array range" );
		return InsertAt( static_cast< Uint32 >( it - Begin() ), elements );
	}

	template < typename TElement >
	typename DynArray< TElement >::Result DynArray< TElement >::InsertAt( const Uint32 index, const TElement& element )
	{
		RED_FATAL_ASSERT( index <= m_size, "Index is outside array range" );

		typedef typename policies::CopyConstructorExecutorSelector< TElement >::Type CopyConstructorExecutor;

		if ( index == m_size )
		{
			PushBack( element );
			return Result::Success( iterator( RED_CHECKED_ITERATOR_THIS TypedData() + index ) );
		}
		else
		{
			ArrayImplUtils::MoveForwardsAt( *this, index );
			TElement* const data = TypedData() + index;
			CopyConstructorExecutor::Execute( data, &element );
			return Result::Success( iterator( RED_CHECKED_ITERATOR_THIS data ) );
		}
	}

	template < typename TElement >
	typename DynArray< TElement >::Result DynArray< TElement >::InsertAt( const Uint32 index, TElement&& element )
	{
		RED_FATAL_ASSERT( index <= m_size, "Index is outside array range" );

		typedef typename policies::MoveConstructorExecutorSelector< TElement >::Type MoveConstructorExecutor;

		if ( index == m_size )
		{
			PushBack( std::forward< TElement >( element ) );
			return Result::Success( iterator( RED_CHECKED_ITERATOR_THIS TypedData() + index ) );
		}
		else
		{
			ArrayImplUtils::MoveForwardsAt( *this, index );
			TElement* const data = TypedData() + index;
			MoveConstructorExecutor::Execute( data, &element );
			return Result::Success( iterator( RED_CHECKED_ITERATOR_THIS data ) );
		}
	}

	// TODO: Add test for this but no time right now
	template < typename TElement >
	typename DynArray< TElement >::Result DynArray< TElement >::InsertAt( const Uint32 index, const red::ArraySpan< TElement >& elements )
	{
		RED_FATAL_ASSERT( index <= m_size, "Index is outside array range" );

		typedef typename policies::CopyConstructorExecutorSelector< TElement >::Type CopyConstructorExecutor;

		if ( index == m_size )
		{
			PushBack( elements );
			return Result::Success( iterator( RED_CHECKED_ITERATOR_THIS TypedData() + index ) );
		}
		else
		{
			const auto elemSize = elements.Size();
			const auto elemsToMove = m_size - index;
			GrowNoConstruct( elemSize );
			TElement* const data = TypedData() + index;
			ArrayImplUtils::MoveForwards( data, elemSize, elemsToMove );
			CopyConstructorExecutor::Execute( data, elements.Data(), elemSize );
			return Result::Success( iterator( RED_CHECKED_ITERATOR_THIS data ) );
		}
	}


	//////////////////////////////////////////////////////////////////////////

	template < typename TElement >
	template < typename... Args >
	TElement& DynArray< TElement >::EmplaceBack( Args&&... args )
	{	
		GrowNoConstruct( 1 );
		return *( ::new ( TypedData() + m_size - 1 ) TElement( std::forward< Args >( args )... ) );
	}

	template < typename TElement >
	template < typename... Args >
	typename DynArray< TElement >::Result DynArray< TElement >::Emplace( const_iterator it, Args&&... args )
	{	
		RED_FATAL_ASSERT( it >= Begin() && it <= End(), "Iterator is outside array range" );
		return EmplaceAt( static_cast< Uint32 >( it - Begin() ), std::forward< Args >( args )... );
	}

	template < typename TElement >
	template < typename... Args >
	typename DynArray< TElement >::Result DynArray< TElement >::EmplaceAt( const Uint32 index, Args&&... args )
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

	template < typename TElement >
	typename DynArray< TElement >::Result DynArray< TElement >::Remove( const_iterator it )
	{
		RED_FATAL_ASSERT( it >= Begin() && it < End(), "Iterator is outside array range" );
		return RemoveAt( static_cast< Uint32 >( it - Begin() ) );
	}

	template < typename TElement >
	typename DynArray< TElement >::Result DynArray< TElement >::Remove( const_iterator first, const_iterator last )
	{
		return RemoveAt( static_cast< Uint32 >( first - Begin() ), static_cast< Uint32 >( last - Begin() ) );
	}

	template < typename TElement >
	typename DynArray< TElement >::Result DynArray< TElement >::RemoveReorder( const_iterator it )
	{
		RED_FATAL_ASSERT( it >= Begin() && it < End(), "Iterator is outside array range" );
		return RemoveAtReorder( static_cast< Uint32 >( it - Begin() ) );
	}

	template < typename TElement >
	typename DynArray< TElement >::Result DynArray< TElement >::Remove( const TElement& element )
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

	template < typename TElement >
	typename DynArray< TElement >::Result DynArray< TElement >::RemoveReorder( const TElement& element )
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

	template < typename TElement >
	typename DynArray< TElement >::Result DynArray< TElement >::RemoveAt( const Uint32 index )
	{
		RED_FATAL_ASSERT( index < m_size, "Index is outside array range" );

		TElement* const data = TypedData() + index;
		ArrayImplUtils::MoveBackwards( data, 1, static_cast< Uint32 >( m_size - index - 1 ) );
		--m_size;
		return Result::Success( iterator( RED_CHECKED_ITERATOR_THIS data ) );
	}

	template < typename TElement >
	typename DynArray< TElement >::Result DynArray< TElement >::RemoveAt( const Uint32 first, const Uint32 last )
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

	template < typename TElement >
	typename DynArray< TElement >::Result DynArray< TElement >::RemoveAtReorder( const Uint32 index )
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

	template < typename TElement >
	Int32 DynArray< TElement >::GetIndex( const TElement& element ) const
	{
		const_iterator i = std::find( Begin(), End(), element );
		if ( i != End() )
		{
			return static_cast< Int32 >( i - Begin() );
		}
		return INVALID_INDEX;
	}

	template < typename TElement >
	Bool DynArray< TElement >::Exist( const TElement& element ) const
	{
		return std::find( Begin(), End(), element ) != End();
	}

	template < typename TElement >
	TElement* DynArray< TElement >::FindPtr( const TElement& element )
	{
		iterator it = std::find( Begin(), End(), element );
		if ( it != End() )
		{
			return it.operator->();
		}
		return nullptr;
	}

	template < typename TElement >
	const TElement* DynArray< TElement >::FindPtr( const TElement& element ) const
	{
		const_iterator it = std::find( Begin(), End(), element );
		if ( it != End() )
		{
			return it.operator->();
		}
		return nullptr;
	}

	//////////////////////////////////////////////////////////////////////////

	template < typename TElement >
	void DynArray< TElement >::Clear()
	{
		typedef typename policies::DestructorExecutorSelector< TElement >::Type DestructorExecutor;

		DestructorExecutor::Execute( TypedData(), m_size );
		m_size = 0;

#if defined( RED_MEMORY_FORCE_DEBUG_ALLOCATOR )
		Shrink();
#endif
	}

	template < typename TElement >
	void DynArray< TElement >::Resize( Uint32 size )
	{
		RED_WARNING( size < m_size || ( size - m_size ) < 0x1000000, "DynArray size is rapidly growing. Is that intended?" );

		typedef typename policies::ConstructorExecutorSelector< TElement >::Type	ConstructorExecutor;
		typedef typename policies::DestructorExecutorSelector< TElement >::Type		DestructorExecutor;

		if ( m_size > size )
		{
			DestructorExecutor::Execute( TypedData() + size, m_size - size );
			// do not "downsize" buffer
			m_size = size;
		}
		else if ( m_size < size )
		{
			// assure that the buffer has at least "size" capacity
			if ( size > Capacity() )
			{
				ResizeBuffer( size );
			}
			ConstructorExecutor::Execute( TypedData() + m_size, size - m_size );
			m_size = size;
		}
	}

	template < typename TElement >
	void DynArray< TElement >::Resize( Uint32 size, const TElement& element )
	{
		RED_WARNING( size < m_size || ( size - m_size ) < 0x1000000, "DynArray size is rapidly growing. Is that intended?" );

		typedef typename policies::CopyConstructorExecutorSelector< TElement >::Type	CopyConstructorExecutor;
		typedef typename policies::DestructorExecutorSelector< TElement >::Type			DestructorExecutor;

		if ( m_size > size )
		{
			DestructorExecutor::Execute( TypedData() + size, m_size - size );
			// do not "downsize" buffer
			m_size = size;
		}
		else if ( m_size < size )
		{
			// assure that the buffer has at least "size" capacity
			if ( size > Capacity() )
			{
				ResizeBuffer( size );
			}

			CopyConstructorExecutor::Execute( TypedData() + m_size, element, size - m_size );
			m_size = size;
		}
	}

	template < typename TElement >
	void DynArray< TElement >::Grow( Uint32 amount )
	{	
		typedef typename policies::ConstructorExecutorSelector< TElement >::Type	ConstructorExecutor;

		const Uint32 newSize = m_size + amount;
		EnsureBufferSize( newSize );
		ConstructorExecutor::Execute( TypedData() + m_size, amount );
		m_size = newSize;
	}

	template < typename TElement >
	void DynArray< TElement >::Reserve( Uint32 capacity )
	{	
		if ( Capacity() < capacity )
		{
			ResizeBuffer( capacity );
		}
	}

	template < typename TElement >
	void DynArray< TElement >::Shrink()
	{
		if ( Capacity() > m_size )
		{
			ResizeBuffer( m_size );
		}
	}

	template < typename TElement >
	void DynArray< TElement >::EnsureBufferSize( Uint32 desiredSize )
	{
		if ( desiredSize > Capacity() )
		{
			ResizeBuffer( ArrayImplUtils::CalcResizeCapacity( desiredSize, Capacity() ) );
		}
	}

	template < typename TElement >
	void DynArray< TElement >::GrowNoConstruct( Uint32 amount )
	{
		const Uint32 newSize = m_size + amount;
		EnsureBufferSize( newSize );
		m_size = newSize;
	}

	template < typename TElement >
	void DynArray< TElement >::ResizeBuffer( Uint32 capacity )
	{
		constexpr Bool isTriviallyMovable = ( std::is_trivially_move_constructible< TElement >::value && std::is_trivially_destructible< TElement >::value );
		DynamicBuffer::ResizeBuffer( capacity, sizeof( TElement ), __alignof( TElement ), isTriviallyMovable || m_size == 0 ? nullptr : MoveAfterReallocation );
	}

	template < typename TElement >
	void DynArray< TElement >::MoveAfterReallocation( void* dst, void* src, Uint32 memorySize, const void* context )
	{
		typedef typename policies::MoveConstructorExecutorSelector< TElement >::Type	MoveConstructorExecutor;
		typedef typename policies::DestructorExecutorSelector< TElement >::Type			DestructorExecutor;

		if ( memorySize == 0 )
		{
			return;
		}

		const DynArray* arr = reinterpret_cast< const DynArray* >( context );
		const Uint32 size = arr->Size();
		MoveConstructorExecutor::Execute( static_cast< TElement *>( dst ), static_cast< TElement* >( src ), size );
		DestructorExecutor::Execute( static_cast< TElement* >( src ), size );
	}

//////////////////////////////////////////////////////////////////////////

	template < typename TElement >
	RED_INLINE void DynArray< TElement >::SetPool( const red::memory::Pool& pool )
	{
		RED_FATAL_ASSERT( m_capacity == 0, "Cannot change pool for already allocated DynArray" );
		red::Memcpy( &m_buffer, &pool, sizeof( red::memory::Pool ) );
	}

	template < typename TElement >
	RED_INLINE const red::memory::Pool& DynArray< TElement >::GetPool() const
	{
		return *reinterpret_cast< red::memory::Pool* >( DynamicBuffer::GetPoolStoragePtr( sizeof( TElement ) ) );
	}

	template < typename TElement >
	RED_INLINE TElement* DynArray< TElement >::TypedData() 
	{ 
		return reinterpret_cast< TElement* >( m_buffer ); 
	}
	
	template < typename TElement >
	RED_INLINE const TElement* DynArray< TElement >::TypedData() const 
	{ 
		return reinterpret_cast< const TElement* >( m_buffer ); 
	}
	
	template < typename TElement >
	RED_INLINE Uint32 DynArray< TElement >::Size() const 
	{ 
		return m_size; 
	}
	
	template < typename TElement >
	RED_INLINE Uint32 DynArray< TElement >::DataSize() const 
	{ 
		return m_size * sizeof( TElement ); 
	}
	
	template < typename TElement >
	RED_INLINE Uint32 DynArray< TElement >::DataCapacity() const 
	{ 
		return m_capacity * sizeof( TElement ); 
	}

	template < typename TElement >
	RED_INLINE Bool DynArray< TElement >::Empty() const 
	{ 
		return m_size == 0;
	}

	template < typename TElement >
	RED_INLINE typename DynArray< TElement >::iterator DynArray< TElement >::Begin() 
	{ 
		return iterator( RED_CHECKED_ITERATOR_THIS reinterpret_cast< TElement* >( m_buffer ) );
	}
	
	template < typename TElement >
	RED_INLINE typename DynArray< TElement >::iterator DynArray< TElement >::End() 
	{ 
		return iterator( RED_CHECKED_ITERATOR_THIS reinterpret_cast< TElement* >( m_buffer ) + m_size );
	}
	
	template < typename TElement >
	RED_INLINE typename DynArray< TElement >::const_iterator DynArray< TElement >::Begin() const 
	{ 
		return const_iterator( RED_CHECKED_ITERATOR_THIS reinterpret_cast< TElement* >( m_buffer ) );
	}
	
	template < typename TElement >
	RED_INLINE typename DynArray< TElement >::const_iterator DynArray< TElement >::End() const 
	{ 
		return const_iterator( RED_CHECKED_ITERATOR_THIS reinterpret_cast< TElement* >( m_buffer ) + m_size );
	}
	
	template < typename TElement >
	RED_INLINE typename DynArray< TElement >::reverse_iterator DynArray< TElement >::RBegin() 
	{ 
		return reverse_iterator( End() ); 
	}
	
	template < typename TElement >
	RED_INLINE typename DynArray< TElement >::reverse_iterator DynArray< TElement >::REnd() 
	{ 
		return reverse_iterator( Begin() ); 
	}
	
	template < typename TElement >
	RED_INLINE typename DynArray< TElement >::reverse_const_iterator DynArray< TElement >::RBegin() const 
	{ 
		return reverse_const_iterator( End() ); 
	}
	
	template < typename TElement >
	RED_INLINE typename DynArray< TElement >::reverse_const_iterator DynArray< TElement >::REnd() const 
	{ 
		return reverse_const_iterator( Begin() ); 
	}
	
	template < typename TElement >
	RED_INLINE typename DynArray< TElement >::ReverseIteration DynArray< TElement >::Reverse() 
	{
		return ReverseIteration( *this ); 
	}
	
	template < typename TElement >
	RED_INLINE typename DynArray< TElement >::ReverseConstIteration DynArray< TElement >::Reverse() const 
	{ 
		return ReverseConstIteration( *this ); 
	}

	template < typename TElement >
	RED_INLINE IndexRange DynArray< TElement >::Indices() const 
	{ 
		return IndexRange( 0, m_size );
	}
	
	template < typename TElement >
	RED_INLINE ReverseIndexRange DynArray< TElement >::ReverseIndices() const 
	{ 
		return ReverseIndexRange( m_size, 0 );
	}

	template < typename TElement >
	RED_INLINE typename DynArray< TElement >::iterator begin( DynArray< TElement >& arr )
	{
		return arr.Begin();
	}

	template < typename TElement >
	RED_INLINE typename DynArray< TElement >::iterator end( DynArray< TElement >& arr )
	{
		return arr.End();
	}

	template < typename TElement >
	RED_INLINE typename DynArray< TElement >::const_iterator begin( const DynArray< TElement >& arr )
	{
		return arr.Begin();
	}

	template < typename TElement >
	RED_INLINE typename DynArray< TElement >::const_iterator end( const DynArray< TElement >& arr )
	{
		return arr.End();
	}

	//////////////////////////////////////////////////////////////////////////
	// ArraySpan compatibility

	template < typename TElement >
	RED_INLINE TElement* GetStartPtr( DynArray< TElement >& arr )
	{
		return arr.TypedData();
	}

	template < typename TElement >
	RED_INLINE TElement* GetEndPtr( DynArray< TElement >& arr )
	{
		return arr.TypedData() + arr.Size();
	}

	template < typename TElement >
	RED_INLINE const TElement* GetStartPtr( const DynArray< TElement >& arr )
	{
		return arr.TypedData();
	}

	template < typename TElement >
	RED_INLINE const TElement* GetEndPtr( const DynArray< TElement >& arr )
	{
		return arr.TypedData() + arr.Size();
	}

} // red