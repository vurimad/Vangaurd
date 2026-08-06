/**
* Copyright (c) 2013-16 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"
#include "../../redMemory/include/metricsUtils.h"
#include "../../redMemory/include/redMemoryPublic.h"
#include "../../redSystem/include/timer.h"
#include "../../redSystem/include/threads.h"
#include "../../redSystem/include/redThreadsThread.h"
#include "../../redSystem/include/clock.h"
#include "../../redSystem/include/types.h"
#include "../../redContainers/include/string/stringLocale.h"

// manual debug usage of profiler - dump timing for block start/stop to the log
namespace DebugProfiler
{
	class CDebugProfilerPrinter
	{
	public:
		CDebugProfilerPrinter();

		void Start( const char* name );
		void End();

		void SignalStart( const char* name, red::Timer& timer );
		void SignalEnd( const char* name, red::Timer& timer );

	private:
		static const Uint32 MAX_DEBUG_PROFILER_DEPTH = 30;
		static const Uint32 MAX_ENTRIES = 1 << 20; // 1M

		struct Entry
		{
			Uint64			m_namePtr;	// bit0 is used to mark push/pop
			Uint64			m_ticks;
		};

		Uint32 m_enabled;
		Int32 m_depth;
		Entry m_entries[ MAX_ENTRIES ];
		Uint32 m_numEntries;
		//String m_name;

		void Dump( const char* name );

	} GDebugProfilerPrinter;

	CDebugProfilerPrinter::CDebugProfilerPrinter()
		: m_enabled( 0 )
		, m_depth( 0 )
		, m_numEntries( 0 )
	{}

	void CDebugProfilerPrinter::Start( const char* name )
	{
		if ( 0 == m_enabled++ )
		{
			//m_name = name;
			m_numEntries = 0;
			m_depth = 0;
		}
	}

	void CDebugProfilerPrinter::End()
	{
		if ( 0 == --m_enabled )
		{
			//Dump( m_name.AsChar() );
			//m_name = String::EMPTY();
		}
	}

	void CDebugProfilerPrinter::SignalStart( const char* name, red::Timer& timer )
	{
		if ( m_enabled && ::SIsMainThread() )
		{
			if ( m_depth < MAX_DEBUG_PROFILER_DEPTH )
			{
				if ( m_numEntries < MAX_ENTRIES )
				{
					m_entries[ m_numEntries ].m_namePtr = reinterpret_cast<Uint64>(name) | 1; // start
					m_entries[ m_numEntries ].m_ticks = timer.GetTicks();
					++m_numEntries;
				}
			}

			++m_depth;
		}
	}

	void CDebugProfilerPrinter::SignalEnd( const char* name, red::Timer& timer )
	{
		if ( m_enabled && ::SIsMainThread() )
		{
			--m_depth;

			if ( m_depth >= 0 && m_depth < MAX_DEBUG_PROFILER_DEPTH )
			{
				if ( m_numEntries < MAX_ENTRIES )
				{
					m_entries[ m_numEntries ].m_namePtr = reinterpret_cast<Uint64>(name) | 0; // end
					m_entries[ m_numEntries ].m_ticks = timer.GetTicks();
					++m_numEntries;
				}
			}
		}
	}

	void CDebugProfilerPrinter::Dump( const char* name )
	{
		// get current system time
		red::DateTime time;
		red::Clock::GetInstance().GetUTCTime( time );

		// get base output path
		const UniChar* baseOutputPath = L"c://"; //GFileManager->GetTempDirectory().AsChar();

		// format file name
		UniChar fileName[ 512 ];
		red::SNPrintFUnsafe( fileName, RED_ARRAY_COUNT_U32(fileName), TXT("%lsprofile_%hs_[%04d_%02d_%02d][%02d_%02d_%02d].txt"),
			baseOutputPath, name,
			time.GetYear(), time.GetMonth(), time.GetDay(), time.GetHour(), time.GetMinute(), time.GetSecond() );

		RED_LOG( "Core: Dumping profiling data to '%ls'...", fileName );

		// open file writer
#if defined( RED_PLATFORM_ORBIS ) ||defined( RED_PLATFORM_LINUX )
		FILE* f = fopen( UNICODE_TO_ANSI(fileName), "w" );
#else
		FILE* f = nullptr;
		errno_t res =_wfopen_s( &f, fileName, L"w" );
#endif
		if ( f )
		{
			Uint32 depthLevel = 0;

			// frequency
			Uint64 freq = 1;
			red::Timer timer;
			timer.GetFrequency( freq );

			// starting times for each depth
			Uint64 startTicks[ MAX_DEBUG_PROFILER_DEPTH ];

			// process entries
			fwprintf( f, TXT("%d profiling entries\n"), m_numEntries );
			for ( Uint32 i=0; i<m_numEntries; ++i )
			{
				const Entry& info = m_entries[ i ];

				// start ?
				const char* name = (const char*)( info.m_namePtr & ~1 );
				if ( info.m_namePtr & 1 )
				{
					startTicks[ depthLevel ] = info.m_ticks;

					// format lead string
					AnsiChar leadString[ MAX_DEBUG_PROFILER_DEPTH*2 + 1 ];
					for ( Uint32 i=0; i<depthLevel; ++i )
					{
						leadString[i] = '\t';
					}
					leadString[depthLevel] = 0;

					if ( depthLevel > 0 )
					{
						const Uint64 delta = startTicks[ depthLevel ] - startTicks[ depthLevel-1 ];
						fprintf( f, "%s+%s @%1.4f\n", leadString, name, ((Double)(delta) / (Double)freq) * 1000.0 );
					}
					else
					{
						fprintf( f, "%s+%s\n", leadString, name );
					}

					++depthLevel;
				}
				else if ( depthLevel > 0 )
				{
					--depthLevel;

					// format lead string
					AnsiChar leadString[ MAX_DEBUG_PROFILER_DEPTH*2 + 1 ];
					for ( Uint32 i=0; i<depthLevel; ++i )
					{
						leadString[i] = '\t';
					}
					leadString[depthLevel] = 0;

					const Uint64 delta = info.m_ticks - startTicks[ depthLevel ];
					fprintf( f, "%s-%s %1.4f\n", leadString, name, ((Double)(delta) / (Double)freq) * 1000.0 );
				}
			}

			fclose(f);
		}

		RED_LOG( "Core: Finished dumping profiling data" );
	}

	void StartDebugProfiler( const char* name )
	{
		GDebugProfilerPrinter.Start( name );
	}

	void EndDebugProfiler()
	{
		GDebugProfilerPrinter.End();
	}
};

//////////////////////////////////////////////////////////////////////////
// implementations

//////////////////////////////////////////////////////////////////////////
// namespaces
namespace red
{    
    //////////////////////////////////////////////////////////////////////////
    //
    // signal with string value
/*
    void InternalProfiler::Signal( InstrumentedFunction* instrFunc, const char* name)
    {
#ifdef RED_ARCH_X64
        if ( !instrFunc || !m_started.GetValue() || name == NULL || m_EOM_String.GetValue() )
		{
            return;
		}

		if( instrFunc->m_enabled.GetValue() == false )
			return;

		const Uint64 time = m_timer.GetTicks();
		Uint64* bufferPtr = GetBufferBlock64Bits( SIGNAL_QWORDS_COUNT );

		if ( bufferPtr != NULL )
		{			
			Uint32 strSize = (Uint32)red::Strlen( name ) + 1;

			Uint64 newValue = 0;
			Uint64 oldValue = 0;
			Uint8* oldCurrentPosSignalStrings = NULL;
			Uint32 oldStoredSignalStrings = 0;
			do 
			{
				oldValue = GetCurrentPosSignalStrings( &oldCurrentPosSignalStrings, &oldStoredSignalStrings );
				newValue = (Uint32)(oldCurrentPosSignalStrings - m_memSignalStrings);
				newValue += strSize;
				if( newValue > m_reservedMemSignalString )
				{
					newValue = 0;
					break;
				}
				newValue = newValue << 32;
				newValue |= (Uint64)(oldStoredSignalStrings+1);
			} while ( m_currPosSignalStrings.CompareExchange( newValue, oldValue ) != oldValue );

			if( newValue )
			{
				// copy name				
				red::Memcpy( oldCurrentPosSignalStrings, name, strSize-1 );
				oldCurrentPosSignalStrings[strSize-1] = '\0';
				*bufferPtr++ = 0x0000000066660000 | instrFunc->m_id | ( ( (Uint64)oldStoredSignalStrings ) << 32 );					
			}
			else
			{
				*bufferPtr++ = 0x0000000066660000 | instrFunc->m_id ;
				RED_LOG_WARNING( "Profiler: string memory is full!!" );
				m_EOM_String.SetValue( true );
			}			
			*bufferPtr++ = GET_THREAD_ID;
			*bufferPtr = time;
        }
		
		// internal timer
		const Uint64 timeDelta = (m_timer.GetTicks()-time);
		RED_THREADS_MEMORY_BARRIER();
		m_internalTicks.SetValue( timeDelta + m_internalTicks.GetValue() );
#else
		RED_UNUSED(instrFunc);
		RED_UNUSED(name);
#endif //RED_ARCH_X64* /
    }*/

	//////////////////////////////////////////////////////////////////////////
	//
	// disable instrumented functions present in m_instrFuncDisabled list
/*
	void InternalProfiler::DisableIntrFuncs()
	{
		if( m_instrFuncConfigLoaded.GetValue() )
		{
			const Uint32 instrFuncCount = m_instrFuncsCounter.GetValue();
			Uint32 instrFuncIndex = 0;
			while ( instrFuncIndex < instrFuncCount)
			{
				HashMap<String, Bool>::iterator foundElement = m_instrFuncDisabled.Find( m_instrFuncs[instrFuncIndex]->m_name );
				if( foundElement != m_instrFuncDisabled.End() )
				{
					m_instrFuncs[instrFuncIndex]->m_enabled.SetValue( false );
				}
				++instrFuncIndex;
			}
		}				
	}*/
	////////////////////////////////////////////////////////////////////////////////////////////////////
	//
	// load list of disabled instrumented functions from instrFuncDisabled.txt m_instrFuncDisabled list
/*
	Bool InternalProfiler::LoadInstrFuncEnabledFile()
	{
		Bool fileLoaded = false;

#ifdef RED_PLATFORM_DURANGO
		AbsolutePath pathToInstrFuncEnabledFile = AbsolutePath::CreateDirPath( "d:\\" );
#else
		AbsolutePath pathToInstrFuncEnabledFile = AbsolutePath::CreateFilePath( "c:\\" ); //GFileManager->GetTempDirectory();
#endif // RED_PLATFORM_DURANGO

		pathToInstrFuncEnabledFile.AddFilePath( "instrFuncDisabled.txt" );

		RED_LOG( "Profiler: Try to load: %ls", pathToInstrFuncEnabledFile.AsChar() );*/

/*
		IFile* instrFuncDisabledFileData = GFileManager->CreateFileReader( pathToInstrFuncEnabledFile, FOF_Buffered|FOF_AbsolutePath );

		if( instrFuncDisabledFileData )
		{
			RED_LOG( "Profiler: File %hs opened.", pathToInstrFuncEnabledFile.AsChar() );
			size_t dataSize = (size_t)instrFuncDisabledFileData->GetSize();
			if( dataSize > 0 )
			{
				Uint8* buffer = new Uint8[dataSize];
				instrFuncDisabledFileData->Serialize( buffer, dataSize ); 
				String newInstFuncName;
				Uint32 dataIndex = 0;
				while ( dataIndex < dataSize )
				{
					if( buffer[dataIndex] == '\n' )
					{
						if( !newInstFuncName.Empty() )
						{
							m_instrFuncDisabled.Set( newInstFuncName, false );
							fileLoaded = true;
						}	
						newInstFuncName.Clear();
					}
					else if( buffer[dataIndex] == '\r' )
					{
						//skip this character
					}
					else
					{
						newInstFuncName.Append( buffer[dataIndex] );
					}
					++dataIndex;
				}
				if( !newInstFuncName.Empty() )
				{
					m_instrFuncDisabled.Set( newInstFuncName, false );
					fileLoaded = true;
				}
			}
			RED_DELETE( instrFuncDisabledFileData );
			RED_LOG( "Profiler: %u instrumented functions disabled.", m_instrFuncDisabled.Size() );
		}
		else 
		{
			RED_LOG( "Profiler: Could not open %hs.", pathToInstrFuncEnabledFile.AsChar() );
		}*/
/*
		return fileLoaded;
	}*/

} // namespace Profiler


//////////////////////////////////////////////////////////////////////////
// EOF
