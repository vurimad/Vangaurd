/**
* Copyright (c) 2017 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"

#ifdef RED_MONITOR_SCRIPTABLE_THREAD_SAFETY

#include "scriptableThreadSafetyMonitorTypes.h"

#ifdef DEBUG_MONITOR_SCRIPTABLE_THREAD_SAFETY
	#pragma optimize("", off)
#endif

namespace debug
{
namespace ScriptableThreadSafetyMonitor
{

static const char ReportDepotDir[] = "\\\\redmatrix\\TEAM-PROJECT-DEV\\CYBERPUNK 2077\\PROGRAMMING\\SCRIPTS\\THREAD_SAFETY_REPORTS\\USER_REPORTS";
static const char PropsReportDepotDir[] = "\\\\redmatrix\\TEAM-PROJECT-DEV\\CYBERPUNK 2077\\PROGRAMMING\\SCRIPTS\\THREAD_SAFETY_REPORTS\\USER_REPORTS_PROPS";
static const char CrossEntityReportDepotDir[] = "\\\\redmatrix\\TEAM-PROJECT-DEV\\CYBERPUNK 2077\\PROGRAMMING\\SCRIPTS\\THREAD_SAFETY_REPORTS\\USER_REPORTS_CROSS_ENTITY";

extern Data s_data;

namespace prv
{
namespace json
{
	static char indentation[ 128 ] = { '\t' };

	void BeginRoot( FILE* file )
	{
		fprintf( file, "{\n" );
	}

	void EndRoot( FILE* file )
	{
		fprintf( file, "}\n" );
	}

	void BeginObject( FILE* file, const Uint32 level, const char* name )
	{
		indentation[ level ] = '\0';
		fprintf( file, "%s\"%s\": {\n", indentation, name );
		indentation[ level ] = '\t';
	}

	void BeginObject( FILE* file, const Uint32 level )
	{
		indentation[ level ] = '\0';
		fprintf( file, "%s{\n", indentation );
		indentation[ level ] = '\t';
	}

	void EndObject( FILE* file, const Uint32 level )
	{
		indentation[ level ] = '\0';
		fprintf( file, "%s}\n", indentation );
		indentation[ level ] = '\t';
	}

	void BeginArray( FILE* file, const Uint32 level, const char* name )
	{
		indentation[ level ] = '\0';
		fprintf( file, "%s\"%s\": [\n", indentation, name );
		indentation[ level ] = '\t';
	}

	void EndArray( FILE* file, const Uint32 level )
	{
		indentation[ level ] = '\0';
		fprintf( file, "%s]\n", indentation );
		indentation[ level ] = '\t';
	}

	void BeginSingleLineArray( FILE* file, const Uint32 level, const char* name )
	{
		indentation[ level ] = '\0';
		fprintf( file, "%s\"%s\": [ ", indentation, name );
		indentation[ level ] = '\t';
	}

	void EndSingleLineArray( FILE* file )
	{
		fprintf( file, " ],\n" );
	}

	void AddValue( FILE* file, const Uint32 level, const char* name, const Uint32 value )
	{
		indentation[ level ] = '\0';
		fprintf( file, "%s\"%s\": %u,\n", indentation, name, value );
		indentation[ level ] = '\t';
	}

	void AddValue( FILE* file, const Uint32 level, const char* name, const Uint64 value )
	{
		indentation[ level ] = '\0';
		fprintf( file, "%s\"%s\": %llu,\n", indentation, name, value );
		indentation[ level ] = '\t';
	}

	void AddValue( FILE* file, const Uint32 level, const char* name, const Bool value )
	{
		indentation[ level ] = '\0';
		fprintf( file, "%s\"%s\": %s,\n", indentation, name, value ? "true" : "false" );
		indentation[ level ] = '\t';
	}

	void AddValue( FILE* file, const Uint32 level, const char* name, const char* value, const Bool isLast = false )
	{
		indentation[ level ] = '\0';
		fprintf( file, "%s\"%s\": \"%s\"%s\n", indentation, name, value, isLast ? "" : "," );
		indentation[ level ] = '\t';
	}

	void SeparateElements( FILE* file, const Uint32 level )
	{
		indentation[ level ] = '\0';
		fprintf( file, "%s,\n", indentation );
		indentation[ level ] = '\t';
	}

	void BeginValue( FILE* file, const Uint32 level, const char* name )
	{
		indentation[ level ] = '\0';
		fprintf( file, "%s\"%s\": \"", indentation, name );
		indentation[ level ] = '\t';
	}

	void AppendValue( FILE* file, const char* value )
	{
		fprintf( file, value );
	}

	void NewLineInValue( FILE* file )
	{
		fprintf( file, "\\n" );
	}

	void EndValue( FILE* file )
	{
		fprintf( file, "\",\n" );
	}
}
}

void TickStage::ToString( char* dst, const Uint32 size ) const
{
	if ( m_group != ( Uint32 ) UpdateTickGroup::COUNT )
	{
		red::Strcpy( dst, GetTickGroupName( ( UpdateTickGroup ) m_group ), size );
	}
	else if ( m_bucket != UpdateBucket::Enum::Count )
	{
		red::Strcpy( dst, GetTickBucketName( ( UpdateBucket::Enum ) m_bucket ), size );
		red::Strcat( dst, "-", size );
		red::Strcat( dst, GetTickPhaseName( ( UpdateBucketPhase ) m_phase ), size );
	}
	else
	{
		red::Strcpy( dst, "<none>", size );
	}
}

void TickStage::BitmaskToJSONValues( FILE* file, const BitMaskType bitmask )
{
	Bool isFirst = true;
	char buffer[ 128 ];
	for ( Uint32 i = 0; i < 64; ++i )
	{
		if ( bitmask & ( ( BitMaskType ) 1 << i ) )
		{
			Uint32 index = i;

			TickStage tickStage;
			tickStage.Init();
			if ( index == 0 )
			{
				goto TickStageDone;
			}
			--index;
			if ( index < ( Uint32 ) UpdateTickGroup::COUNT )
			{
				tickStage.m_group = index;
			}
			else
			{
				index -= ( Uint32 ) UpdateTickGroup::COUNT;
				tickStage.m_bucket = index / ( Uint32 ) UpdateBucketPhase::COUNT;
				tickStage.m_phase = index % ( Uint32 ) UpdateBucketPhase::COUNT;
			}
			
		TickStageDone:
			if ( isFirst )
			{
				isFirst = false;
			}
			else
			{
				fprintf( file, ", " );
			}
			fprintf( file, "\"" );
			tickStage.ToString( buffer, RED_ARRAY_COUNT_U32( buffer ) );
			fprintf( file, buffer );
			fprintf(file, "\"");
		}
	}
}

void ScriptableStoredAccessInfo::CallstackToJSON( FILE* file, const Uint32 indentation, const char* elementName ) const
{
	prv::json::BeginValue( file, indentation, elementName );
	for ( Uint32 i = 0; i < m_callstackSize; ++i )
	{
		const CallstackEntry& entry = GetCallstack()[ i ];
		if ( i )
		{
			prv::json::NewLineInValue( file );
		}
		prv::json::AppendValue( file, entry.m_contextName.AsChar() );
		prv::json::AppendValue( file, " -> " );
		prv::json::AppendValue( file, entry.m_memberName.AsChar() );
	}
	prv::json::EndValue( file );
}

void ScriptableConflict::ToJSON( FILE* file, const ScriptableConflictStats& stats ) const
{
	prv::json::BeginObject( file, 2 );
		prv::json::AddValue( file, 3, "class", m_exclusiveAccess.m_class->GetName().AsChar() );
		if ( s_data.m_captureMode == CaptureMode::PropertyAccesses )
		{
			prv::json::AddValue( file, 3, "property", m_exclusiveAccess.m_property->GetName().AsChar() );
		}
		prv::json::AddValue( file, 3, "occurrences", stats.m_numOccurrences );
		prv::json::AddValue( file, 3, "overlapping", stats.m_overlappingOccurencesDetected ? "yes" : "no" );
		prv::json::BeginSingleLineArray( file, 3, "tick-stages" );
			TickStage::BitmaskToJSONValues( file, stats.m_tickStagesFlag );
		prv::json::EndSingleLineArray( file );
		m_exclusiveAccess.CallstackToJSON( file, 3, "thread-1-callstack" );
		prv::json::AddValue( file, 3, "thread-1-access-type", "exclusive" );
		m_accessFromAnotherThread.CallstackToJSON( file, 3, "thread-2-callstack" );
		prv::json::AddValue( file, 3, "thread-2-access-type", m_accessFromAnotherThread.m_isShared ? "shared" : "exclusive", true );
	prv::json::EndObject( file, 2 );
}

void ScriptableStoredAccessInfo::ToJSON( FILE* file, const ScriptableCrossEntityAccessViaEventStats& stats ) const
{
	const CallstackEntry* callstack = GetCallstack();

	prv::json::BeginObject( file, 2 );
		prv::json::AddValue( file, 3, "source entity or component class", callstack[ 0 ].m_contextName.AsChar() );
		prv::json::AddValue( file, 3, "source entity or component event", callstack[ 0 ].m_memberName.AsChar() );
		prv::json::AddValue( file, 3, "destination entity or component class", m_class->GetName().AsChar() );
		prv::json::AddValue( file, 3, "property accessed", m_property->GetName().AsChar() );
		prv::json::AddValue( file, 3, "occurrences", stats.m_numOccurrences );
		CallstackToJSON( file, 3, "callstack" );
	prv::json::EndObject( file, 2 );
}

void DumpReportToJSON( FILE* file )
{
	// Make sure data isn't updated now

	for ( Uint32 i = 0; i < MaxThreads; ++i )
	{
		s_data.m_frameCaptureLocks[ i ].Acquire();
	}

	// Build header

	prv::json::BeginRoot( file );
	switch ( s_data.m_captureMode )
	{
		case CaptureMode::PropertyAccesses:
		case CaptureMode::ObjectAccesses:
		{
			prv::json::AddValue( file, 1, "number-of-conflicts", s_data.m_conflicts.Size() );
			prv::json::BeginArray( file, 1, "conflicts" );
			{
				for ( auto it = s_data.m_conflicts.Begin(); it != s_data.m_conflicts.End(); ++it )
				{
					if ( it != s_data.m_conflicts.Begin() )
					{
						prv::json::SeparateElements( file, 2 );
					}
					const ScriptableConflict& conflict = it.Key();
					const ScriptableConflictStats& conflictStats = it.Value();
					conflict.ToJSON( file, conflictStats );
				}
			}
			prv::json::EndArray( file, 1 );
			break;
		}
		case CaptureMode::CrossEntityAccessViaEvent:
		{
			prv::json::AddValue( file, 1, "number-of-forbidden-cross-entity-accesses", s_data.m_conflicts.Size() );
			prv::json::BeginArray( file, 1, "accesses" );
			{
				for ( auto it = s_data.m_crossEntityAccessesViaEvents.Begin(); it != s_data.m_crossEntityAccessesViaEvents.End(); ++it )
				{
					if ( it != s_data.m_crossEntityAccessesViaEvents.Begin() )
					{
						prv::json::SeparateElements( file, 2 );
					}
					const ScriptableStoredAccessInfo& access = it.Key();
					const ScriptableCrossEntityAccessViaEventStats& stats = it.Value();
					access.ToJSON( file, stats );
				}
			}
			prv::json::EndArray( file, 1 );
			break;
		}
	}
	prv::json::EndRoot( file );

	// Release locks

	for ( Uint32 i = 0; i < MaxThreads; ++i )
	{
		s_data.m_frameCaptureLocks[ i ].Release();
	}
}

Bool IsAnythingToReport()
{
	Bool isAnythingToReport = false;

	for ( Uint32 i = 0; i < MaxThreads; ++i )
	{
		s_data.m_frameCaptureLocks[ i ].Acquire();
	}

	switch ( s_data.m_captureMode )
	{
	case CaptureMode::ObjectAccesses:
	case CaptureMode::PropertyAccesses:
		isAnythingToReport = !s_data.m_conflicts.Empty();
		break;
	case CaptureMode::CrossEntityAccessViaEvent:
		isAnythingToReport = !s_data.m_crossEntityAccessesViaEvents.Empty();
		break;
	}

	for ( Uint32 i = 0; i < MaxThreads; ++i )
	{
		s_data.m_frameCaptureLocks[ i ].Release();
	}

	return isAnythingToReport;
}

void DumpMonitoringReportToSingleFile()
{
#ifdef RED_PLATFORM_WINPC // PC only support

	// Construct output file name

	SYSTEMTIME systemTime;
	GetSystemTime( &systemTime );

	char userName[ 128 ];
	DWORD userNameLength = RED_ARRAY_COUNT_U32( userName );
	GetUserNameA( userName, &userNameLength );
	for ( Uint32 i = 0; userName[ i ]; ++i )
	{
		if ( userName[ i ] == ':' || userName[ i ] == ' ' )
		{
			userName[ i ] = '_';
		}
	}

	char fileName[ MAX_PATH ];
	red::SNPrintFSafe(
		fileName, RED_ARRAY_COUNT_U32( fileName ),
		"script_thread_safety_report_%u_%u_%u_%u_%u_%u_%u_%s.json",
		systemTime.wYear, systemTime.wMonth, systemTime.wDay, systemTime.wHour, systemTime.wMinute, systemTime.wSecond, systemTime.wMilliseconds,
		userName );

	// Write to file

	FILE* file = nullptr;
	if ( !fopen_s( &file, fileName, "wb" ) )
	{
		DumpReportToJSON( file );
		fclose( file );
		RED_LOG( "Scriptable thread-safety report data stored in %s", fileName );

		// When done try to make a copy to report depot

		const char* dir = nullptr;
		switch ( s_data.m_captureMode )
		{
		case CaptureMode::ObjectAccesses:
			dir = ReportDepotDir;
			break;
		case CaptureMode::PropertyAccesses:
			dir = PropsReportDepotDir;
			break;
		case CaptureMode::CrossEntityAccessViaEvent:
			dir = CrossEntityReportDepotDir;
			break;
		}

		char reportDepotDayDir[ MAX_PATH ];
		red::SNPrintFSafe( reportDepotDayDir, RED_ARRAY_COUNT_U32( reportDepotDayDir ), "%s\\%u_%u_%u",
			dir,
			systemTime.wYear, systemTime.wMonth, systemTime.wDay );
		CreateDirectoryA( reportDepotDayDir, NULL );

		char reportDepotFilePath[ MAX_PATH ];
		red::SNPrintFSafe( reportDepotFilePath, RED_ARRAY_COUNT_U32( reportDepotFilePath ), "%s\\%s", reportDepotDayDir, fileName );
		const BOOL ret = CopyFileA( fileName, reportDepotFilePath, TRUE );
		if ( ret )
		{
			RED_LOG( "Scriptable thread-safety report data copied to %s", reportDepotDayDir );
		}
		else
		{
			RED_LOG_ERROR( "Failed to copy scriptable thread-safety report to %s", reportDepotDayDir );
		}
	}
	else
	{
		RED_LOG_ERROR( "Failed to store scriptable thread-safety report in %s, reason: failed to open file", fileName );
	}
#endif
}

void ScriptableStoredAccessInfo::DumpToFile( const ScriptableCrossEntityAccessViaEventStats& stats ) const
{
	RED_ASSERT( s_data.m_captureMode == CaptureMode::CrossEntityAccessViaEvent );

#ifdef RED_PLATFORM_WINPC // PC only support

	// Prepare output file name

	const CallstackEntry* callstack = GetCallstack();
	char reportDepotFilePath[ MAX_PATH ];
	red::SNPrintFSafe( reportDepotFilePath, RED_ARRAY_COUNT_U32( reportDepotFilePath ), "%s\\MERGED\\%s_%s_%s_%s.json",
		CrossEntityReportDepotDir,
		callstack[ 0 ].m_contextName.AsChar(), callstack[ 0 ].m_memberName.AsChar(),
		m_class->GetName().AsChar(), m_property->GetName().AsChar() );

	RED_LOG_INFO( "Dumping cross-entity thread-safety issue to '%s'...", reportDepotFilePath );

	// Store in local file first

	const char* tempFileName = "temp_thread_safety_issue_file.json";
	FILE* file = nullptr;
	if ( fopen_s( &file, tempFileName, "wb" ) )
	{
		RED_LOG_ERROR( "Failed to open scriptable thread-safety file %s for writing", tempFileName );
		return;
	}
	ToJSON( file, stats );
	fclose( file );

	// Successfully stored locally - now copy the file over to shared depot (and rename it to some readable name)

	const BOOL ret = CopyFileA( tempFileName, reportDepotFilePath, FALSE );
	if ( !ret )
	{
		RED_LOG_ERROR( "Failed to copy scriptable thread-safety per-issue file to %s", reportDepotFilePath );
	}
#endif
}

void DumpMonitoringReportToFilePerIssue()
{
	if ( s_data.m_captureMode != CaptureMode::CrossEntityAccessViaEvent )
	{
		return; // Only cross-entity accesses get reported in this mode for now
	}

#ifdef RED_PLATFORM_WINPC // PC only support

	for ( Uint32 i = 0; i < MaxThreads; ++i )
	{
		s_data.m_frameCaptureLocks[ i ].Acquire();
	}

	for ( auto it : s_data.m_crossEntityAccessesViaEvents )
	{
		const ScriptableStoredAccessInfo& access = it.Key();
		const ScriptableCrossEntityAccessViaEventStats& stats = it.Value();

		access.DumpToFile( stats );
	}

	for ( Uint32 i = 0; i < MaxThreads; ++i )
	{
		s_data.m_frameCaptureLocks[ i ].Release();
	}

	RED_LOG( "Scriptable thread-safety report data stored in %s\\MERGED", CrossEntityReportDepotDir );
#endif
}

void DumpMonitoringReportToFile()
{
	if ( !IsAnythingToReport() )
	{
		return;
	}

	DumpMonitoringReportToSingleFile();
	DumpMonitoringReportToFilePerIssue();
}

} // namespace ScriptableThreadSafetyMonitor
} // namespace debug

#ifdef DEBUG_MONITOR_SCRIPTABLE_THREAD_SAFETY
	#pragma optimize("", on)
#endif

#endif // RED_MONITOR_SCRIPTABLE_THREAD_SAFETY