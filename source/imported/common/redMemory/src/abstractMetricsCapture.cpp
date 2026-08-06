/**
 * Copyright (c) 2017 CD Projekt Red. All Rights Reserved.
 */

#include "build.h"
#include "abstractMetricsCapture.h"
#include "callstack.h"
#include "hookHandler.h"
#include "hook.h"
#include "poolRegistry.h"
#include "../include/stream.h"
#include "../include/streamUtils.h"

namespace red
{
namespace memory
{
namespace
{
	const char c_modulePostfix[] = "_MOD";	// Mark the end of modules
}

	void PreWriteAllocationsCallback( HookPreParameter & param, void* userData )
	{
		AbstractMetricsCapture * serializer = static_cast< AbstractMetricsCapture* >( userData );
		const Block & block = *param.block;
		if( block.address )
		{
			// Free function called, or Reallocate.
			Callstack callstack;
			serializer->WriteFree( param.proxy.poolHandle, block, callstack );
		}
	}

	void PostWriteAllocationsCallback( HookPostParameter & param, void* userData )
	{
		AbstractMetricsCapture * serializer = static_cast< AbstractMetricsCapture* >( userData );

		const Block & output = *param.outputBlock;
		if( output.address )
		{
			// Allocate or Reallocate with size > 0.
			Callstack callstack;
			serializer->WriteAllocate( param.proxy.poolHandle, output, callstack );
		}
	}

	enum AllocationType : u8
	{
		AllocationType_Allocate = 0,
		AllocationType_Free,
		AllocationType_FrameMarker
	};

	struct AllocationInfo
	{
		u64 time;
		u64 address;
		u32 size;
		u32 callstackHash;
		u32 poolHandle;
		u8 type;
		u8 callstackCached;
	};

	static bool SerializeAllocationInfo( Serializer& serializer, const AllocationInfo& info )
	{
		return serializer.Serialize( info.time ) && serializer.Serialize( info.address )
			&& serializer.Serialize( info.size ) && serializer.Serialize( info.callstackHash )
			&& serializer.Serialize( info.poolHandle ) && serializer.Serialize( info.type )
			&& serializer.Serialize( info.callstackCached );
	}

	AbstractMetricsCapture::CallstackCache::CallstackCache()
		: m_writeMarker( nullptr )
	{
		Clear();
	}

	AbstractMetricsCapture::CallstackCache::~CallstackCache()
	{}

	bool AbstractMetricsCapture::CallstackCache::IsCached( u64 hash )
	{
		u64 * searchMarker = m_writeMarker;
		while( searchMarker-- > m_hashes )
		{
			if( *searchMarker == hash )
			{
				return true;
			}
		}

		searchMarker = m_hashes + c_callstackCacheHashCount;
		while( searchMarker-- > m_writeMarker )
		{
			if( *searchMarker == hash )
			{
				return true;
			}
		}

		return false;
	}

	void AbstractMetricsCapture::CallstackCache::Add( u64 hash )
	{
		*m_writeMarker++ = hash;
		if( m_writeMarker >= m_hashes + c_callstackCacheHashCount )
		{
			m_writeMarker = m_hashes;
		}
	}

	void AbstractMetricsCapture::CallstackCache::Clear()
	{
		std::memset( m_hashes, 0, sizeof( m_hashes ) );
		m_writeMarker = m_hashes;
	}

	AbstractMetricsCapture::AbstractMetricsCapture()
		:	m_oom( false ),
			m_hookHandler( nullptr ),
			m_hookHandle( 0 ),
			m_poolRegistry( nullptr )
	{}

	void AbstractMetricsCapture::Initialize( HookHandler * hookHandler, PoolRegistry * poolRegistry )
	{
		m_hookHandler = hookHandler;
		m_poolRegistry = poolRegistry;
	}

	void AbstractMetricsCapture::Start( const u32 bufferSize, OutOfProfilerMemoryCallback outOfMetricsCaptureMemoryCallback )
	{
		m_oom = false;
		m_stream = CreateUniquePtr< MemoryStream >();
		(static_cast<MemoryStream&>(*m_stream)).Initialize( &red::memory::AcquireSystemAllocator(), bufferSize );
		m_outOfMetricsCaptureMemoryCallback = std::move( outOfMetricsCaptureMemoryCallback );

		m_serializer.Initialize( m_stream.Get() );

		m_callstackAllocateCache.Clear();
		m_callstackFreeCache.Clear();

		m_timer.Reset();

		WriteHeader();
		WritePools();
		WriteCurrentFrameTick();

		const HookCreationParameter param = 
		{
			PreWriteAllocationsCallback,
			PostWriteAllocationsCallback,
			this,
			HookType::HookType_Memory_Profiler
		};

		m_hookHandle = m_hookHandler->Create( param );
	}

	void AbstractMetricsCapture::Start( const char * filename )
	{
		m_oom = false;
		m_stream = OpenStream( filename );
		m_serializer.Initialize( m_stream.Get() );

		m_callstackAllocateCache.Clear();
		m_callstackFreeCache.Clear();

		m_timer.Reset();

		WriteHeader();
		WritePools();
		WriteCurrentFrameTick();

		const HookCreationParameter param = 
		{
			PreWriteAllocationsCallback,
			PostWriteAllocationsCallback,
			this,
			HookType::HookType_Memory_Profiler
		};

		m_hookHandle = m_hookHandler->Create( param );
	}
	
	void AbstractMetricsCapture::Stop()
	{
		m_hookHandler->Remove( m_hookHandle );
		WriteFooter();
	}

	void AbstractMetricsCapture::Reset()
	{
		m_serializer.Uninitialize();
		m_stream.Reset();
		m_outOfMetricsCaptureMemoryCallback = []{};
		m_oom = false;
	}

	red::memory::Stream& AbstractMetricsCapture::GetStream()
	{
		return *m_stream.Get();
	}

	void AbstractMetricsCapture::WriteHeader()
	{
		WritePlatfromIdentifier( m_serializer );
		WriteLoadedModules( m_serializer );

		m_serializer.Serialize( c_modulePostfix, sizeof( c_modulePostfix ) - 1 );

		Uint64 frequency = 0;
		m_timer.GetFrequency( frequency );
		m_serializer.Serialize( frequency );
	}

	void AbstractMetricsCapture::WritePools()
	{
		m_poolRegistry->WritePools( m_serializer );
	}

	void AbstractMetricsCapture::WriteFooter()
	{}

	void AbstractMetricsCapture::WriteAllocate( PoolHandle poolHandle, const Block & block, const Callstack & callstack )
	{
		const u32 callstackHash = callstack.GetHash();
		const bool cachedCallstack = m_callstackAllocateCache.IsCached( callstackHash );

		ScopedLock< Mutex > scopedLock( m_monitor );

		if ( m_oom )
		{
			return;
		}

		Bool result = false;
		const u32 dataSize = sizeof( AllocationInfo ) + callstack.GetSerializationSize();

		if ( dataSize < m_serializer.DataAvailableToWrite() )
		{
			const AllocationInfo info = 
			{
				m_timer.GetTicks(),
				block.address,
				static_cast< u32 >( block.size ),
				callstackHash,
				poolHandle,
				AllocationType_Allocate,
				cachedCallstack
			};


			result = SerializeAllocationInfo( m_serializer, info );
			RED_FATAL_ASSERT( result, "Failed to serialize allocation info" );

			if( result && !cachedCallstack )
			{
				// explicitly testing the result of the assignment to suppress C4706 warning
				if ( ( result = callstack.SerializeCallstack( m_serializer ) ) == true )
				{
					m_callstackAllocateCache.Add( callstackHash );
				}
				else
				{
					RED_FATAL_ASSERT( result, "Failed to serialize allocation callstack" );
				}
			}
		}

		if ( !result )
		{
			HandleSerializationFailure();
		}
	}

	void AbstractMetricsCapture::WriteFree( PoolHandle poolHandle, const Block & block, const Callstack & callstack )
	{
		const u32 callstackHash = callstack.GetHash();
		const bool cachedCallstack = m_callstackFreeCache.IsCached( callstackHash );

		ScopedLock< Mutex > scopedLock( m_monitor );

		if ( m_oom )
		{
			return;
		}

		Bool result = false;
		const u32 dataSize = sizeof( AllocationInfo ) + callstack.GetSerializationSize();

		if ( dataSize < m_serializer.DataAvailableToWrite() )
		{
			const AllocationInfo info = 
			{
				m_timer.GetTicks(),
				block.address,
				static_cast< u32 >( block.size ),
				callstackHash,
				poolHandle,
				AllocationType_Free,
				cachedCallstack
			};

			result = SerializeAllocationInfo( m_serializer, info );
			RED_FATAL_ASSERT( result, "Failed to serialize allocation info" );

			if( result && !cachedCallstack )
			{
				// explicitly testing the result of the assignment to suppress C4706 warning
				if ( ( result = callstack.SerializeCallstack( m_serializer ) ) == true )
				{
					m_callstackFreeCache.Add( callstackHash );
				}
				else
				{
					RED_FATAL_ASSERT( result, "Failed to serialize allocation callstack" );
				}
			}
		}

		if ( !result )
		{
			HandleSerializationFailure();
		}
	}

	void AbstractMetricsCapture::WriteCurrentFrameTick()
	{
		ScopedLock< Mutex > scopedLock( m_monitor );

		if ( m_oom )
		{
			return;
		}

		const u32 dataSize = sizeof( u64 ) * 2;
		if ( dataSize < m_serializer.DataAvailableToWrite() )
		{
			if ( ( m_serializer.Serialize( m_timer.GetTicks() ) && m_serializer.Serialize( static_cast< u64 >( 0 ) ) ) != true )
			{
				HandleSerializationFailure();
			}
		}
	}

	void AbstractMetricsCapture::HandleSerializationFailure()
	{
		RED_LOG_WARNING( "Failed to serialize allocation info. Stopping memory capture." );
		Hook* hook = reinterpret_cast< Hook* >( m_hookHandle );
		RED_FATAL_ASSERT( hook != nullptr, "Hook does not exist" );
		RED_UNUSED( hook );
		m_oom = true;
		if ( m_outOfMetricsCaptureMemoryCallback )
		{
			m_outOfMetricsCaptureMemoryCallback();
			m_outOfMetricsCaptureMemoryCallback = []{};
		}
	}

}
}
