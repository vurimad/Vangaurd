/**
 * Copyright (c) 2020 CD Projekt Red. All Rights Reserved.
 */

#ifndef _RED_MEMORY_WWISE_ALLOCATOR_H_
#define _RED_MEMORY_WWISE_ALLOCATOR_H_

namespace red
{
namespace memory
{
    class SystemAllocator;
    class DefaultAllocator;

    struct WwiseAllocatorMetrics
    {
        AllocatorMetrics metrics;
		TLSFAllocatorMetrics tlsfAllocatorMetrics;
		BigSizeAllocatorMetrics bigSizeAllocatorMetrics;
    };

    struct WwiseAllocatorParameter
    {
        SystemAllocator * systemAllocator;
        DefaultAllocator * defaultAllocator;
    };

    class RED_MEMORY_API WwiseAllocator
    {
    public:

        RED_MEMORY_DECLARE_ALLOCATOR( WwiseAllocator, WwiseAllocatorMetrics, 8 );

        WwiseAllocator();
        ~WwiseAllocator();

        void Initialize( const WwiseAllocatorParameter & parameter );
        void Uninitialize();

		Block Allocate( u32 size );
		Block AllocateAligned( u32 size, u32 alignment );
		Block Reallocate( Block & block, u32 size );
		Block ReallocateAligned( Block & block, u32 size, u32 alignment );
		void Free( Block & block );

		bool OwnBlock( u64 block ) const;
		u64 GetBlockSize( u64 address ) const;

		void RegisterCurrentThread( const AnsiChar* threadName );

		void BuildMetrics( WwiseAllocatorMetrics & metrics );
		void SerializeMetrics( Serializer & serializer );

    private:

		WwiseAllocator( const WwiseAllocator & );
		WwiseAllocator & operator=( const WwiseAllocator& );

		bool IsInitialized() const;
		
		Block ReallocateFromTLSFAllocator( Block & block, u32 size );
		Block ReallocateFromTLSFAllocator( Block & block, u32 size, u32 alignment );

		RED_ALIGN( 64 ) LockingDynamicTLSFAllocator m_tlsfAllocator;

        SystemAllocator* m_systemAllocator;
		DefaultAllocator* m_defaultAllocator;
		BigSizeAllocator m_bigSizeAllocator;
    };

    RED_MEMORY_API WwiseAllocator & AcquireWwiseAllocator();
}
}

#endif
