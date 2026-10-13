/*
* Copyright (c) 2018 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"
#include "redIOTraceWriterOrbis.h"

#if defined( RED_PLATFORM_ORBIS ) && defined( USE_PROFILER )

#include "redIOProfilerOrbis.h"
#include "redIOProfilerVisitor.h"

#include "../../../../external/rapidjson/include/rapidjson/prettywriter.h"
#include "../../../../external/rapidjson/include/rapidjson/reader.h"
#include "../../../../external/rapidjson/include/rapidjson/filewritestream.h"
#include "../../../../external/rapidjson/include/rapidjson/document.h"

#include <rtc.h>

namespace res
{
	namespace json
	{
		class JSONAllocator
		{
		public:

			static void* Malloc( size_t size )
			{
				return RED_ALLOCATE( red::PoolDebug, size );
			}

			static void* Realloc( void* originalPtr, size_t originalSize, size_t newSize )
			{
				return RED_REALLOCATE( red::PoolDebug, originalPtr, newSize );
			}

			static void Free( void *ptr )
			{
				RED_FREE( red::PoolDebug, ptr );
			}
		};
	}
}

namespace rapidjson
{
	using JSONDocument = rapidjson::GenericDocument< rapidjson::UTF8< red::AnsiChar >, rapidjson::MemoryPoolAllocator< res::json::JSONAllocator >, res::json::JSONAllocator >;
	using JSONValue = rapidjson::GenericValue< rapidjson::UTF8< red::AnsiChar >, rapidjson::MemoryPoolAllocator< res::json::JSONAllocator > >;
}

class JsonFileWriter
{
public:
	typedef AnsiChar Ch;

	explicit JsonFileWriter( FILE* file )
		: m_file( file )
	{
	}

	void Put( const AnsiChar ch )
	{
		fputc( ch, m_file );
	}

	void Flush()
	{
		fflush( m_file );
	}

private:
	FILE* m_file;
};

using RapidJsonWriter = rapidjson::PrettyWriter< JsonFileWriter, rapidjson::UTF8< red::AnsiChar >, rapidjson::UTF8< red::AnsiChar >, res::json::JSONAllocator >;
using RapidJsonDocument = rapidjson::GenericDocument< rapidjson::UTF8< red::AnsiChar >, rapidjson::MemoryPoolAllocator< res::json::JSONAllocator >, res::json::JSONAllocator >;
using RapidJsonValue = rapidjson::GenericValue< rapidjson::UTF8< red::AnsiChar >, rapidjson::MemoryPoolAllocator< res::json::JSONAllocator > >;

namespace io
{
namespace orbis
{
	class TraceWriter::ProfilerVisitorJSONWriter : public orbis::IProfileStreamVisitor
	{
	public:
		ProfilerVisitorJSONWriter( RapidJsonWriter& writer, const Profiler::ResultInfo& resultInfo, const red::Map< FileOffsetSize, String >& logicalFileNameMap )
			: m_writer( writer )
			, m_logicalFileNameMap( logicalFileNameMap )
			, m_resultInfo( resultInfo )
			, m_freq( 0 )
		{
			m_aioFreeFakeThreads.Resize( 3 );
			m_aioFakeThread.Resize( 3 );

			for ( auto& it : m_aioFreeFakeThreads )
			{
				it.SetAll();
			}

			red::Clock::GetInstance().GetTimer().GetFrequency( m_freq );		
		}

		~ProfilerVisitorJSONWriter()
		{
		}

		void Start()
		{
			m_writer.StartObject();
			m_writer.Key( "traceEvents" );
			m_writer.StartArray();

			struct PriorityInfo
			{
				Uint32 value;
				const char* name;
			}
			priInfos[] = {
				{ SCE_KERNEL_AIO_PRIORITY_LOW, "LowPriority" },
				{ SCE_KERNEL_AIO_PRIORITY_MID, "MidPriority" },
				{ SCE_KERNEL_AIO_PRIORITY_HIGH, "HighPriority" } };

			for ( const auto& info : priInfos )
			{
				m_writer.StartObject();
				{
					m_writer.Key( "pid" ); m_writer.Uint( info.value );
					m_writer.Key( "tid" ); m_writer.Uint( 0 );
					m_writer.Key( "ts" ); m_writer.Uint64( 0 );
					m_writer.Key( "ph" ); m_writer.String( "M" );
					m_writer.Key( "cat" ); m_writer.String( "__metadata" );
					m_writer.Key( "name" ); m_writer.String( "process_name" );
					m_writer.Key( "args" );
					m_writer.StartObject();
					{
						m_writer.Key( "name" ); m_writer.String( info.name );
					}
					m_writer.EndObject();
				}
				m_writer.EndObject();
			}
		}

		void Finish()
		{
			m_writer.EndArray();

			m_writer.Key( "AIO SCE_PERF_TRACE_STOP_BUFFER_FULL" ); m_writer.Bool( m_resultInfo.aioWriteBufferFull );
			m_writer.Key( "BIO2 SCE_PERF_TRACE_STOP_BUFFER_FULL" ); m_writer.Bool( m_resultInfo.bio2WriteBufferFull );

			m_writer.EndObject();
		}

	private:
		static const char* GetEventName( const String* logicalFileName )
		{
			if ( !logicalFileName )
			{
				return "<UNKNOWN>";
			}

			// More useful to use the full name in order aggregate how many times a file was loaded
			// Otherwise can falsely aggregate files with the same stem, but different paths.
			return logicalFileName->AsChar();

#if 0
			const char* lastSlash = red::StrchrR( logicalFileName->AsChar(), '\\' );
			if ( !lastSlash )
			{
				return logicalFileName->AsChar();
			}

			return lastSlash + 1;
#endif
		}

		static const Uint64 GetEventTimeInMicroseconds( Uint64 eventTime, Uint64 baseEventTime, Uint64 freq )
		{
			//RED_FATAL_ASSERT( eventTime >= baseEventTime ); // timestamps after suspend/resume?
			const Uint64 delta = 1000000UL * ( eventTime - baseEventTime );
			return delta / freq;
		}

		virtual void OnDecompression( const DecompressionEvent& event ) override
		{
			m_writer.StartObject();
			{
				m_writer.Key( "pid" ); m_writer.Uint( 100 );
				m_writer.Key( "tid" ); m_writer.Uint( event.threadID );
				m_writer.Key( "ts" ); m_writer.Uint64( GetEventTimeInMicroseconds( event.timestampBegin, m_resultInfo.traceStartedTimestamp, m_freq ) );
				m_writer.Key( "dur" ); m_writer.Uint64( GetEventTimeInMicroseconds( event.timestampEnd - event.timestampBegin, 0, m_freq ) );
				m_writer.Key( "ph" ); m_writer.String( "X" );
				m_writer.Key( "cat" ); m_writer.String( "decompression" );
				m_writer.Key( "name" ); m_writer.String( event.fileName.AsChar() );
			}
			m_writer.EndObject();
		}

		virtual void OnIOWorkerCPU( const IOWorkerCPUEvent& event ) override
		{
			m_writer.StartObject();
			{
				m_writer.Key( "pid" ); m_writer.Uint( event.priority );
				m_writer.Key( "tid" ); m_writer.Uint( 999 );
				m_writer.Key( "ts" ); m_writer.Uint64( GetEventTimeInMicroseconds( event.timestampBegin, m_resultInfo.traceStartedTimestamp, m_freq ) );
				m_writer.Key( "dur" ); m_writer.Uint64( GetEventTimeInMicroseconds( event.timestampEnd - event.timestampBegin, 0, m_freq ) );
				m_writer.Key( "ph" ); m_writer.String( "X" );
				m_writer.Key( "cat" ); m_writer.String( "aio,worker" );
				m_writer.Key( "name" ); m_writer.String( "IOWorker" );
			}
			m_writer.EndObject();
		}

		virtual void OnMark( const MarkEvent& event ) override
		{
			m_writer.StartObject();
			{
				m_writer.Key( "pid" ); m_writer.Uint( 0 );
				m_writer.Key( "tid" ); m_writer.Uint( 0 );
				m_writer.Key( "s" ); m_writer.String( "g" ); // global scope
				m_writer.Key( "ts" ); m_writer.Uint64( GetEventTimeInMicroseconds( event.timestamp, m_resultInfo.traceStartedTimestamp, m_freq ) );
				m_writer.Key( "ph" ); m_writer.String( "I" );
				m_writer.Key( "cat" ); m_writer.String( "aio,mark" );
				m_writer.Key( "name" ); m_writer.String( event.eventName.AsChar() );
			}
			m_writer.EndObject();
		}

		virtual void OnAioSubmitCmdInRead( const AioSubmitCmdInEvent& event ) override
		{
			m_submitEvents[ event.submitUniqueID ] = event;

			m_writer.StartObject();
			{
				m_writer.Key( "pid" ); m_writer.Uint( event.priority );
				m_writer.Key( "tid" ); m_writer.Uint( 9999 );
				m_writer.Key( "ts" ); m_writer.Uint64( GetEventTimeInMicroseconds( event.timestamp, m_resultInfo.traceStartedTimestamp, m_freq ) );
				m_writer.Key( "ph" ); m_writer.String( "i" );
				m_writer.Key( "cat" ); m_writer.String( "aio,submit" );
				m_writer.Key( "name" ); m_writer.String( "SubmitCmd" );
			}
			m_writer.EndObject();			
		}

		virtual void OnAioInRead( const AioInEvent& event ) override
		{
			auto* submitEvent = m_submitEvents.FindPtr( event.submitUniqueID );
			if ( !submitEvent )
			{
				return;
			}

			FileOffsetSize key;
			{
				key.fileID = submitEvent->fileInfo.fileDescriptor;
				key.offset = submitEvent->fileInfo.offset;
				key.size = submitEvent->fileInfo.numberOfBytes;
			}

			const String* logicalFileName = m_logicalFileNameMap.FindPtr( key );

			const Uint32 priority = submitEvent->priority;

			auto& pending = m_aioFakeThread[ priority - 1 ];

			for ( auto& it : pending )
			{
				if ( it.submitUniqueID == event.submitUniqueID )
				{
					// skip it, large file and broken up into multiple AioReadIn events, but won't have a balancing AioReadOut
					return;
				}
			}

			const Uint32 tid = m_aioFreeFakeThreads[ priority -1 ].FindNextSet( 0 );
			RED_FATAL_ASSERT( tid != m_aioFreeFakeThreads[ priority -1 ].Size() );
			m_aioFreeFakeThreads[ priority -1 ].Clear( tid );
			FakeThreadInfo info;
			info.submitUniqueID = event.submitUniqueID;
			info.threadID = tid;
			pending.PushBack( info );

			m_writer.StartObject();
			{
				m_writer.Key( "pid" ); m_writer.Uint( priority );
				m_writer.Key( "tid" ); m_writer.Uint( tid );
				m_writer.Key( "ts" ); m_writer.Uint64( GetEventTimeInMicroseconds( event.timestamp, m_resultInfo.traceStartedTimestamp, m_freq ) );
				m_writer.Key( "ph" ); m_writer.String( "B" );
				m_writer.Key( "cat" ); m_writer.String( "aio,read" );
				m_writer.Key( "name" ); m_writer.String( GetEventName( logicalFileName ) );

				m_writer.Key( "args" );
				m_writer.StartObject();
				{
					m_writer.Key( "logicalFileName" ); m_writer.String( logicalFileName ? logicalFileName->AsChar() : "<UNKNOWN>" );

					char fmtBuf[ 64 ];
					red::SNPrintFUnsafe( fmtBuf, RED_ARRAY_COUNT_U32( fmtBuf ), "reqID=0x%llX,syscallID=0x%llX", event.submitUniqueID.requestID, event.submitUniqueID.submitSyscallUniqueID );
					m_writer.Key( "aioID" ); m_writer.String( fmtBuf );
				}
				m_writer.EndObject();
			}
			m_writer.EndObject();
		}

		virtual void OnAioOutRead( const AioOutEvent& event ) override
		{
			auto* submitEvent = m_submitEvents.FindPtr( event.submitUniqueID );
			if ( !submitEvent )
			{
				return;
			}
			
			const Uint32 priority = submitEvent->priority;
		
			FileOffsetSize key;
			{
				key.fileID = submitEvent->fileInfo.fileDescriptor;
				key.offset = submitEvent->fileInfo.offset;
				key.size = submitEvent->fileInfo.numberOfBytes;
			}

			const String* logicalFileName = m_logicalFileNameMap.FindPtr( key );

			Uint32 threadID = m_aioFreeFakeThreads[ priority -1 ].Size();
			auto& pending = m_aioFakeThread[ priority - 1 ];
			for ( Uint32 i : pending.Indices() )
			{
				if ( pending[ i ].submitUniqueID == event.submitUniqueID )
				{
					threadID = pending[ i ].threadID;
					
					pending.RemoveAtReorder( i );
					m_aioFreeFakeThreads[ priority -1 ].Set( threadID );
					break;
				}
			}

			// #tbd: if happens, could ignore and assume lost AioReadIn, but for now more likely something to investigate
			//RED_FATAL_ASSERT( threadID != m_aioFreeFakeThreads[ priority - 1 ].Size() );
			if ( threadID == m_aioFreeFakeThreads[ priority - 1 ].Size() )
			{
				return;
			}

			m_writer.StartObject();
			{
				m_writer.Key( "pid" ); m_writer.Uint( priority );
				m_writer.Key( "tid" ); m_writer.Uint( threadID );
				m_writer.Key( "ts" ); m_writer.Uint64( GetEventTimeInMicroseconds( event.timestamp, m_resultInfo.traceStartedTimestamp, m_freq ) );
				m_writer.Key( "ph" ); m_writer.String( "E" );
				m_writer.Key( "cat" ); m_writer.String( "aio,read" );
				m_writer.Key( "name" ); m_writer.String( GetEventName( logicalFileName ) );

				m_writer.Key( "args" );
				m_writer.StartObject();
				{
					m_writer.Key( "logicalFileName" ); m_writer.String( logicalFileName ? logicalFileName->AsChar() : "<UNKNOWN>" );
					char fmtBuf[ 64 ];
					red::SNPrintFUnsafe( fmtBuf, RED_ARRAY_COUNT_U32( fmtBuf ), "reqID=0x%llX,syscallID=0x%llX", event.submitUniqueID.requestID, event.submitUniqueID.submitSyscallUniqueID );
					m_writer.Key( "aioID" ); m_writer.String( fmtBuf );
				}
				m_writer.EndObject();
			}
			m_writer.EndObject();
		}

		RapidJsonWriter& m_writer;
		red::Map< SubmitUniqueID, AioSubmitCmdInEvent > m_submitEvents{ red::PoolDebug() };
		const red::Map< FileOffsetSize, String >& m_logicalFileNameMap;
		Profiler::ResultInfo m_resultInfo;
		Uint64 m_freq;

		red::StaticArray< red::BitSet64< 64 >, 3 > m_aioFreeFakeThreads;

		struct FakeThreadInfo
		{
			SubmitUniqueID submitUniqueID{};
			Uint32 threadID{ 0 };
		};
		red::StaticArray< red::StaticArray< FakeThreadInfo, 64 >, 3 > m_aioFakeThread;
	};

	void TraceWriter::RegisterLogicalFile( Uint32 fileID, Uint64 offset, Uint32 size, const char* logicalFileName )
	{
		RED_SCOPE_LOCK( m_logicalFileNameMapLock );
		FileOffsetSize key;
		key.fileID = fileID;
		key.offset = offset;
		key.size = size;
		(void)m_logicalFileNameMap.Insert( key, logicalFileName );
	}

	void TraceWriter::StopTrace( Profiler& profiler )
	{
		RED_SCOPE_LOCK( m_logicalFileNameMapLock );

		const auto resultInfo = profiler.StopTrace();
		if ( !resultInfo.validTrace )
		{
			RED_LOG_WARNING( "No io trace available" );
			return;
		}

		char filePath[ 1024 ] = "/hostapp/ioprofiler_";

		SceRtcTick tick;
		sceRtcGetCurrentTick( &tick );
		sceRtcFormatRFC3339LocalTime( filePath + red::Strlen( filePath ), &tick );

		for ( char* ch = filePath; *ch; ++ch )
		{
			if ( *ch == ':' || *ch == '+' || *ch == '.' )
			{
				*ch = '_';
			}
		}

		red::Strcat( filePath, ".json", RED_ARRAY_COUNT_U32( filePath ) );	

		FILE* file = nullptr;
		if ( fopen_s( &file, filePath, "w" ) == 0 )
		{
			JsonFileWriter textWriter{ file };
			RapidJsonWriter writer{ textWriter };

			ProfilerVisitorJSONWriter jsonWriter{ writer, resultInfo, m_logicalFileNameMap };
			{
				jsonWriter.Start();
				profiler.VisitTrace( jsonWriter );
				jsonWriter.Finish();
			}
			fclose( file );
		}
	}

	const String* TraceWriter::FindLogicalFile_NoLock( Uint32 fileID, Uint64 offset, Uint32 size )
	{
		FileOffsetSize asyncOpKey;
		{
			asyncOpKey.fileID = fileID;
			asyncOpKey.offset = offset;
			asyncOpKey.size = size;
		}

		m_logicalFileNameMap.MakeClean();
		const auto& keys = m_logicalFileNameMap.Keys();
		const auto& values = m_logicalFileNameMap.Values();
		auto predicate = std::less< FileOffsetSize >();

		auto fileRangeMatch = [&asyncOpKey]( const FileOffsetSize& fileKey ) -> Bool {
			if ( fileKey.fileID != asyncOpKey.fileID )
			{
				return false;
			}

			if ( ( asyncOpKey.offset >= fileKey.offset ) &&
				( asyncOpKey.offset + asyncOpKey.size <= fileKey.offset + fileKey.size ) )
			{
				return true;
			}

			return false;
		};

		// Find first element not less than this key, so can return either an exact match
		// or the next file above, so have to iterate backwards at most one time
		auto it = std::lower_bound( keys.begin(), keys.end(), asyncOpKey, predicate );
		auto itBegin = keys.begin();
		auto itEnd = keys.end();

		if ( ( it == itEnd ) || !fileRangeMatch( *it ) )
		{
			if ( it == itBegin )
			{
				return nullptr;
			}

			--it;

			if ( !fileRangeMatch( *it ) )
			{
				return nullptr;
			}
		}

		const Uint32 index = static_cast<Uint32>( it - itBegin );
		return &values[ index ];
	}

} // orbis
} // io

#else
RED_NO_EMPTY_FILE()
#endif // RED_PLATFORM_ORBIS
