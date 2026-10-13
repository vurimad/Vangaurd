/**
* Copyright (c) 2016-17 CD Projekt Red. All Rights Reserved.
*/

#pragma once


//////////////////////////////////////////////////////////////////////////
// headers
#include "absolutePath.h"
#include "profiler.h"
#include "../../redMemory/include/metricsUtils.h"

namespace red
{
	class REDCORE_API MemoryProfilerExtension
	{
		const Uint32 _savingBufferSize = 65536;

	public:
		MemoryProfilerExtension();
		~MemoryProfilerExtension();

		// common
		Bool Init( const Uint32 bufferSize );
		void Shutdown();
		void Update();

		// control
		void Start( red::memory::OutOfProfilerMemoryCallback outOfProfilerMemoryCallback = []{} );
		void Stop();
		void NextFrame( red::ProfilerFrameType frameType );
		Bool Store( const red::String& filename );
		red::memory::Stream& GetOutputStream();

		Bool IsRunning() const;

		Bool IsOutOfMemory() const;
		void SetOutOfMemory();

		// todo:
		Float GetFilledBufferFactor();

	private:

		//! control flags
		Bool					m_stored; //! is current buffer was stored
		red::Atomic<Bool>		m_init;
		red::Atomic<Bool>		m_isRunning;
		red::Atomic<Bool>		m_oom;
		Uint32					m_reservedMem;
	};
}
