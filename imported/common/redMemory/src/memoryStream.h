/**
 * Copyright (c) 2017 CD Projekt Red. All Rights Reserved.
 */

#ifndef _RED_MEMORY_MEMORY_STREAM_H_
#define _RED_MEMORY_MEMORY_STREAM_H_

#include "../include/stream.h"
#include "../include/systemAllocator.h"

namespace red
{
namespace memory
{
	class RED_MEMORY_API MemoryStream final : public Stream
	{
	public:
		MemoryStream( u32 memoryFlags = Flags_CPU_Read_Write );

		void Initialize( SystemAllocator * systemAllocator, const u32 bufferSize = 0, const u32 initialAllocation = 0 );
		void Uninitialize();

		void ResetMarkers();

	private:
		SystemBlock AllocateBlock( u32 size );

		virtual u32 OnDataAvailableToWrite() const override;
		virtual u32 OnDataAvailableToRead() const override;
		virtual Bool OnWriteBuffer( const void * buffer, u32 size ) override;
		virtual Bool OnReadBuffer( void * buffer, u32 size, u32 & readSize ) override;
		virtual void* OnGetReadData() const override;
		virtual void OnSeekReadIndex( u32 size ) override;

		u64 m_writeMarker;
		u64 m_readMarker;

		u64 m_nextVirtualAddress;
		SystemAllocator * m_allocator;
		VirtualRange m_virtualRange;
		u32 m_memoryFlags;
	};
}
}

#endif