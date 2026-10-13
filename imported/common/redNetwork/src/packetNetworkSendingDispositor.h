/**
* Copyright (c) 2015 CD Projekt Red. All Rights Reserved.
*/

#pragma once

#include "../../redMemory/include/redMemoryPublic.h"
#include "../../redContainers/include/dynArray.h"

namespace red
{
	namespace Network
	{
		namespace Utils
		{
			class DataDisposer
			{
			private:
				struct DataBlock
				{
					DataBlock() {}
					DataBlock( Uint8* data, Uint32 size )
						: m_data( data )
						, m_size( size )
					{

					}

					const Uint8* m_data;
					Uint32 m_size;
				};

			public:
				DataDisposer( Uint32 reserveBlocksCount = 1 )
				{
					ResetState();
					m_dataBlocks.Reserve( reserveBlocksCount );
				}

				void AddDataBlock( const void* data, Uint32 size )
				{
					DataBlock dataBlock( (Uint8*)data, size );
					m_dataBlocks.PushBack( dataBlock );
				}

				void GetNextData( const void*& data, Uint32& dataSize ) const
				{
					if( IsEndOfData() )
						return;

					data = m_dataBlocks[m_currentBlockToSend].m_data + m_currentBlockDataOffset;
					dataSize = m_dataBlocks[m_currentBlockToSend].m_size - m_currentBlockDataOffset;
				}

				void UpdateState( Uint32 bytesSent )
				{
					if( IsEndOfData() )
						return;

					m_currentBlockDataOffset += bytesSent;

					RED_ASSERT( m_currentBlockDataOffset <= m_dataBlocks[m_currentBlockToSend].m_size );
					if( m_currentBlockDataOffset == m_dataBlocks[m_currentBlockToSend].m_size )
					{
						m_currentBlockDataOffset = 0;
						m_currentBlockToSend++;
					}
				}

				void ResetState()
				{
					m_currentBlockDataOffset = 0;
					m_currentBlockToSend = 0;
				}

				Bool IsEndOfData() const
				{
					return m_currentBlockToSend >= m_dataBlocks.Size();
				}

			private:
				red::DynArray< DataBlock > m_dataBlocks{ red::PoolEngine() };
				Uint32 m_currentBlockToSend;
				Uint32 m_currentBlockDataOffset;

			};
		}
	}	// Network
}	// Red
