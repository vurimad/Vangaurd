/**
 * Copyright (c) 2020 CD Projekt Red. All Rights Reserved.
 */

#ifndef _RED_MEMORY_ICU_ALLOCATOR_H_
#define _RED_MEMORY_ICU_ALLOCATOR_H_

namespace red
{
namespace memory
{
    class SystemAllocator;
    class DefaultAllocator;

    struct ICUAllocatorMetrics
    {
        AllocatorMetrics metrics;
		TLSFAllocatorMetrics tlsfAllocatorMetrics;
    };

    struct ICUAllocatorParameter
    {
        SystemAllocator * systemAllocator;
        DefaultAllocator * defaultAllocator;
    };

    class RED_MEMORY_API ICUAllocator
    {
    public:

        RED_MEMORY_DECLARE_ALLOCATOR( ICUAllocator, ICUAllocatorMetrics, 8 );

        ICUAllocator();
        ~ICUAllocator();

        void Initialize( const ICUAllocatorParameter & parameter );
        void Uninitialize();

		Block Allocate( u32 size );
		Block AllocateAligned( u32 size, u32 alignment );
		Block Reallocate( Block & block, u32 size );
		Block ReallocateAligned( Block & block, u32 size, u32 alignment );
		void Free( Block & block );

		bool OwnBlock( u64 block ) const;
		u64 GetBlockSize( u64 address ) const;

		void RegisterCurrentThread( const AnsiChar* threadName );

		void BuildMetrics( ICUAllocatorMetrics & metrics );
		void SerializeMetrics( Serializer & serializer );

    private:

		ICUAllocator( const ICUAllocator & );
		ICUAllocator & operator=( const ICUAllocator& );

		bool IsInitialized() const;
		
		Block ReallocateFromTLSFAllocator( Block & block, u32 size );
		Block ReallocateFromTLSFAllocator( Block & block, u32 size, u32 alignment );

		RED_ALIGN( 64 ) LockingDynamicTLSFAllocator m_tlsfAllocator;

        SystemAllocator* m_systemAllocator;
		DefaultAllocator * m_defaultAllocator;
    };

    RED_MEMORY_API ICUAllocator & AcquireICUAllocator();
}
}

#endif
