/**
 * Copyright (c) 2020 CD Projekt Red. All Rights Reserved.
 */

#include "build.h"
#include "frameAllocatorWithFallback.h"

namespace red
{
namespace memory
{
#ifndef RED_CONFIGURATION_FINAL
namespace
{
	class UpdateFlagGuard
	{
	public:
		enum Mode
		{
			Inclusive,
			Exclusive
		};

		UpdateFlagGuard( UpdateFlag& flag, Mode mode, u32 numberOfFrames )
			: m_flag( flag )
			, m_numberOfFrames( numberOfFrames )
			, m_mode( mode )
		{
			RED_WARNING_PUSH();
			RED_DISABLE_WARNING_MSC(4127); // Conditional expression is constant
			
			if ( c_checkDebugBanFrameAllocator && m_numberOfFrames == 1 )
			{
				if ( mode == Exclusive )
				{
					m_flag.AcquireExclusive();
				}
				else
				{
					m_flag.AcquireShared();
				}
			}

			RED_WARNING_POP();
		}

		~UpdateFlagGuard()
		{
			RED_WARNING_PUSH();
			RED_DISABLE_WARNING_MSC(4127); // Conditional expression is constant

			if ( c_checkDebugBanFrameAllocator && m_numberOfFrames == 1 )
			{
				if ( m_mode == Exclusive )
				{
					m_flag.ReleaseExclusive();
				}
				else
				{
					m_flag.ReleaseShared();
				}
			}

			RED_WARNING_POP();
		}

		UpdateFlagGuard( const UpdateFlag& ) = delete;
		UpdateFlagGuard( UpdateFlag&& ) = delete;

	private:
		UpdateFlag& m_flag;
		u32 m_numberOfFrames;
		Mode m_mode;
	};
}
#endif

	FrameAllocatorWithFallback::FrameAllocatorWithFallback()
#ifndef RED_CONFIGURATION_FINAL
		:  m_debugBanFrameAllocator( "Unsafe use of frame allocator detected." )
		, m_outOfBudgetUsedBytes( 0 )
#endif
	{}

	FrameAllocatorWithFallback::~FrameAllocatorWithFallback()
	{}

	void FrameAllocatorWithFallback::Initialize( const FrameAllocatorParameter& parameter )
	{
		m_frameAllocator.Initialize( parameter );
	}

	void FrameAllocatorWithFallback::Uninitialize()
	{
		m_frameAllocator.Uninitialize();
	}

	Block FrameAllocatorWithFallback::Allocate( u32 size )
	{
#ifndef RED_CONFIGURATION_FINAL
		UpdateFlagGuard guard{ m_debugBanFrameAllocator, UpdateFlagGuard::Inclusive, m_frameAllocator.Internal_GetNumberOfFrames() };
#endif

		Block block = m_frameAllocator.Allocate( size );
		if ( !block.address )
		{
			block = AcquireDefaultAllocator().Allocate( size );

#ifdef RED_MEMORY_ENABLE_METRICS
			if ( block.address )
			{
				atomic::ExchangeAdd64( &m_outOfBudgetUsedBytes, block.size );
			}
#endif
		}

		return block;
	}

	Block FrameAllocatorWithFallback::AllocateAligned( u32 size, u32 alignment )
	{
#ifndef RED_CONFIGURATION_FINAL
		UpdateFlagGuard guard{ m_debugBanFrameAllocator, UpdateFlagGuard::Inclusive, m_frameAllocator.Internal_GetNumberOfFrames() };
#endif

		Block block = m_frameAllocator.AllocateAligned( size, alignment );
		if ( !block.address )
		{
			block = AcquireDefaultAllocator().AllocateAligned( size, alignment );

#ifdef RED_MEMORY_ENABLE_METRICS
			if ( block.address )
			{
				atomic::ExchangeAdd64( &m_outOfBudgetUsedBytes, block.size );
			}
#endif
		}

		return block;
	}

	Block FrameAllocatorWithFallback::Reallocate( Block& block, u32 size )
	{
		if( size == 0 )
		{
			Free( block );
			return NullBlock();
		}

		if( block.address == 0 )
		{
			return Allocate( size );
		}

		if ( m_frameAllocator.OwnBlock( block.address ) )
		{
			return ReallocateFromFrameAllocator( block, size );
		}
		else
		{
			auto& defaultAllocator = AcquireDefaultAllocator();
			if ( defaultAllocator.OwnBlock( block.address ) )
			{
				return ReallocateFromDefaultAllocator( defaultAllocator, block, size );
			}
			else
			{
				RED_MEMORY_ASSERT( 0, "Block was not allocated by FrameAllocatorWithFallback" );
				return NullBlock();
			}
		}
	}

	Block FrameAllocatorWithFallback::ReallocateAligned( Block& block, u32 size, u32 alignment )
	{
		if( size == 0 )
		{
			Free( block );
			return NullBlock();
		}

		if( block.address == 0 )
		{
			return AllocateAligned( size, alignment );
		}

		if ( m_frameAllocator.OwnBlock( block.address ) )
		{
			return ReallocateFromFrameAllocator( block, size, alignment );
		}
		else
		{
			auto& defaultAllocator = AcquireDefaultAllocator();
			if ( defaultAllocator.OwnBlock( block.address ) )
			{
				return ReallocateFromDefaultAllocator( defaultAllocator, block, size, alignment );
			}
			else
			{
				RED_MEMORY_ASSERT( 0, "Block was not allocated by FrameAllocatorWithFallback" );
				return NullBlock();
			}
		}
	}

	void FrameAllocatorWithFallback::Free( Block& block )
	{
		if ( block.address )
		{
			if ( m_frameAllocator.OwnBlock( block.address ) )
			{
				m_frameAllocator.Free( block );
			}
			else
			{
#ifndef RED_CONFIGURATION_FINAL
				UpdateFlagGuard guard{ m_debugBanFrameAllocator, UpdateFlagGuard::Inclusive, m_frameAllocator.Internal_GetNumberOfFrames() };
#endif

				auto& defaultAllocator = AcquireDefaultAllocator();
				if ( defaultAllocator.OwnBlock( block.address ) )
				{
					defaultAllocator.Free( block );
#ifdef RED_MEMORY_ENABLE_METRICS
					atomic::ExchangeAdd64( &m_outOfBudgetUsedBytes, -static_cast< i64 >( block.size ) );
#endif
				}
				else
				{
					RED_MEMORY_ASSERT( 0, "Block was not allocated by FrameAllocatorWithFallback" );
				}
			}
		}
	}

	bool FrameAllocatorWithFallback::OwnBlock( u64 block ) const
	{
#ifndef RED_CONFIGURATION_FINAL
		UpdateFlagGuard guard{ m_debugBanFrameAllocator, UpdateFlagGuard::Inclusive, m_frameAllocator.Internal_GetNumberOfFrames() };
#endif
		return m_frameAllocator.OwnBlock( block ) || AcquireDefaultAllocator().OwnBlock( block );
	}

	u64 FrameAllocatorWithFallback::GetBlockSize( u64 address ) const
	{
		if ( m_frameAllocator.OwnBlock( address ) )
		{
			return m_frameAllocator.GetBlockSize( address );
		}
		else
		{
#ifndef RED_CONFIGURATION_FINAL
			UpdateFlagGuard guard{ m_debugBanFrameAllocator, UpdateFlagGuard::Inclusive, m_frameAllocator.Internal_GetNumberOfFrames() };
#endif
			auto& defaultAllocator = AcquireDefaultAllocator();
			if ( defaultAllocator.OwnBlock( address ) )
			{
				return defaultAllocator.GetBlockSize( address );
			}
			else
			{
				RED_MEMORY_ASSERT( 0, "Block was not allocated by FrameAllocatorWithFallback" );
				return 0;
			}
		}
	}

	void FrameAllocatorWithFallback::Reset()
	{
#ifndef RED_CONFIGURATION_FINAL
		UpdateFlagGuard guard{ m_debugBanFrameAllocator, UpdateFlagGuard::Exclusive, m_frameAllocator.Internal_GetNumberOfFrames() };
#endif

		m_frameAllocator.Reset();

#ifdef RED_MEMORY_ENABLE_METRICS
		if ( m_frameAllocator.Internal_GetNumberOfFrames() == 1 )
		{
			const Uint64 outOfBudgetUsedBytes = atomic::FetchValue64( &m_outOfBudgetUsedBytes );
			RED_MEMORY_ASSERT( outOfBudgetUsedBytes == 0, "Detected memory leak" );
		}
#endif
	}

	void FrameAllocatorWithFallback::BuildMetrics( FrameAllocatorWithFallbackMetrics& metrics )
	{
		m_frameAllocator.BuildMetrics( metrics.metrics );
#ifdef RED_MEMORY_ENABLE_METRICS
		metrics.outOfBudgetUsedBytes = atomic::FetchValue64( &m_outOfBudgetUsedBytes );
#else
		metrics.outOfBudgetUsedBytes = 0;
#endif
		metrics.metrics.metrics.consumedMemoryBytes += metrics.outOfBudgetUsedBytes;
		metrics.metrics.metrics.consumedSystemMemoryBytes += metrics.outOfBudgetUsedBytes;
	}

	void FrameAllocatorWithFallback::SerializeMetrics( Serializer& serializer )
	{
		FrameAllocatorWithFallbackMetrics metrics;
		BuildMetrics( metrics );

		SerializeAllocatorIdentifiers( this, serializer );

		serializer.Serialize( static_cast< u32 >( sizeof( metrics ) ) );
		serializer.Serialize( &metrics, sizeof( metrics ) );
	}

	u64 FrameAllocatorWithFallback::Debug_GetOutOfBudgetBytes() const
	{
#ifdef RED_MEMORY_ENABLE_METRICS
		return atomic::FetchValue64( &m_outOfBudgetUsedBytes );
#else
		return 0;
#endif
	}

	Block FrameAllocatorWithFallback::ReallocateFromFrameAllocator( Block& block, u32 size )
	{
#ifndef RED_CONFIGURATION_FINAL
		UpdateFlagGuard guard{ m_debugBanFrameAllocator, UpdateFlagGuard::Inclusive, m_frameAllocator.Internal_GetNumberOfFrames() };
#endif
		Block newBlock = m_frameAllocator.Reallocate( block, size );
		if ( !newBlock.address )
		{
			auto& defaultAllocator = AcquireDefaultAllocator();
			newBlock = defaultAllocator.Allocate( size );
			if ( newBlock.address )
			{
				MemcpyBlock( newBlock, block );
				m_frameAllocator.Free( block );

#ifdef RED_MEMORY_ENABLE_METRICS
				atomic::ExchangeAdd64( &m_outOfBudgetUsedBytes, newBlock.size );
#endif
			}
		}

		return newBlock;
	}

	Block FrameAllocatorWithFallback::ReallocateFromDefaultAllocator( DefaultAllocator& defaultAllocator, Block& block, u32 size )
	{
#ifndef RED_CONFIGURATION_FINAL
		UpdateFlagGuard guard{ m_debugBanFrameAllocator, UpdateFlagGuard::Inclusive, m_frameAllocator.Internal_GetNumberOfFrames() };
#endif

		Block newBlock = m_frameAllocator.Allocate( size );
		if ( newBlock.address )
		{
			MemcpyBlock( newBlock, block );
			defaultAllocator.Free( block );

#ifdef RED_MEMORY_ENABLE_METRICS
			atomic::ExchangeAdd64( &m_outOfBudgetUsedBytes, -static_cast< i64 >( block.size ) );
#endif
		}
		else
		{
			newBlock = defaultAllocator.Reallocate( block, size );
			if ( newBlock.address )
			{
#ifdef RED_MEMORY_ENABLE_METRICS
				const i64 newSize = -static_cast< i64 >( block.size ) + newBlock.size;
				atomic::ExchangeAdd64( &m_outOfBudgetUsedBytes, newSize );
#endif
			}
		}

		return newBlock;
	}

	Block FrameAllocatorWithFallback::ReallocateFromFrameAllocator( Block& block, u32 size, u32 alignment )
	{
#ifndef RED_CONFIGURATION_FINAL
		UpdateFlagGuard guard{ m_debugBanFrameAllocator, UpdateFlagGuard::Inclusive, m_frameAllocator.Internal_GetNumberOfFrames() };
#endif

		Block newBlock = m_frameAllocator.ReallocateAligned( block, size, alignment );
		if ( !newBlock.address )
		{
			auto& defaultAllocator = AcquireDefaultAllocator();
			newBlock = defaultAllocator.AllocateAligned( size, alignment );
			if ( newBlock.address )
			{
				MemcpyBlock( newBlock, block );
				m_frameAllocator.Free( block );

#ifdef RED_MEMORY_ENABLE_METRICS
				atomic::ExchangeAdd64( &m_outOfBudgetUsedBytes, newBlock.size );
#endif
			}
		}

		return newBlock;
	}

	Block FrameAllocatorWithFallback::ReallocateFromDefaultAllocator( DefaultAllocator& defaultAllocator, Block& block, u32 size, u32 alignment )
	{
#ifndef RED_CONFIGURATION_FINAL
		UpdateFlagGuard guard{ m_debugBanFrameAllocator, UpdateFlagGuard::Inclusive, m_frameAllocator.Internal_GetNumberOfFrames() };
#endif

		Block newBlock = m_frameAllocator.AllocateAligned( size, alignment );
		if ( newBlock.address )
		{
			MemcpyBlock( newBlock, block );
			defaultAllocator.Free( block );

#ifdef RED_MEMORY_ENABLE_METRICS
			atomic::ExchangeAdd64( &m_outOfBudgetUsedBytes, -static_cast< i64 >( block.size ) );
#endif
		}
		else
		{
			newBlock = defaultAllocator.ReallocateAligned( block, size, alignment );
			if ( newBlock.address )
			{
#ifdef RED_MEMORY_ENABLE_METRICS
				const i64 newSize = -static_cast< i64 >( block.size ) + newBlock.size;
				atomic::ExchangeAdd64( &m_outOfBudgetUsedBytes, newSize );
#endif
			}
		}

		return newBlock;
	}
}
}