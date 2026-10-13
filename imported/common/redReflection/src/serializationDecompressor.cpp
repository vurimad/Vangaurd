/*
* Copyright (c) 2018 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"
#include "serializationDecompressor.h"

#include "../../redCompression/include/compression.h"
#include "../../redSystem/include/timer.h"
#include "../../redIO/include/redIOAsyncIO.h"
#include "../../redContainers/include/blob.h"

#if defined(RED_PLATFORM_DURANGO) && defined(USE_PROFILER)
#define USE_PIX
#include <pix.h>
#endif

namespace serialization
{
	static thread_local Int32 t_threadIndex = -1;
	static red::Atomic< Int32 > s_threadIndexPool;
	
	RED_ALIGNED_STRUCT( CacheAlignedStatsBlock, 64 )
	{
		Uint64 packedBytesPerUsec{ 0 };
	};

	static CacheAlignedStatsBlock s_perThreadStats[ 64 ];

	Bool Decompressor::DecompressData( const char* debugLogicalFileName, const red::BlobView& compressedData, red::BlobSpan outputBuffer )
	{
		return DecompressData( debugLogicalFileName, compressedData, outputBuffer.Data(), outputBuffer.Size() );
	}

	namespace helper
	{
		struct ScopedStatsUpdater : red::NonCopyable
		{
			explicit ScopedStatsUpdater( Uint32 outputBufferSize )
#ifdef USE_PROFILER
				: m_startTicks( red::ProfileTimer::GetTicks() )
				, m_outputBufferSize( outputBufferSize )
				, m_isFailed( false )
			{
				if ( t_threadIndex == -1 )
				{
					t_threadIndex = s_threadIndexPool.Increment() - 1;
					RED_FATAL_ASSERT( t_threadIndex < RED_ARRAY_COUNT_U32( s_perThreadStats ), "Too many threads decompressing. Are you respawning new threads?" );
				}
				
				m_statsBlock = &s_perThreadStats[ t_threadIndex ];
			}
#else
			{
			}
#endif

			void SetFailed()
			{
#ifdef USE_PROFILER
				m_isFailed = true;
#endif
			}

			~ScopedStatsUpdater()
			{
#ifdef USE_PROFILER
				const Uint64 endTicks = red::ProfileTimer::GetTicks();
				const Uint64 usec = red::ProfileTimer::GetDeltaUsec( endTicks, m_startTicks );
				const Uint32 numBytesOutput = m_outputBufferSize;
									
				const Uint64 packedBytesPerUsec = m_statsBlock->packedBytesPerUsec;
				Uint32 totalBytesOutput = packedBytesPerUsec >> 32;
				Uint32 totalUsec = static_cast<Uint32>( packedBytesPerUsec);

				// Could overflow usec approx every half hour, assuming non-stop decompressing without stats reset, which could happen if the debug visualizer is off
				Bool overflow = ( totalBytesOutput + numBytesOutput < totalBytesOutput ) || ( totalUsec + static_cast< Uint32 >( usec ) < totalUsec );
				if ( overflow )
				{
					totalBytesOutput = 0;
					totalUsec = 0;
				}

				totalBytesOutput += numBytesOutput;
				totalUsec += static_cast< Uint32 >( usec );

				if ( m_isFailed )
				{
					return;
				}

				m_statsBlock->packedBytesPerUsec = ( static_cast<Uint64>( totalBytesOutput ) << 32 ) | totalUsec;
#endif
			}

#ifdef USE_PROFILER
			CacheAlignedStatsBlock* m_statsBlock;
			Uint64 m_startTicks;
			Uint32 m_outputBufferSize;
			Bool m_isFailed;
#endif
		};

	}

	Bool Decompressor::DecompressData( const char* debugLogicalFileName, const red::BlobView& compressedData, void* outputBuffer, Uint32 outputBufferSize )
	{
		PC_SCOPE_FUNC();
		io::GAsyncIO.ProfileDecompressStart( debugLogicalFileName );

		helper::ScopedStatsUpdater statsUpdater{ outputBufferSize };

		const auto compressionType = compression::GetCompressionTypeFromData( compressedData.Data(), compressedData.Size() );
#if defined(RED_PLATFORM_DURANGO) && defined(USE_PROFILER)
		PIXScopedEvent( PIX_COLOR(255,128,128), "Decompressing %s - Type: %s InSize: %u OutSize: %u", debugLogicalFileName, compression::compressionTypeNames[(Uint32)compressionType], compressedData.Size(), outputBufferSize );
#endif
		if ( !compression::DecompressData( compressionType, compressedData.Data(), compressedData.Size(),
			compression::GetInplaceCompressionAllocator( outputBuffer, outputBufferSize ) ) )
		{
			io::GAsyncIO.ProfileDecompressEnd( debugLogicalFileName );

			RED_LOG_ERROR( "Failed  to decompress buffer!" );
			statsUpdater.SetFailed();
			return false;
		}

		io::GAsyncIO.ProfileDecompressEnd( debugLogicalFileName );
		return true;
	}

	void Decompressor::FlushStats( StatsAccumulator& outStats )
	{
#ifdef USE_PROFILER
		Uint64 totalBytesOutput = 0;
		Uint64 totalUsec = 0;
		const Uint32 numSubtotals = s_threadIndexPool.GetValue();
		for ( Uint32 i = 0; i < numSubtotals; ++i )
		{
			const Uint64 packedBytesPerUsec = atomic::Exchange64( atomic::alias_cast64< Uint64* >( &s_perThreadStats[ i ].packedBytesPerUsec ), 0 );
			const Uint32 bytesOutput = packedBytesPerUsec >> 32;
			const Uint32 usec = static_cast<Uint32>( packedBytesPerUsec );

			// Would need about 4 billion decompression threads to overflow in this loop
			totalBytesOutput += bytesOutput;
			totalUsec += usec;
		}

		// Really shouldn't overflow either
		Bool overflow = ( outStats.m_totalBytesOutput + totalBytesOutput < outStats.m_totalBytesOutput ) || ( outStats.m_totalUsec + totalUsec < outStats.m_totalUsec );
		if ( !overflow )
		{
			outStats.m_totalBytesOutput += totalBytesOutput;
			outStats.m_totalUsec += totalUsec;
		}
		else
		{
			outStats.m_totalBytesOutput = totalBytesOutput;
			outStats.m_totalUsec = totalUsec;
		}
#endif
	}
}