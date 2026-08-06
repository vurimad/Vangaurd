/**
* Copyright (c) 2014 CD Projekt Red. All Rights Reserved.
*/

#ifndef _RED_MEMORY_UNIQUE_BUFFER_HPP_
#define _RED_MEMORY_UNIQUE_BUFFER_HPP_

#include "operators.h"

namespace red
{
namespace internal
{
	class UniqueBufferAllocatorInterface
	{
	public:

		virtual void * ReallocateAligned( void * inputBuffer, Uint32 size, Uint32 alignment ) = 0;
		virtual void Free( void * buffer ) = 0;
		virtual const red::memory::Pool* GetPool() const = 0;
	protected:

		~UniqueBufferAllocatorInterface();
	};

	RED_MEMORY_INLINE UniqueBufferAllocatorInterface::~UniqueBufferAllocatorInterface()
	{}

	//-----

	template< typename Proxy >
	class UniqueBufferAllocator : public UniqueBufferAllocatorInterface
	{
	public:
		UniqueBufferAllocator( Proxy * proxy );

		virtual void * ReallocateAligned( void * inputBuffer, Uint32 size, Uint32 alignment ) override final;
		virtual void Free( void * buffer ) override final;
		virtual const red::memory::Pool* GetPool() const override;
	private:
		Proxy * m_proxy;
	};

	template< typename Proxy >
	const red::memory::Pool* red::internal::UniqueBufferAllocator<Proxy>::GetPool() const
	{
		return nullptr;
	}

	template< typename Proxy >
	RED_MEMORY_INLINE UniqueBufferAllocator< Proxy >::UniqueBufferAllocator( Proxy * proxy )
		: m_proxy( proxy )
	{}

	template< typename Proxy >
	void * UniqueBufferAllocator< Proxy >::ReallocateAligned( void * inputBuffer, Uint32 size, Uint32 alignment )
	{
		return memory::ReallocateAligned( *m_proxy, inputBuffer, size, alignment );
	}

	template< typename Proxy >
	void UniqueBufferAllocator< Proxy >::Free( void * buffer )
	{
		memory::Free( *m_proxy, buffer );
	}

	//-----

	class RED_MEMORY_API UniqueBufferPoolAllocator : public UniqueBufferAllocatorInterface
	{
	public:
		UniqueBufferPoolAllocator( const red::memory::Pool * pool );

		virtual void * ReallocateAligned( void * inputBuffer, Uint32 size, Uint32 alignment ) override final;
		virtual void Free( void * buffer ) override final;
		virtual const red::memory::Pool* GetPool() const override final;
	private:
		 Uint8 m_buffer[sizeof(void*)];
	};

}
	template< typename Pool >
	RED_MEMORY_INLINE UniqueBuffer CreateUniqueBuffer( Uint32 size, Uint32 alignment )
	{
		void * buffer = RED_ALLOCATE_ALIGNED( Pool, size, alignment );
		return MakeUniqueBuffer< Pool >( buffer, size, alignment );
	}

	template< typename Proxy >
	RED_MEMORY_INLINE UniqueBuffer CreateUniqueBuffer( Proxy & proxy, Uint32 size, Uint32 alignment )
	{
		void * buffer = RED_ALLOCATE_ALIGNED( proxy, size, alignment );
		return MakeUniqueBuffer( proxy, buffer, size, alignment );
	}

	template< typename Pool >
	RED_MEMORY_INLINE UniqueBuffer MakeUniqueBuffer( void * buffer, Uint32 size, Uint32 alignment )
	{
		return MakeUniqueBuffer( Pool::GetInstance(), buffer, size, alignment );
	}

	template< typename Proxy >
	RED_MEMORY_INLINE UniqueBuffer MakeUniqueBuffer( Proxy & proxy, void * buffer, Uint32 size, Uint32 alignment )
	{

#ifndef RED_MEMORY_FORCE_DEBUG_ALLOCATOR
		RED_MEMORY_ASSERT( memory::IsAligned( buffer, alignment ), "Buffer is not correctly aligned." );
#endif

		UniqueBuffer result;

		typedef typename std::conditional< 
			std::is_base_of< red::memory::Pool, Proxy >::value,
			internal::UniqueBufferPoolAllocator,
			internal::UniqueBufferAllocator< Proxy >
			>::type AllocatorType;

		static_assert( sizeof( AllocatorType ) <= sizeof( result.m_allocator ), "UniqueBuffer cannot construct inplace its allocator." );

		result.m_buffer = buffer;
		result.m_size = size;
		result.m_alignment = alignment;
		new (result.m_allocator.buffer) AllocatorType( &proxy );

		return result;
	}

	
	template< typename Proxy >
	UniqueBuffer MakeEmptyUniqueBuffer( Proxy & proxy, Uint32 alignment )
	{
		UniqueBuffer result;

		typedef typename std::conditional< 
			std::is_base_of< red::memory::Pool, Proxy >::value,
			internal::UniqueBufferPoolAllocator,
			internal::UniqueBufferAllocator< Proxy >
			>::type AllocatorType;

		static_assert( sizeof( AllocatorType ) <= sizeof( result.m_allocator ), "UniqueBuffer cannot construct inplace its allocator." );

		result.m_alignment = alignment;
		new (result.m_allocator.buffer) AllocatorType( &proxy );

		return result;
	}

	template< typename Pool >
	UniqueBuffer MakeEmptyUniqueBuffer( Uint32 alignment )
	{
		return MakeEmptyUniqueBuffer( Pool::GetInstance(), alignment );
	}

	constexpr UniqueBuffer::UniqueBuffer()
		: m_buffer( nullptr )
		, m_size( 0 )
		, m_alignment( 0 )
		, m_allocator()
	{
	}

	constexpr void * UniqueBuffer::Get() const
	{
		return m_buffer;
	}

	constexpr Uint32 UniqueBuffer::GetSize() const
	{
		return m_size;
	}

	constexpr Uint32 UniqueBuffer::GetAlignment() const
	{
		return m_alignment;
	}

	constexpr void* UniqueBuffer::Data() const
	{
		return m_buffer;
	}

	constexpr Uint32 UniqueBuffer::Size() const
	{
		return m_size;
	}

	RED_MEMORY_INLINE const red::memory::Pool& UniqueBuffer::GetPool() const
	{
		const internal::UniqueBufferAllocatorInterface* allocator = reinterpret_cast<const internal::UniqueBufferAllocatorInterface*>( m_allocator.buffer );
		const red::memory::Pool* pool = allocator->GetPool();
		RED_FATAL_ASSERT( pool, "UniqueBuffer Pool is a nullptr - calling GetPool for the buffer created with an allocator is not allowed and will result in a crash." );
		return *pool;
	}

	constexpr UniqueBuffer::operator bool() const
	{
		return m_buffer != nullptr;
	}

	constexpr bool UniqueBuffer::operator!() const
	{
		return m_buffer == nullptr;
	}

}

#endif
