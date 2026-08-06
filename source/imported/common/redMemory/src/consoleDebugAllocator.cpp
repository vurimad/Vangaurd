/**
 * Copyright (c) 2020 CD Projekt Red. All Rights Reserved.
 */

#include "build.h"
#include "consoleDebugAllocator.h"

namespace red
{
namespace memory
{
	ConsoleDebugAllocator::ConsoleDebugAllocator()
	{}

	ConsoleDebugAllocator::~ConsoleDebugAllocator()
	{}

	void ConsoleDebugAllocator::Initialize( DefaultAllocator* defaultAllocator, SystemAllocator* systemAllocator )
	{
		RED_FATAL_ASSERT( defaultAllocator );
		RED_FATAL_ASSERT( systemAllocator );

		m_defaultAllocator = defaultAllocator;
		
		BigSizeAllocatorParameter param =
		{
			systemAllocator,
			RED_GIGA_BYTE( 4 ),
			red::memory::Flags_CPU_Read_Write
		};

		m_bigSizeAllocator.Initialize( param );
	}

	void ConsoleDebugAllocator::Uninitialize()
	{
		m_bigSizeAllocator.Uninitialize();
	}

	Block ConsoleDebugAllocator::Allocate( u32 size )
	{
		if( size <= DefaultAllocator::GetMaxAllocationSize() )
		{
			return m_defaultAllocator->Allocate( size );
		}
		else
		{
			return m_bigSizeAllocator.Allocate( size );
		}
	}
	
	Block ConsoleDebugAllocator::AllocateAligned( u32 size, u32 alignment )
	{
		if( size <= DefaultAllocator::GetMaxAllocationSize() )
		{
			return m_defaultAllocator->AllocateAligned( size, alignment );
		}
		else
		{
			return m_bigSizeAllocator.AllocateAligned( size, alignment );
		}
	}
	
	Block ConsoleDebugAllocator::Reallocate( Block& block, u32 size )
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

		Block newBlock = NullBlock();

		if( m_defaultAllocator->OwnBlock( block.address ) )
		{
			if( size <= DefaultAllocator::GetMaxAllocationSize() )
			{
				newBlock = m_defaultAllocator->Reallocate( block, size );
			}
			else
			{
				newBlock = m_bigSizeAllocator.Allocate( size );
				if( newBlock.address )
				{
					MemcpyBlock( newBlock.address, block.address, GetBlockSize( block.address ) );
					m_defaultAllocator->Free( block );
				}
			}
		}
		else
		{
			if( size <= DefaultAllocator::GetMaxAllocationSize() )
			{
				newBlock = m_defaultAllocator->Allocate( size );
				if( newBlock.address )
				{
					MemcpyBlock( newBlock.address, block.address, GetBlockSize( block.address ) );
					m_bigSizeAllocator.Free( block );
				}
			}
			else
			{
				newBlock = m_bigSizeAllocator.Reallocate( block, size );
			}

		}

		return newBlock;
	}
	
	Block ConsoleDebugAllocator::ReallocateAligned( Block& block, u32 size, u32 alignment )
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

		Block newBlock = NullBlock();

		if( m_defaultAllocator->OwnBlock( block.address ) )
		{
			if( size <= DefaultAllocator::GetMaxAllocationSize() )
			{
				newBlock = m_defaultAllocator->ReallocateAligned( block, size, alignment );
			}
			else
			{
				newBlock = m_bigSizeAllocator.AllocateAligned( size, alignment );
				if( newBlock.address )
				{
					MemcpyBlock( newBlock.address, block.address, GetBlockSize( block.address ) );
					m_defaultAllocator->Free( block );
				}
			}
		}
		else
		{
			if( size <= DefaultAllocator::GetMaxAllocationSize() )
			{
				newBlock = m_defaultAllocator->AllocateAligned( size, alignment );
				if( newBlock.address )
				{
					MemcpyBlock( newBlock.address, block.address, GetBlockSize( block.address ) );
					m_bigSizeAllocator.Free( block );
				}
			}
			else
			{
				newBlock = m_bigSizeAllocator.ReallocateAligned( block, size, alignment );
			}
		}

		return newBlock;
	}
	
	void ConsoleDebugAllocator::Free( Block& block )
	{
		if( m_defaultAllocator->OwnBlock( block.address ) )
		{
			m_defaultAllocator->Free( block );
		}
		else
		{
			m_bigSizeAllocator.Free( block );
		}
	}

	bool ConsoleDebugAllocator::OwnBlock( u64 block ) const
	{
		return m_defaultAllocator->OwnBlock( block ) || m_bigSizeAllocator.OwnBlock( block );
	}

	u64 ConsoleDebugAllocator::GetBlockSize( u64 address ) const
	{
		if( m_defaultAllocator->OwnBlock( address ) )
		{
			return m_defaultAllocator->GetBlockSize( address );
		}
		else
		{
			return m_bigSizeAllocator.GetBlockSize( address );
		}
	}
}
}
