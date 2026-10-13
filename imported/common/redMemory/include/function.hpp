/**
 * Copyright (c) 2018 CD Projekt Red. All Rights Reserved.
 */

#ifndef _RED_MEMROY_FUNCTION_HPP_
#define _RED_MEMROY_FUNCTION_HPP_

#include "operators.h"

namespace red
{
	template< typename R, typename... Args, Uint32 SmallBufferOptimizationSize, Uint32 Alignment >
	Function< R( Args... ), SmallBufferOptimizationSize, Alignment >::Function()
		: m_vtablePtr( nullptr )
		, m_poolAllocationSize( 0 )
		, m_mode( FunctionMode_Internal )
	{
#if defined( RED_MEMORY_ENABLE_POOL_VALIDATION_IN_FUNCTION )
		red::Memzero( std::addressof( m_pool ), sizeof( red::memory::Pool* ) );
#endif
	}

	template< typename R, typename... Args, Uint32 SmallBufferOptimizationSize, Uint32 Alignment >
	Function< R( Args... ), SmallBufferOptimizationSize, Alignment >::Function( const red::memory::Pool& pool )
		: m_vtablePtr( nullptr )
		, m_poolAllocationSize( 0 )
		, m_mode( FunctionMode_Internal )
	{
		static_assert( sizeof( red::memory::Pool ) == sizeof( void* ), "Memory pool must have the same size as a pointer" );
		red::Memcpy( std::addressof( m_pool ), &pool, sizeof( red::memory::Pool* ) );
	}

	template< typename R, typename... Args, Uint32 SmallBufferOptimizationSize, Uint32 Alignment >
	Function< R( Args... ), SmallBufferOptimizationSize, Alignment >::Function( std::nullptr_t ) noexcept
		: m_vtablePtr( nullptr )
		, m_poolAllocationSize( 0 )
		, m_mode( FunctionMode_Internal )
	{
#if defined( RED_MEMORY_ENABLE_POOL_VALIDATION_IN_FUNCTION )
		red::Memzero( std::addressof( m_pool ), sizeof( red::memory::Pool* ) );
#endif
	}

	template< typename R, typename... Args, Uint32 SmallBufferOptimizationSize, Uint32 Alignment >
	Function< R( Args... ), SmallBufferOptimizationSize, Alignment >::~Function()
	{
		DestroyFunction();
	}

	template< typename R, typename... Args, Uint32 SmallBufferOptimizationSize, Uint32 Alignment >
	template< typename T, typename C,
		typename std::enable_if<
			!std::is_same< C, Function< R( Args... ), SmallBufferOptimizationSize, Alignment > >::value
			&& !std::is_base_of< red::memory::Pool, C >::value
		>::type* >
	Function< R( Args... ), SmallBufferOptimizationSize, Alignment >::Function( T&& closure )
		: m_poolAllocationSize( 0 )
		, m_mode( FunctionMode_Internal )
	{
		static_assert( sizeof( C ) <= SmallBufferOptimizationSize, "Function cannot be constructed from object with that big size" );
		static_assert( Alignment % std::alignment_of< C >::value == 0, "Function cannot be constructed from object with that big alignment" );

		static const VTable< R, Args... > s_vt{ ClosureWrapper< C >{} };
		m_vtablePtr = std::addressof( s_vt );

#if defined( RED_MEMORY_ENABLE_POOL_VALIDATION_IN_FUNCTION )
		red::Memzero( std::addressof( m_pool ), sizeof( red::memory::Pool* ) );
#endif

		CreateFunction( std::forward< T >( closure ) );
	}

	template< typename R, typename... Args, Uint32 SmallBufferOptimizationSize, Uint32 Alignment >
	template< typename T, typename C,
		typename std::enable_if<
			!std::is_same< C, Function< R(Args...), SmallBufferOptimizationSize, Alignment > >::value
			&& !std::is_base_of< red::memory::Pool, C >::value
		>::type* >
	Function< R( Args... ), SmallBufferOptimizationSize, Alignment >::Function( const red::memory::Pool& pool, T&& closure )
		: m_poolAllocationSize( 0 )
		, m_mode( FunctionMode_Internal )
	{
		static_assert( sizeof( red::memory::Pool ) == sizeof( void* ), "Memory pool must have the same size as a pointer" );
		static_assert( Alignment % std::alignment_of< C >::value == 0, "Function cannot be constructed from object with that big alignment" );
		red::Memcpy( std::addressof( m_pool ), &pool, sizeof( red::memory::Pool* ) );

		static const VTable< R, Args... > s_vt{ ClosureWrapper< C >{} };
		m_vtablePtr = std::addressof( s_vt );

		CreateFunction( std::forward< T >( closure ) );
	}

	template< typename R, typename... Args, Uint32 SmallBufferOptimizationSize, Uint32 Alignment >
	Function< R( Args... ), SmallBufferOptimizationSize, Alignment >::Function( const Function& other )
		: m_vtablePtr( other.m_vtablePtr )
		, m_poolAllocationSize( 0 )
		, m_mode( FunctionMode_Internal )
	{
		red::Memcpy( std::addressof( m_pool ), std::addressof( other.m_pool ), sizeof( const red::memory::Pool* ) );

		CopyFunction( other );
	}

	template< typename R, typename... Args, Uint32 SmallBufferOptimizationSize, Uint32 Alignment >
	Function< R( Args... ), SmallBufferOptimizationSize, Alignment >::Function( Function&& other ) noexcept
		: m_vtablePtr( Exchange( other.m_vtablePtr, nullptr ) )
		, m_poolAllocationSize( 0 )
		, m_mode( FunctionMode_Internal )
	{
		red::Memcpy( std::addressof( m_pool ), std::addressof( other.m_pool ), sizeof( const red::memory::Pool* ) );

		MoveFunction( std::move( other ) );
	}

	template< typename R, typename... Args, Uint32 SmallBufferOptimizationSize, Uint32 Alignment >
	Function< R( Args... ), SmallBufferOptimizationSize, Alignment >& Function< R( Args... ), SmallBufferOptimizationSize, Alignment >::operator=( const Function& other )
	{
		if ( this != std::addressof( other ) )
		{
			DestroyFunction();

			red::Memcpy( std::addressof( m_pool ), std::addressof( other.m_pool ), sizeof( const red::memory::Pool* ) );

			m_vtablePtr = other.m_vtablePtr;

			CopyFunction( other );
		}

		return *this;
	}

	template< typename R, typename... Args, Uint32 SmallBufferOptimizationSize, Uint32 Alignment >
	Function< R( Args... ), SmallBufferOptimizationSize, Alignment >& Function< R( Args... ), SmallBufferOptimizationSize, Alignment >::operator=( Function&& other ) noexcept
	{
		if( this != std::addressof( other ) )
		{
			DestroyFunction();

			red::Memcpy( std::addressof( m_pool ), std::addressof( other.m_pool ), sizeof( const red::memory::Pool* ) );
			m_vtablePtr = Exchange( other.m_vtablePtr, nullptr );

			MoveFunction( std::move( other ) );
		}
		return *this;
	}

	template< typename R, typename... Args, Uint32 SmallBufferOptimizationSize, Uint32 Alignment >
	Function< R( Args... ), SmallBufferOptimizationSize, Alignment >& Function< R( Args... ), SmallBufferOptimizationSize, Alignment >::operator=( std::nullptr_t ) noexcept
	{
		DestroyFunction();
		m_vtablePtr = nullptr;

#if defined( RED_MEMORY_ENABLE_POOL_VALIDATION_IN_FUNCTION )
		red::Memzero( std::addressof( m_pool ), sizeof( red::memory::Pool* ) );
#endif
		return *this;
	}

	template< typename R, typename... Args, Uint32 SmallBufferOptimizationSize, Uint32 Alignment >
	template< typename T, typename C,
		typename std::enable_if<
			!std::is_same< C, Function< R(Args...), SmallBufferOptimizationSize, Alignment > >::value
			&& !std::is_base_of< red::memory::Pool, C >::value
		>::type* >
	Function< R(Args...), SmallBufferOptimizationSize, Alignment >& Function< R( Args... ), SmallBufferOptimizationSize, Alignment >::operator=( T&& closure )
	{
		static_assert( sizeof( C ) <= SmallBufferOptimizationSize, "Function cannot be constructed from object with that big size" );
		static_assert( Alignment % std::alignment_of< C >::value == 0, "Function cannot be constructed from object with that big alignment" );

		DestroyFunction();

		static const VTable< R, Args... > s_vt{ ClosureWrapper< C >{} };
		m_vtablePtr = std::addressof( s_vt );

		CreateFunction( std::forward< T >( closure ) );

		return *this;
	}

	template< typename R, typename... Args, Uint32 SmallBufferOptimizationSize, Uint32 Alignment >
	R Function< R( Args... ), SmallBufferOptimizationSize, Alignment >::operator()( Args... args ) const
	{
		RED_MEMORY_ASSERT( m_vtablePtr != nullptr && m_vtablePtr->invokePtr != nullptr, "Trying to call non-existing callable. This should never happen." );
		if ( IsInternal() )
		{
			return m_vtablePtr->invokePtr( reinterpret_cast< void* >( std::addressof( m_stack ) ), std::forward< Args >( args )... );
		}
		else
		{
			void* ptr = reinterpret_cast< void* >( *reinterpret_cast< Uint64* >( std::addressof( m_stack ) ) );
			return m_vtablePtr->invokePtr( ptr, std::forward< Args >( args )... );
		}
	}

	template< typename R, typename... Args, Uint32 SmallBufferOptimizationSize, Uint32 Alignment >
	Function< R( Args... ), SmallBufferOptimizationSize, Alignment >::operator Bool() const noexcept
	{
		return m_vtablePtr != nullptr;
	}

	template< typename R, typename... Args, Uint32 SmallBufferOptimizationSize, Uint32 Alignment >
	RED_INLINE void Function< R( Args... ), SmallBufferOptimizationSize, Alignment >::SetPool( const red::memory::Pool& pool )
	{
		RED_MEMORY_ASSERT( m_poolAllocationSize == 0, "Cannot change pool for Function" );
		red::Memcpy( std::addressof( m_pool ), &pool, sizeof( red::memory::Pool* ) );
	}

	template< typename R, typename... Args, Uint32 SmallBufferOptimizationSize, Uint32 Alignment >
	RED_INLINE const red::memory::Pool& Function< R( Args... ), SmallBufferOptimizationSize, Alignment >::GetPool() const
	{
#if defined( RED_MEMORY_ENABLE_POOL_VALIDATION_IN_FUNCTION )
		std::aligned_storage< sizeof( red::memory::Pool* ), std::alignment_of< red::memory::Pool* >::value >::type emptyStorage;
		red::Memzero( std::addressof( emptyStorage ), sizeof( red::memory::Pool* ) );
		const Bool hasMemoryPool = red::Memcmp( std::addressof( emptyStorage ), std::addressof( m_pool ), sizeof( red::memory::Pool* ) );
		RED_MEMORY_ASSERT( hasMemoryPool, "Function does not have memory pool" );
#endif
		return *reinterpret_cast< const red::memory::Pool* >( std::addressof( m_pool ) );
	}

	template< typename R, typename... Args, Uint32 SmallBufferOptimizationSize, Uint32 Alignment >
	RED_INLINE Bool Function< R( Args... ), SmallBufferOptimizationSize, Alignment >::IsInternal() const
	{
		return m_poolAllocationSize == FunctionMode_Internal;
	}

	template< typename R, typename... Args, Uint32 SmallBufferOptimizationSize, Uint32 Alignment >
	template< typename T, typename C,
		typename std::enable_if<
			!std::is_same< C, Function< R(Args...), SmallBufferOptimizationSize, Alignment > >::value
			&& !std::is_base_of< red::memory::Pool, C >::value
		>::type* >
	RED_INLINE void Function< R(Args...), SmallBufferOptimizationSize, Alignment >::CreateFunction( T&& closure )
	{
		if ( sizeof( C ) <= SmallBufferOptimizationSize )
		{
			CreateInternalFunction( std::forward< T >( closure ) );
		}
		else
		{
			CreateExternalFunction( std::forward< T >( closure ) );
		}
	}

	template< typename R, typename... Args, Uint32 SmallBufferOptimizationSize, Uint32 Alignment >
	template< typename T, typename C,
		typename std::enable_if<
			!std::is_same< C, Function< R(Args...), SmallBufferOptimizationSize, Alignment > >::value
			&& !std::is_base_of< red::memory::Pool, C >::value
		>::type* >
	void RED_NOINLINE Function< R(Args...), SmallBufferOptimizationSize, Alignment >::CreateExternalFunction( T&& closure )
	{
		m_poolAllocationSize = sizeof( C );
		m_mode = FunctionMode_External;

		void* ptr = RED_ALLOCATE_ALIGNED( GetPool(), m_poolAllocationSize, Alignment );
		*reinterpret_cast< Uint64* >( std::addressof( m_stack ) ) = reinterpret_cast< Uint64 >( ptr );
		::new ( ptr ) C{ std::forward< T >( closure ) };
	}

	template< typename R, typename... Args, Uint32 SmallBufferOptimizationSize, Uint32 Alignment >
	template< typename T, typename C,
		typename std::enable_if<
			!std::is_same< C, Function< R(Args...), SmallBufferOptimizationSize, Alignment > >::value
			&& !std::is_base_of< red::memory::Pool, C >::value
		>::type* >
	RED_INLINE void Function< R(Args...), SmallBufferOptimizationSize, Alignment >::CreateInternalFunction( T&& closure )
	{
		m_mode = FunctionMode_Internal;
		::new ( std::addressof( m_stack ) ) C{ std::forward< T >( closure )  };
	}

	template< typename R, typename... Args, Uint32 SmallBufferOptimizationSize, Uint32 Alignment >
	RED_INLINE void Function< R(Args...), SmallBufferOptimizationSize, Alignment >::DestroyFunction()
	{
		if ( !m_vtablePtr )
		{
			return;
		}

		if ( IsInternal() )
		{
			DestroyInternalFunction();
		}
		else
		{
			DestroyExternalFunction();
		}
	} 

	template< typename R, typename... Args, Uint32 SmallBufferOptimizationSize, Uint32 Alignment >
	RED_INLINE void Function< R(Args...), SmallBufferOptimizationSize, Alignment >::DestroyInternalFunction()
	{
		RED_MEMORY_ASSERT( m_vtablePtr->destructorPtr != nullptr, "Trying to destroy non-existing callable. This should never happen." );
		m_vtablePtr->destructorPtr( std::addressof( m_stack ) );
	}

	template< typename R, typename... Args, Uint32 SmallBufferOptimizationSize, Uint32 Alignment >
	void RED_NOINLINE Function< R(Args...), SmallBufferOptimizationSize, Alignment >::DestroyExternalFunction()
	{
		void* ptr = reinterpret_cast< void* >( *reinterpret_cast< Uint64* >( std::addressof( m_stack ) ) );
		m_vtablePtr->destructorPtr( ptr );

		if ( ptr )
		{
			RED_FREE( GetPool(), ptr );
			m_poolAllocationSize = 0;
		}
	}

	template< typename R, typename... Args, Uint32 SmallBufferOptimizationSize, Uint32 Alignment >
	RED_INLINE void Function< R(Args...), SmallBufferOptimizationSize, Alignment >::CopyFunction( const Function& other )
	{
		if ( other.IsInternal() )
		{
			CopyInternalFunction( other );
		}
		else
		{
			CopyExternalFunction( other );
		}
	}

	template< typename R, typename... Args, Uint32 SmallBufferOptimizationSize, Uint32 Alignment >
	RED_INLINE void Function< R(Args...), SmallBufferOptimizationSize, Alignment >::CopyInternalFunction( const Function& other )
	{
		m_mode = FunctionMode_Internal;

		if ( m_vtablePtr )
		{
			RED_MEMORY_ASSERT( m_vtablePtr->copyPtr != nullptr, "Trying to copy non-copyable callable. This should never happen." );
			m_vtablePtr->copyPtr( std::addressof( m_stack ), std::addressof( other.m_stack ) );
		}
	}

	template< typename R, typename... Args, Uint32 SmallBufferOptimizationSize, Uint32 Alignment >
	void RED_NOINLINE Function< R(Args...), SmallBufferOptimizationSize, Alignment >::CopyExternalFunction( const Function& other )
	{
		void* ptr = RED_ALLOCATE_ALIGNED( GetPool(), other.m_poolAllocationSize, Alignment );
		*reinterpret_cast< Uint64* >( std::addressof( m_stack ) ) = reinterpret_cast< Uint64 >( ptr );
		m_poolAllocationSize = other.m_poolAllocationSize;
		m_mode = FunctionMode_External;

		void* address = reinterpret_cast< void* >( *reinterpret_cast< Uint64* >( std::addressof( other.m_stack ) ) );
		RED_MEMORY_ASSERT( m_vtablePtr->copyPtr != nullptr, "Trying to copy non-copyable callable. This should never happen." );
		m_vtablePtr->copyPtr( ptr, address );
	}

	template< typename R, typename... Args, Uint32 SmallBufferOptimizationSize, Uint32 Alignment >
	RED_INLINE void Function< R(Args...), SmallBufferOptimizationSize, Alignment >::MoveFunction( Function&& other )
	{
		if ( other.IsInternal() )
		{
			MoveInternalFunction( std::move( other ) );
		}
		else
		{
			MoveExternalFunction( std::move( other ) );
		}

		other.m_poolAllocationSize = 0;
		other.m_mode = FunctionMode_Internal;
	}

	template< typename R, typename... Args, Uint32 SmallBufferOptimizationSize, Uint32 Alignment >
	RED_INLINE void Function< R(Args...), SmallBufferOptimizationSize, Alignment >::MoveInternalFunction( Function&& other )
	{
		m_mode = FunctionMode_Internal;
		if ( m_vtablePtr )
		{
			RED_MEMORY_ASSERT( m_vtablePtr->relocatePtr != nullptr, "Trying to relocate non-movable callable. This should never happen." );
			m_vtablePtr->relocatePtr( std::addressof( m_stack ), std::addressof( other.m_stack ) );
		}
	}

	template< typename R, typename... Args, Uint32 SmallBufferOptimizationSize, Uint32 Alignment >
	void RED_NOINLINE Function< R(Args...), SmallBufferOptimizationSize, Alignment >::MoveExternalFunction( Function&& other )
	{
		*reinterpret_cast< Uint64* >( std::addressof (m_stack ) ) = *reinterpret_cast< Uint64* >( std::addressof( other.m_stack ) );
		m_poolAllocationSize = other.m_poolAllocationSize;
		m_mode = FunctionMode_External;
	}
}

#endif