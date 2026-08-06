/**
* Copyright (c) 2014-17 CD Projekt Red. All Rights Reserved.
*/

#pragma once

//////////////////////////////////////////////////////////////////////////
// headers
#include "../../redMemory/include/uniqueBuffer.h"
#include "absolutePath.h"
#include "profiler.h"


namespace red
{
	class REDCORE_API RedProfilerTool
	{
	public:
	#ifdef USE_RED_PROFILER
		RedProfilerTool();
		~RedProfilerTool();

		// common
		void Init( const Uint32 mem );
		void Shutdown();
		void Update();
		
		// end frame markers
		void NextFrame( red::ProfilerFrameType frameType );

		// control
		void Start();
		void Stop();
		Bool Store( const red::String& filename, red::InstrumentationObject* handles[], const Uint32 handlesCount, const Bool isEditor );
		Bool StoreToMem( red::DynArray<Uint8>& buffer, red::InstrumentationObject* handles[], const Uint32 handlesCount, const Bool isEditor );

		// blocks
		void StartBlock( red::InstrumentationObject* block, const char* scopeName );
		void StopBlock( red::InstrumentationObject* block, const char* scopeName );

		// signals
		void Signal( red::InstrumentationObject* block );
		void Signal( red::InstrumentationObject* block, const Float value );
		void Signal( red::InstrumentationObject* block, const Int32 value );
		void Signal( red::InstrumentationObject* block, const char* msg );

		//! set atomic properties for Signal Strings
		RED_FORCE_INLINE void SetCurrentPosSignalStrings( const Uint8* newAddress, const Uint32 counter );
		//! get atomic properties for Signal Strings
		RED_FORCE_INLINE Uint64 GetCurrentPosSignalStrings( Uint8** newAddress, Uint32* counter ) const;
		//! return free block of memory if present	
		RED_FORCE_INLINE Uint64* GetBufferBlock64Bits( const Uint32 num64bits ); 

		Float GetFilledBufferFactor();
		Uint32 GetStoredFrames();
		Uint32 GetStoredBlocks();
		Uint32 GetStoredSignals();

		// Uses data as point of reference ( no deletion on dtor )
		 Bool SaveToFile( const char* filename, red::InstrumentationObject* handles[], const Uint32 handlesCount, const Bool isEditor ) const;

	private:

		void ClearBuffers();

		//! control flags
		Bool					m_stored; //! is current buffer was stored
		
		red::Atomic<Bool>		m_init;
        
		red::Atomic<Bool>		m_EOM;
		red::Atomic<Bool>		m_EOM_String;
		red::Atomic<Bool>		m_instrFuncConfigLoaded;

		//! blocks data
		Uint32					m_reservedMem;
		red::Atomic<Uint64*>	m_currentPos;
		Uint64*					m_endStopFramePos;
		red::UniqueBuffer		m_mem;
		red::Atomic<Uint32>		m_storedFrames;
		red::Atomic<Uint32>		m_storedBlocks;
		red::Atomic<Uint32>		m_storedSignals;

		// strings data
		Uint32					m_reservedMemSignalString;
		Uint8*					m_memSignalStrings;
		Uint32					m_signalStringsCount;
		red::Atomic<Uint64>		m_currPosSignalStrings; //! store offset and signals count	

		red::Timer				m_timer;
	#endif
	};
}
