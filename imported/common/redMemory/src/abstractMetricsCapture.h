/**
 * Copyright (c) 2017 CD Projekt Red. All Rights Reserved.
 */

#ifndef _RED_MEMORY_ABSTRACT_METRICS_CAPTURE_H_
#define _RED_MEMORY_ABSTRACT_METRICS_CAPTURE_H_

#include "spinLock.h"
#include "../include/hookTypes.h"
#include "../include/metricsUtils.h"
#include "../include/uniquePtr.h"
#include "../../redSystem/include/timer.h"

namespace red
{
namespace memory
{
	class HookHandler;
	class Callstack;
	class Stream;
	class Serializer;

	const u32 c_callstackCacheHashCount = 256;

	class AbstractMetricsCapture
	{
	public:
		AbstractMetricsCapture();

		void Initialize( HookHandler * hookHandler, PoolRegistry * poolRegistry );

		void Start( const u32 bufferSize, OutOfProfilerMemoryCallback outOfMetricsCaptureMemoryCallback = []{} );
		void Start( const char * filename );
		void Stop();
		void Reset();
		red::memory::Stream& GetStream();

		void WriteAllocate( PoolHandle poolHandle, const Block & block, const Callstack & callstack );
		void WriteFree( PoolHandle poolHandle, const Block & block, const Callstack & callstack );
		void WriteCurrentFrameTick();

	protected:
		virtual void WritePlatfromIdentifier( Serializer& serializer ) = 0;
		virtual void WriteLoadedModules( Serializer& serializer ) = 0;

	private:
		class CallstackCache
		{
		public:

			CallstackCache();
			~CallstackCache();

			bool IsCached( u64 hash );
			void Add( u64 hash );
			void Clear();

		private:
			u64 m_hashes[ c_callstackCacheHashCount ];
			u64 * m_writeMarker;
		};

		void WriteHeader();
		void WriteFooter();
		void WritePools();

		void HandleSerializationFailure();

		UniquePtr< Stream > m_stream;
		Serializer m_serializer;

		CallstackCache m_callstackAllocateCache;
		CallstackCache m_callstackFreeCache;
		Timer m_timer;
		Mutex m_monitor;
		Bool m_oom;

		HookHandler * m_hookHandler;
		HookHandle m_hookHandle;

		PoolRegistry * m_poolRegistry;
		OutOfProfilerMemoryCallback m_outOfMetricsCaptureMemoryCallback;
	};
	
}
}

#endif
