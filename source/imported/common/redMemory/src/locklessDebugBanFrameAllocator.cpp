/**
 * Copyright (c) 2019 CD Projekt Red. All Rights Reserved.
 */

#include "build.h"
#include "locklessDebugBanFrameAllocator.h"

#ifndef RED_CONFIGURATION_FINAL

namespace red
{
namespace memory
{
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

LocklessDebugBanFrameAllocator::LocklessDebugBanFrameAllocator()
		: m_debugBanFrameAllocator( "Unsafe use of frame allocator detected." )
	{}
	
	LocklessDebugBanFrameAllocator::~LocklessDebugBanFrameAllocator()
	{}

	void LocklessDebugBanFrameAllocator::Initialize( const FrameAllocatorParameter & parameter )
	{
		m_allocator.Initialize( parameter );
	}

	void LocklessDebugBanFrameAllocator::Uninitialize()
	{
		m_allocator.Uninitialize();
	}

	Block LocklessDebugBanFrameAllocator::Allocate( u32 size )
	{
		UpdateFlagGuard guard{ m_debugBanFrameAllocator, UpdateFlagGuard::Inclusive, m_allocator.Internal_GetNumberOfFrames() };
		return m_allocator.Allocate( size );
	}
	
	Block LocklessDebugBanFrameAllocator::AllocateAligned( u32 size, u32 alignment )
	{
		UpdateFlagGuard guard{ m_debugBanFrameAllocator, UpdateFlagGuard::Inclusive, m_allocator.Internal_GetNumberOfFrames() };
		return m_allocator.AllocateAligned( size, alignment );
	}
	
	Block LocklessDebugBanFrameAllocator::Reallocate( Block & block, u32 size )
	{
		return ReallocateAligned( block, size, DefaultAlignmentType::value );
	}
	
	Block LocklessDebugBanFrameAllocator::ReallocateAligned( Block & block, u32 size, u32 alignment )
	{
		UpdateFlagGuard guard{ m_debugBanFrameAllocator, UpdateFlagGuard::Inclusive, m_allocator.Internal_GetNumberOfFrames() };
		return m_allocator.ReallocateAligned( block, size, alignment );
	}
	
	void LocklessDebugBanFrameAllocator::Free( Block & block )
	{
		UpdateFlagGuard guard{ m_debugBanFrameAllocator, UpdateFlagGuard::Inclusive, m_allocator.Internal_GetNumberOfFrames() };
		m_allocator.Free( block );
	}

	bool LocklessDebugBanFrameAllocator::OwnBlock( u64 block ) const
	{
		UpdateFlagGuard guard{ m_debugBanFrameAllocator, UpdateFlagGuard::Inclusive, m_allocator.Internal_GetNumberOfFrames() };
		return m_allocator.OwnBlock( block );
	}
	
	u64 LocklessDebugBanFrameAllocator::GetBlockSize( u64 address ) const
	{
		UpdateFlagGuard guard{ m_debugBanFrameAllocator, UpdateFlagGuard::Inclusive, m_allocator.Internal_GetNumberOfFrames() };
		return m_allocator.GetBlockSize( address );
	}

	void LocklessDebugBanFrameAllocator::Reset()
	{
		UpdateFlagGuard guard{ m_debugBanFrameAllocator, UpdateFlagGuard::Exclusive, m_allocator.Internal_GetNumberOfFrames() };
		m_allocator.Reset();
	}

	void LocklessDebugBanFrameAllocator::BuildMetrics( FrameAllocatorMetrics& metrics )
	{
		m_allocator.BuildMetrics( metrics );
	}

	void LocklessDebugBanFrameAllocator::SerializeMetrics( Serializer& serializer )
	{
		FrameAllocatorMetrics metrics;
		m_allocator.BuildMetrics( metrics );

		SerializeAllocatorIdentifiers( this, serializer );

		serializer.Serialize( static_cast< u32 >( sizeof( metrics ) ) );
		serializer.Serialize( &metrics, sizeof( metrics ) );
	}

}
}

#else

RED_NO_EMPTY_FILE();

#endif
