/**
* Copyright (c) 2014-17 CD Projekt Red. All Rights Reserved.
*/

//////////////////////////////////////////////////////////////////////////
// headers
#include "build.h"
#include "../../redSystem/include/timer.h"
#include "../../redSystem/include/threads.h"
#include "../../redSystem/include/redThreadsThread.h"
#include "../../redSystem/include/clock.h"
#include "../../redIO/include/redIOPublic.h"

#include "profilerToolRed.h"

#ifdef USE_RED_PROFILER

using red::DynArray;
using red::HashMap;
using red::String;

namespace red
{
	//////////////////////////////////////////////////////////////////////////
	// defines
	#define FRAME_QWORDS_COUNT 3
	#define START_QWORDS_COUNT 3
	#define END_QWORDS_COUNT 3
	#define SIGNAL_QWORDS_COUNT 3
	#define STARTPROFILE_QWORDS_COUNT FRAME_QWORDS_COUNT + 2 

	//////////////////////////////////////////////////////////////////////////
	// static init
	static const Uint16 FILE_VERSION = 0x0006;
	static const Uint16 FILE_ISEDITOR = 0x0100;
	static const Uint32 FILE_SIGNATURE = 0x66600000;
	static const Uint64 BLOCK_MASK = 0xF000000000000000ULL;
	static const Uint64 BLOCK_START_MASK = 0x1000000000000000ULL;
	static const Uint64 BLOCK_NAME_MASK = 0x0FFFFFFFFFFFFFFFULL;

	RED_TLS red::ThreadId* g_thredId = nullptr;
	RED_FORCE_INLINE static const red::ThreadId* GetThreadId()
	{
		if( g_thredId == nullptr )
		{
			g_thredId = RED_NEW( red::ThreadId, red::PoolDebug )();
			*g_thredId = red::ThreadId::CurrentThread();
		}
		return g_thredId;
	}
	#define GET_THREAD_ID GetThreadId()->id


	//////////////////////////////////////////////////////////////////////////
	RedProfilerTool::RedProfilerTool() :
		m_stored( false ),
		m_EOM( false ),   
		m_EOM_String( false ),
		m_instrFuncConfigLoaded( false ),

		//! blocks data
		m_reservedMem( 0 ),
		m_currentPos( nullptr ),
		m_endStopFramePos( nullptr ),
		m_storedFrames( 0 ),
		m_storedBlocks( 0 ),
		m_storedSignals( 0 ),

		// strings data
		m_reservedMemSignalString( 0 ),
		m_memSignalStrings( nullptr ),

		m_signalStringsCount( 0 )
	{
		// strings data
		SetCurrentPosSignalStrings( nullptr, 0 );
	}

	RedProfilerTool::~RedProfilerTool()
	{
		ClearBuffers();

		// free mem
		m_mem.Reset();
	}

    //////////////////////////////////////////////////////////////////////////
    //
    // return internal memory fill factor
    Float RedProfilerTool::GetFilledBufferFactor()
    {
		Double bufferFactor = 0.0;
		if( m_reservedMem )
		{
			Double factor = ((Double)sizeof( Uint64 ))/((Double)m_reservedMem);
			RED_THREADS_MEMORY_BARRIER(); // get m_currentPos.GetValue() in last possible moment
			bufferFactor = (m_currentPos.GetValue()-reinterpret_cast<Uint64*>( m_mem.Get()))*factor;
		}
        return (Float)bufferFactor;
    }

	//////////////////////////////////////////////////////////////////////////
    //
    // return no. of stored frames
    Uint32 RedProfilerTool::GetStoredFrames()
    {
        return m_storedFrames.GetValue();
    }
	//////////////////////////////////////////////////////////////////////////
	//
	// return stored blocks count
	Uint32 RedProfilerTool::GetStoredBlocks()
	{
		return m_storedBlocks.GetValue();
	}
	//////////////////////////////////////////////////////////////////////////
	//
	// return stored signals count
	Uint32 RedProfilerTool::GetStoredSignals()
	{
		return m_storedSignals.GetValue();
	}

	void RedProfilerTool::Init( const Uint32 mem )
	{
		Uint32 memSignalString = mem/10;

		memSignalString += 19; // for default text "STRING_MEMORY_FULL"

		m_mem.Reset();
		m_mem = red::CreateUniqueBuffer<red::PoolDebug>( mem, 8 );
		red::Memset( m_mem.Get(), 0, mem );
		m_reservedMem = mem;
		m_currentPos.SetValue( reinterpret_cast<Uint64*>( m_mem.Get() ) );
		m_endStopFramePos = reinterpret_cast<Uint64*>( m_mem.Get() );
		m_storedFrames.SetValue( 0 );
		m_storedBlocks.SetValue( 0 );
		m_storedSignals.SetValue( 0 );
		
		if ( m_memSignalStrings )
		{
			RED_FREE( red::PoolDebug, m_memSignalStrings );
		}
		m_memSignalStrings = (Uint8*)RED_ALLOCATE( red::PoolDebug, memSignalString );
		red::Memset( m_memSignalStrings, 0, memSignalString );
		red::Memcpy( m_memSignalStrings, "STRING_MEMORY_FULL", 18 );
		m_memSignalStrings[19] = '\0';			
		m_reservedMemSignalString = memSignalString;
		SetCurrentPosSignalStrings( m_memSignalStrings + 19, 1 );

		m_EOM.SetValue( false );
		m_EOM_String.SetValue( false );
		m_stored = false;

		RED_LOG( "Profiler: %u bytes buffer initiated [$%p-$%p]..", m_reservedMem, m_mem.Get(), reinterpret_cast<Uint64*>( m_mem.Get() )+mem );

		m_init.SetValue( true );

		// store freq and first frame at start        
		Uint64 freq = 0;
		m_timer.GetFrequency( freq );

		Uint64* bufferPtr = GetBufferBlock64Bits( STARTPROFILE_QWORDS_COUNT );
		
		if ( bufferPtr )
		{		
			*bufferPtr++ = 0xFFFFFFFFFFFFFFFF;
			*bufferPtr++ = freq;
			*bufferPtr++ = 0xFFFFFFFF00000001;
			*bufferPtr++ = (Uint64)red::memory::GetTotalBytesAllocated();
			*bufferPtr   = m_timer.GetTicks();
			m_storedFrames.Increment();
		}
	}

	void RedProfilerTool::Start()
	{
	}

	void RedProfilerTool::Stop()
	{
		// Save last frame
		Uint64* bufferPtr = GetBufferBlock64Bits( 0 ); // should be reserved
		if ( bufferPtr )
		{			
			*bufferPtr++ = 0xFFFFFFFF00000001;
			*bufferPtr++ = (Uint64)red::memory::GetTotalBytesAllocated();
			*bufferPtr++ = m_timer.GetTicks();
			m_endStopFramePos = bufferPtr;
			m_storedFrames.Increment();
		}
	}
	//////////////////////////////////////////////////////////////////////////
	// store on disk to specified file name
	Bool RedProfilerTool::Store( const red::String& filename, red::InstrumentationObject* handles[], const Uint32 handlesCount, const Bool isEditor )
	{
		// add extension
		red::AbsolutePath outputPath = red::AbsolutePath::CreateFilePath( filename );
		outputPath.AppendFilePath( ".prd" );

		// save
		RED_LOG( "Profiler: Storing profiling dump [pc]: [%hs]..", outputPath.AsChar() );

		if ( SaveToFile( outputPath.AsChar(), handles, handlesCount, isEditor ) == false )
		{
			RED_LOG_WARNING( "Profiler: Cannot save profile!!" );
			return false;
		}

		// def result
		return true;
	}
	//////////////////////////////////////////////////////////////////////////
	// temp: buffer stream writer
	void Write( red::DynArray<Uint8>& buffer, Uint32& pos, const void* data, Uint32 dataLen )
	{
		if ( pos + dataLen > buffer.Capacity() )
		{
			buffer.Resize( buffer.Size() + dataLen );
		}

		if ( data )
		{
			red::Memcpy( &buffer[0]+pos, data, dataLen );
		}
		
		pos += dataLen;
	}
	//////////////////////////////////////////////////////////////////////////
	// temp: store to memory buffer
	// will be replace soon by memory writer
	Bool RedProfilerTool::StoreToMem( red::DynArray<Uint8>& buffer, red::InstrumentationObject* handles[], const Uint32 handlesCount, const Bool isEditor )
	{
		// init
		buffer.Resize( m_reservedMem + handlesCount*8 );
		Uint32 written = 0;

		// Write signature
		const Uint32 FILEMARK = FILE_SIGNATURE | (FILE_ISEDITOR*isEditor) | FILE_VERSION;
		Write( buffer, written, &FILEMARK, sizeof(Int32) );

		// Write signal functions
		Uint8* currentPosBuffSignalStrings = (Uint8*)m_memSignalStrings;

		DynArray< const char* > stringsArray{ red::PoolDebug() };
		HashMap< Uint32, Uint32 > stringIndexToStringArrayIndexMap{ red::PoolDebug() };

		Uint8* currentPosSignalStrings = nullptr;
		Uint32 storedSignalStrings = 0;

		GetCurrentPosSignalStrings( &currentPosSignalStrings, &storedSignalStrings );

		Uint32 stringIndex = 0;
		while ( currentPosBuffSignalStrings < currentPosSignalStrings )
		{
			size_t stringArrayIndex = stringsArray.Size();
			for( size_t i = 0; i < stringsArray.Size(); i++ )
			{
				if( red::Strcmp( stringsArray[(Int32)i], (const char*)currentPosBuffSignalStrings ) == 0 )
				{
					stringArrayIndex = i;
					break;
				}
			}

			if( stringArrayIndex == stringsArray.Size() ) // add new string to array
			{
				stringsArray.PushBack( (const char*) currentPosBuffSignalStrings );
			}

			stringIndexToStringArrayIndexMap.Insert( stringIndex, (Uint32)stringArrayIndex );

			stringIndex++;

			const Uint32 length = (Uint32)strlen( (const char*)currentPosBuffSignalStrings );
			currentPosBuffSignalStrings += length + 1;
		}

		Uint32 stringArraySize = stringsArray.Size();
		Write( buffer, written, &stringArraySize, sizeof(Uint32) );

		for ( size_t stringIndex = 0 ; stringIndex < stringsArray.Size(); stringIndex++ )
		{
			const Uint32 length = (Uint32)strlen( stringsArray[(Int32)stringIndex] );
			Write( buffer, written, (void*)(&length), sizeof(Uint32) );
			Write( buffer, written, (void*)stringsArray[(Int32)stringIndex], length );
		}

		Uint32 mapSize = stringIndexToStringArrayIndexMap.Size();
		Write( buffer, written, &mapSize, sizeof(Uint32) );

		for ( HashMap< Uint32, Uint32 >::iterator it=stringIndexToStringArrayIndexMap.Begin(); it!=stringIndexToStringArrayIndexMap.End(); ++it )
		{
			Uint32 key = it.Key();
			Write( buffer, written, &key, sizeof(Uint32) );
			Write( buffer, written, &it.Value(), sizeof(Uint32) );
		}

		Uint64* bufferPtr = reinterpret_cast< Uint64* >( m_mem.Get() );
		const Uint32 bufferSize = static_cast< Uint32 >( m_endStopFramePos - bufferPtr ) * sizeof( Uint64 );

		// Write buffer size
		Write( buffer, written, (void*)(&bufferSize), sizeof(Uint32) );

		// Write buffer
		Write( buffer, written, bufferPtr, bufferSize );

		// Skip profile header
		bufferPtr += STARTPROFILE_QWORDS_COUNT;

		// Write block names
		const Uint64* eos = reinterpret_cast< const Uint64* >( reinterpret_cast< const char* >( bufferPtr ) + bufferSize );

		red::HashSet< Uint64 > storedBlockNames{ red::PoolDebug() };
		storedBlockNames.Reserve( handlesCount );

		while ( bufferPtr < eos )
		{
			const Uint64 key = *bufferPtr;
			if ( ( key & BLOCK_MASK ) == BLOCK_START_MASK && !storedBlockNames.Exist( key ) )
			{
				storedBlockNames.Insert( key );
				const char* blockName = reinterpret_cast< const char* >( reinterpret_cast< void* >( key & BLOCK_NAME_MASK ) );
				const Uint32 length = static_cast< Uint32 >( red::Strlen( blockName ) );
				Write( buffer, written, reinterpret_cast< const void* >( &key ), sizeof( Uint64 ) );
				Write( buffer, written, reinterpret_cast< const void* >( &length ), sizeof( Uint32 ) );
				Write( buffer, written, reinterpret_cast< const void* >( blockName ), length );
			}

			bufferPtr += FRAME_QWORDS_COUNT;
		}

		if ( buffer.Size() != written )
		{
			buffer.Resize( written );
		}

		return true;
	}

	void RedProfilerTool::Shutdown()
	{

	}

	////////////////////////////////////////////////////////////////////////////////////////////////////
	//
	// set atomic address and counter for string data
	void RedProfilerTool::SetCurrentPosSignalStrings( const Uint8* newAddress, const Uint32 counter )
	{
		Uint64 newValue = (Uint64)(newAddress - m_memSignalStrings);
		newValue = newValue << 32;
		newValue |= counter;
		m_currPosSignalStrings.SetValue( newValue );
	}

	void RedProfilerTool::StartBlock( red::InstrumentationObject* block, const char* scopeName )
	{
		Uint64 time = 0;

		if ( !block )
		{
			return;
		}

		if( block->m_enabled.GetValue() == false )
			return;

		time = m_timer.GetTicks();

		Uint64* bufferPtr = GetBufferBlock64Bits( START_QWORDS_COUNT );

		if ( bufferPtr )
		{
			const Uint64 scopeNameAsUint64 = reinterpret_cast< Uint64 >( scopeName );
			*bufferPtr++ = 0x1000000000000000 | scopeNameAsUint64;
			*bufferPtr++ = GET_THREAD_ID;
			*bufferPtr = time;
			m_storedBlocks.Increment();
		}
	}

	void RedProfilerTool::StopBlock( red::InstrumentationObject* block, const char* scopeName )
	{
		Uint64 time = 0;

		if ( !block )
		{
			return;
		}

		if( block->m_enabled.GetValue() == false )
			return;

		time = m_timer.GetTicks();

		Uint64* bufferPtr = GetBufferBlock64Bits( END_QWORDS_COUNT );

		if ( bufferPtr )
		{
			const Uint64 scopeNameAsUint64 = reinterpret_cast< Uint64 >( scopeName );
			*bufferPtr++ = 0x2000000000000000 | scopeNameAsUint64;
			*bufferPtr++ = GET_THREAD_ID;
			*bufferPtr   = time;		    
		}
	}

	//////////////////////////////////////////////////////////////////////////
	//
	// check does current pos is in memory range, count as 64 bits
	// return pointer to memory block, if not return null
	Uint64* RedProfilerTool::GetBufferBlock64Bits( const Uint32 num64bits )
	{
		if ( m_EOM.GetValue() )
		{
			return nullptr;
		}
		Uint64* newCurrentPos;   
		Uint64* oldCurrentPos; 
		do
		{
			oldCurrentPos = m_currentPos.GetValue();
			newCurrentPos = oldCurrentPos + num64bits;
			// Plus 3*64 bits for next frame marker (invoked in ProfilerManager::Store)
			if ( (Uint32)((newCurrentPos-reinterpret_cast<Uint64*>(m_mem.Get()))*sizeof(Uint64) + FRAME_QWORDS_COUNT*sizeof(Uint64)) > m_reservedMem )
			{
				RED_LOG_WARNING( "Profiler: Memory is full!!" );
				m_endStopFramePos = oldCurrentPos;
				m_EOM.SetValue( true );
				return nullptr;
			}
		} while ( m_currentPos.CompareExchange( newCurrentPos, oldCurrentPos ) != oldCurrentPos );
		
		return oldCurrentPos;
	}

	////////////////////////////////////////////////////////////////////////////////////////////////////
	//
	// get atomic address and counter of string data
	Uint64 RedProfilerTool::GetCurrentPosSignalStrings( Uint8** newAddress, Uint32* counter ) const 
	{
		Uint64 value = m_currPosSignalStrings.GetValue();
		*counter = (Uint32)(value & 0x00000000FFFFFFFF);
		*newAddress = m_memSignalStrings;
		*newAddress += (Uint32)(value >> 32);		
		return value;
	}

	void RedProfilerTool::Update()
	{

	}
	//////////////////////////////////////////////////////////////////////////
	// next frame marker
	void RedProfilerTool::NextFrame( red::ProfilerFrameType frameType )
	{
		Uint64* bufferPtr = GetBufferBlock64Bits( FRAME_QWORDS_COUNT );

		if ( bufferPtr )
		{
			// todo: will be splitted soon (in next prd file format)
			if ( frameType == PFT_GAMEPLAY || frameType == PFT_EDITOR || frameType == PFT_ENGINE || frameType == PFT_NETWORK )
			{
				*bufferPtr++ = 0xFFFFFFFF00000001 | ((Uint32)frameType)<<24;
				*bufferPtr++ = static_cast<Uint64>( red::memory::GetTotalBytesAllocated() );
				*bufferPtr   = m_timer.GetTicks();
				m_storedFrames.Increment();
			}
		}
	}

	void RedProfilerTool::Signal( InstrumentationObject* scope )
	{
		if ( !scope )
		{
			return;
		}

		if( scope->m_enabled.GetValue() == false )
			return;

		const Uint64 time = m_timer.GetTicks();

		Uint64* bufferPtr = GetBufferBlock64Bits( SIGNAL_QWORDS_COUNT );

		if ( bufferPtr )
		{			
			*bufferPtr++ = 0x3333333333330000 | scope->m_id;
			*bufferPtr++ = GET_THREAD_ID;
			*bufferPtr   = time;
			m_storedSignals.Increment();
		}
	}

	void RedProfilerTool::Signal( InstrumentationObject* scope, const Float value )
	{
		if ( !scope )
		{
			return;
		}

		if( scope->m_enabled.GetValue() == false )
			return;

		const Uint64 time = m_timer.GetTicks();

		Uint64* bufferPtr = GetBufferBlock64Bits( SIGNAL_QWORDS_COUNT );

		if ( bufferPtr )
		{		    
			*bufferPtr++ = 0x0000000044440000 | scope->m_id | (((Uint64)*((Uint32*)&value))<<32);
			*bufferPtr++ = GET_THREAD_ID;
			*bufferPtr   = time;
			m_storedSignals.Increment();
		}
	}

	void RedProfilerTool::Signal( InstrumentationObject* scope, const Int32 value )
	{
		if ( !scope )
		{
			return;
		}

		if( scope->m_enabled.GetValue() == false )
			return;

		const Uint64 time = m_timer.GetTicks();

		Uint64* bufferPtr = GetBufferBlock64Bits( SIGNAL_QWORDS_COUNT );

		if ( bufferPtr )
		{			
			*bufferPtr++ = 0x0000000055550000 | scope->m_id | (((Uint64)value)<<32);
			*bufferPtr++ = GET_THREAD_ID;
			*bufferPtr   = time;
			m_storedSignals.Increment();
		}
	}

	void RedProfilerTool::Signal( InstrumentationObject* block, const char* msg )
	{
		// todo:
	}

	//////////////////////////////////////////////////////////////////////////
	//
    void RedProfilerTool::ClearBuffers()
    {
		m_signalStringsCount	= 0;
    }

    //////////////////////////////////////////////////////////////////////////
	//
	Bool RedProfilerTool::SaveToFile( const char* fileName, red::InstrumentationObject* handles[], const Uint32 handlesCount, const Bool isEditor ) const
	{
		if ( !m_mem.Get() || !fileName )
		{
			return false;
		}

		io::NativeFileHandle writer;
		writer.Open( fileName, io::eOpenFlag_WriteNew );
		if ( writer.IsValid() )
		{
			Uint32 written = 0;

			// Write signature
			const Uint32 FILEMARK = FILE_SIGNATURE | (FILE_ISEDITOR*isEditor) | FILE_VERSION;
			writer.Write( &FILEMARK, sizeof(Int32), written );

			// Write signal functions
			Uint8* currentPosBuffSignalStrings = (Uint8*)m_memSignalStrings;

			DynArray< const char* > stringsArray{ red::PoolDebug() };
			HashMap< Uint32, Uint32 > stringIndexToStringArrayIndexMap{ red::PoolDebug() };

			Uint8* currentPosSignalStrings = nullptr;
			Uint32 storedSignalStrings = 0;

			GetCurrentPosSignalStrings( &currentPosSignalStrings, &storedSignalStrings );

			Uint32 stringIndex = 0;
			while( currentPosBuffSignalStrings < currentPosSignalStrings )
			{
				size_t stringArrayIndex = stringsArray.Size();
				for( size_t i = 0; i < stringsArray.Size(); i++ )
				{
					if( red::Strcmp( stringsArray[(Int32)i], (const char*)currentPosBuffSignalStrings ) == 0 )
					{
						stringArrayIndex = i;
						break;
					}
				}

				if( stringArrayIndex == stringsArray.Size() ) // add new string to array
				{
					stringsArray.PushBack( (const char*) currentPosBuffSignalStrings );
				}

				stringIndexToStringArrayIndexMap.Insert( stringIndex, (Uint32)stringArrayIndex );

				stringIndex++;

				const Uint32 length = (Uint32)strlen( (const char*)currentPosBuffSignalStrings );
				currentPosBuffSignalStrings += length + 1;
			}

			Uint32 stringArraySize = stringsArray.Size();
			writer.Write( &stringArraySize, sizeof(Uint32), written );

			for ( size_t stringIndex = 0 ; stringIndex < stringsArray.Size(); stringIndex++ )
			{
				const Uint32 length = (Uint32)strlen( stringsArray[(Int32)stringIndex] );
				writer.Write( (void*)(&length), sizeof(Uint32), written );
				writer.Write( (void*)stringsArray[(Int32)stringIndex], length, written );
			}

			Uint32 mapSize = stringIndexToStringArrayIndexMap.Size();
			writer.Write( &mapSize, sizeof(Uint32), written );

			for ( HashMap< Uint32, Uint32 >::iterator it=stringIndexToStringArrayIndexMap.Begin(); it!=stringIndexToStringArrayIndexMap.End(); ++it )
			{
				Uint32 key = it.Key();
				writer.Write( &key, sizeof(Uint32), written );
				writer.Write( &it.Value(), sizeof(Uint32), written );
			}

			Uint64* bufferPtr = reinterpret_cast<Uint64*>( m_mem.Get() );
			const Uint32 bufferSize = (Uint32)( m_endStopFramePos - bufferPtr )*sizeof(Uint64);

			// Write buffer size
			writer.Write( (void*)(&bufferSize), sizeof(Uint32), written );

			// Write buffer
			writer.Write( bufferPtr, bufferSize, written );

			// Skip profile header
			bufferPtr += STARTPROFILE_QWORDS_COUNT;

			// Write block names
			const Uint64* eos = reinterpret_cast< const Uint64* >( reinterpret_cast< const char* >( bufferPtr ) + bufferSize );

			red::HashSet< Uint64 > storedBlockNames{ red::PoolDebug() };
			storedBlockNames.Reserve( handlesCount );

			while ( bufferPtr < eos )
			{
				const Uint64 key = *bufferPtr;
				if ( ( key & BLOCK_MASK ) == BLOCK_START_MASK && !storedBlockNames.Exist( key ) )
				{
					storedBlockNames.Insert( key );
					const char* blockName = reinterpret_cast< const char* >( reinterpret_cast< void* >( key & BLOCK_NAME_MASK ) );
					const Uint32 length = static_cast< Uint32 >( red::Strlen( blockName ) );
					writer.Write( reinterpret_cast< const void* >( &key ), sizeof( Uint64 ), written );
					writer.Write( reinterpret_cast< const void* >( &length ), sizeof( Uint32 ), written );
					writer.Write( reinterpret_cast< const void* >( blockName ), length, written );
				}

				bufferPtr += FRAME_QWORDS_COUNT;
			}

			writer.Flush();
			writer.Close();
		}
		else
		{
			RED_LOG_WARNING( "Cannot create profile file: '%hs'", fileName );
			return false;
		}

		return true;
	}
}

#else 
	RED_NO_EMPTY_FILE();
#endif
