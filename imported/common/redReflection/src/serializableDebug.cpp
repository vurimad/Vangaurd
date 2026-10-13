#include "build.h"
#include "serializableDebug.h"
#include "rttiSystem.h"

#ifdef RED_ENABLE_SERIALIZABLE_DEBUG

#include "../../../common/redMemory/include/reportUtils.h"
#include "../../../common/redCore/include/commandline.h"

namespace serializableDebug
{
	struct Data
	{
		Bool isEnabled = false;
		
		// Global lock for all serializables
		red::SpinLock lock;
		// Registry of all serializables; for each serializable we store its class
		red::HashMap< const ISerializable*, const rtti::ClassType* > serializables{ red::PoolDebug() };
		// Per class object count
		red::HashMap< const rtti::ClassType*, ClassStats > classStats{ red::PoolDebug() };
	};

	Data s_serializableData;

	void DumpRTTIToJsonFunction( FILE* );

	void Initialize()
	{
		Bool shouldEnable = false;

		const red::CommandLine& commandLine = red::CommandLine::Get();
		if( commandLine.HasOption( "debugObjects" ) )
		{
			shouldEnable = true;
		}

		if( commandLine.HasOption( "dumpRtti" ) )
		{
			shouldEnable = true;
			red::memory::SetDumpRTTIToJsonFunction( DumpRTTIToJsonFunction );
		}

		if( shouldEnable )
		{
			Enable();
		}
	}

	void Enable()
	{
		s_serializableData.isEnabled = true;

		RED_SCOPE_LOCK( s_serializableData.lock );
		s_serializableData.serializables.Clear();
		s_serializableData.classStats.Clear();
	}
	
	void Disable()
	{
		s_serializableData.isEnabled = false;
	}

	Bool IsEnabled()
	{
		return s_serializableData.isEnabled;
	}

	void Register( const ISerializable* serializable, const rtti::ClassType* cls )
	{
		if( !s_serializableData.isEnabled )
		{
			return;
		}

		RED_SCOPE_LOCK( s_serializableData.lock );
		s_serializableData.serializables.Insert( serializable, cls );
		++s_serializableData.classStats[ cls ].count;
	}

	void Unregister( const ISerializable* serializable )
	{
		if( !s_serializableData.isEnabled )
		{
			return;
		}

		RED_SCOPE_LOCK( s_serializableData.lock );
		auto it = s_serializableData.serializables.Find( serializable );
		if( it != s_serializableData.serializables.End() )
		{
			ClassStats& classStats = s_serializableData.classStats[ it.Value() ];
			--classStats.count;
			classStats.everDropped = true;

			s_serializableData.serializables.Remove( it );
		}
	}

	void CaptureStatistics( SerializableStats& stats )
	{
		if( !s_serializableData.isEnabled )
		{
			return;
		}

		PC_SCOPE_FUNC();
		RED_SCOPE_LOCK( s_serializableData.lock );
		stats.classStats = s_serializableData.classStats;
	}

	struct DumpEntry
	{
		const char* className;
		Uint32 count;
		Uint32 classSize;
		Uint32 memory;
		Bool neverDropped;
	};

	const red::DynArray< DumpEntry > CaptureEntriesForDump()
	{
		SerializableStats stats;
		CaptureStatistics( stats );

		red::DynArray< DumpEntry > dumpEntries{ red::PoolDebug() };
		dumpEntries.Reserve( stats.classStats.Size() );
		for( auto it : stats.classStats )
		{
			const rtti::ClassType* cls = it.Key();
			const ClassStats& classStats = it.Value();
			const Uint32 count = classStats.count;
			if( count > 0 )
			{
				const Uint32 classSize = cls->GetSize();
				const Uint32 memory = count * cls->GetSize();
				const Bool neverDropped = !classStats.everDropped;
				const DumpEntry entry{ cls->GetName().AsChar(), count, classSize, memory, neverDropped };
				dumpEntries.PushBack( entry );
			}
		}
		
		std::sort( dumpEntries.Begin(), dumpEntries.End(), []( const DumpEntry& a, const DumpEntry& b ) { return red::Strcmp( a.className, b.className ) < 0; } );

		return dumpEntries;
	}

	void DumpRTTIToJsonFunction( FILE* f )
	{
		const red::DynArray< DumpEntry > dumpEntries = CaptureEntriesForDump();
		Bool isFirst = true;
		for( const DumpEntry& entry : dumpEntries)
		{
			if( !isFirst )
			{
				std::fprintf( f, ",\n" );
			}
			fprintf( f, "{\"class\":\"%s\",\"size\":%u,\"count\":%u}", entry.className, entry.classSize, entry.count );
			std::fflush( f );
			isFirst = false;
		}
	}

	Bool DumpStatsToFile()
	{
		PC_SCOPE_FUNC();

		red::DateTime dateTime;
		red::Clock::GetInstance().GetLocalTime( dateTime );

		char dumpFileName[ 256 ];
		red::SNPrintFSafe( dumpFileName, RED_ARRAY_COUNT_U32( dumpFileName ), "objects_dump_%04u_%02u_%02u_%02u_%02u_%02u_%03u.csv",
			dateTime.GetYear(), dateTime.GetMonth() + 1, dateTime.GetDay() + 1,
			dateTime.GetHour(), dateTime.GetMinute(), dateTime.GetSecond(),
			dateTime.GetMilliSeconds() );

		FILE* f = nullptr;
#if defined( RED_PLATFORM_WINPC ) || defined( RED_PLATFORM_DURANGO )
		if( fopen_s( &f, dumpFileName, "w" ) )
#else
		f = fopen( dumpFileName, "w" );
		if( f )
#endif
		{
			RED_LOG_ERROR( "Failed to dump ISerializable capture to '%s', reason: failed to open file for writing", dumpFileName );
			return false;
		}

		fprintf( f, "class,count,class_size,memory,never_dropped\n" );

		const red::DynArray< DumpEntry > dumpEntries = CaptureEntriesForDump();
		for( const DumpEntry& entry : dumpEntries)
		{
			fprintf( f, "%s,%u,%u,%u,%s\n", entry.className, entry.count, entry.classSize, entry.memory, entry.neverDropped ? "YES" : "NO" );
		}

		fclose( f );

		RED_LOG_INFO( "ISerializable capture saved to '%s'" );
		return true;
	}

	void VisitObjects( FilterFunction&& filter, VisitorFunction&& visitor )
	{
		RED_SCOPE_LOCK( s_serializableData.lock );
		red::DynArray< const ISerializable* > serializables{ red::PoolDebug() };
		for( auto it : s_serializableData.serializables )
		{
			const ISerializable* serializable = it.Key();
			if( filter( *serializable ) )
			{
				serializables.PushBack( serializable );
			}
		}
		visitor( serializables );
	}
}

#else

	RED_NO_EMPTY_FILE()

#endif // RED_ENABLE_SERIALIZABLE_DEBUG
