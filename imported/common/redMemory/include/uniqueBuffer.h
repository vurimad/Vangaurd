/**
* Copyright (c) 2014 CD Projekt Red. All Rights Reserved.
*/

#ifndef _RED_MEMORY_UNIQUE_BUFFER_H_
#define _RED_MEMORY_UNIQUE_BUFFER_H_

#include "poolUtils.h"

namespace red
{
	class RED_MEMORY_API UniqueBuffer
	{
		RED_USE_MEMORY_POOL( red::PoolEngine );

	public:
		constexpr UniqueBuffer();
		UniqueBuffer( UniqueBuffer && rvalue );
		UniqueBuffer( const UniqueBuffer & ) = delete;
		~UniqueBuffer();

		UniqueBuffer & operator=( UniqueBuffer && rvalue );
		UniqueBuffer & operator=( const UniqueBuffer & ) = delete;

		constexpr explicit operator bool() const;
		constexpr bool operator!() const;

		constexpr void * Get() const;
		constexpr Uint32 GetSize() const;
		constexpr Uint32 GetAlignment() const;

		// Template compatible functions with other APIs
		constexpr void* Data() const;
		constexpr Uint32 Size() const;

		void Reset();

		void Swap( UniqueBuffer & value );

		// ctremblay WARNING only use if you know what you are doing. In doubt, ask around.
		void * Release(); 

		void Reallocate( Uint32 size );

		// Shrink the size without actually reallocating
		// E.g., useful if buffer is short-lived and just want GetSize() to return a smaller value.
		void PatchSize( Uint32 smallerSize );

		const red::memory::Pool& GetPool() const;
	private:

		void * m_buffer;
		Uint32 m_size;
		Uint32 m_alignment;
		struct { Uint8 buffer[ 16 ]; } m_allocator;
	
		template< typename Proxy >
		friend UniqueBuffer MakeUniqueBuffer( Proxy & proxy, void * buffer, Uint32 size, Uint32 alignment  );

		template< typename Proxy >
		friend UniqueBuffer MakeEmptyUniqueBuffer( Proxy & proxy, Uint32 alignment );
	};

	template< typename Pool >
	UniqueBuffer CreateUniqueBuffer( Uint32 size, Uint32 alignment );

	template< typename Proxy >
	UniqueBuffer CreateUniqueBuffer( Proxy & proxy, Uint32 size, Uint32 alignment );

	template< typename Pool >
	UniqueBuffer MakeUniqueBuffer( void * buffer, Uint32 size, Uint32 alignment );

	template< typename Proxy >
	UniqueBuffer MakeUniqueBuffer( Proxy & proxy, void * buffer, Uint32 size, Uint32 alignment );

	template< typename Proxy >
	UniqueBuffer MakeEmptyUniqueBuffer( Proxy & proxy, Uint32 alignment );

	template< typename Pool >
	UniqueBuffer MakeEmptyUniqueBuffer( Uint32 alignment );
}

#include "uniqueBuffer.hpp"

#endif
