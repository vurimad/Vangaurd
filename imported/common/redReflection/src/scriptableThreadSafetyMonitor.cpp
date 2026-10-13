/**
* Copyright (c) 2017 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"

#ifdef RED_MONITOR_SCRIPTABLE_THREAD_SAFETY

#include "scriptableThreadSafetyMonitorTypes.h"
#include "../../redFileSystem/include/fileSys.h"
#include "../../redCore/include/globalModeInfo.h"

#ifdef DEBUG_MONITOR_SCRIPTABLE_THREAD_SAFETY
	#pragma optimize("", off)
#endif

namespace debug
{
namespace ScriptableThreadSafetyMonitor
{

Data s_data;
thread_local ThreadLocalData s_tlData;

const CallstackEntry* ScriptableStoredAccessInfo::GetCallstack() const
{
	return m_tempCallstackPtr ? m_tempCallstackPtr : &s_data.m_conflictsCallstackBuffer[ m_callstackStart ];
}

Uint32 ScriptableStoredAccessInfo::CalcHash() const
{
	Uint32 hash = red::CalculateHash32FromUint32Array( reinterpret_cast< const Uint32* >( GetCallstack() ), m_callstackSize * sizeof( CallstackEntry ) / sizeof( Uint32 ) );
	hash = red::CombineHashes32( hash, red::CalculatePtrHash32( m_class ) );
	hash = red::CombineHashes32( hash, red::CalculatePtrHash32( m_property ) );
	hash = red::CombineHashes32( hash, m_callstackSize );
	return hash;
}

void ScriptableStoredAccessInfo::AllocateCallstack()
{
	RED_ASSERT( m_tempCallstackPtr );

	m_callstackStart = s_data.m_conflictsCallstackBuffer.Size();
	s_data.m_conflictsCallstackBuffer.Grow( m_callstackSize );
	red::Memcpy( &s_data.m_conflictsCallstackBuffer[ m_callstackStart ], m_tempCallstackPtr, m_callstackSize * sizeof( CallstackEntry ) );

	m_tempCallstackPtr = nullptr;
}

void ScriptableConflict::AllocateCallstacks()
{
	m_exclusiveAccess.AllocateCallstack();
	m_accessFromAnotherThread.AllocateCallstack();
}

void ResetStatisticsPerFrame( Statistics& statistics )
{
	statistics.m_numLastFrameCapturedAccesses =
	statistics.m_memoryUsage = 0;
}

void ResetStatistics( Statistics& statistics )
{
	ResetStatisticsPerFrame( statistics );
	statistics.m_totalCapturedCalls =
	statistics.m_totalFoundConflicts =
	statistics.m_totalFoundConflictOccurrences = 0;
}

Bool IsUserInGroup( const red::AbsolutePath& rootEnginePath, const char* groupFileName, const char* userName )
{
	const red::AbsolutePath groupFilePath = rootEnginePath.AddFilePath( groupFileName );
	red::String usersString;
	if ( red::LoadFileToString( groupFilePath, usersString ) && usersString.Contains( userName ) )
	{
		RED_LOG_INFO( "Current user %s is in script thread-safety reporting group: automatic data collection and reporting enabled. Modify %s file to add/remove user.", userName, groupFilePath.AsChar() );
		return true;
	}
	return false;
}

Bool IncludeAllUsers( const char* mode )
{
#ifdef RED_PLATFORM_WINPC
	static const char EnableReportFromAllUsersFileDir[] = "\\\\redmatrix\\TEAM-PROJECT-DEV\\CYBERPUNK 2077\\PROGRAMMING\\SCRIPTS\\THREAD_SAFETY_REPORTS";
	char path[ MAX_PATH ];
	red::SNPrintFSafe( path, RED_ARRAY_COUNT_U32( path ), "%s\\enable_reporting_for_all_users_%s", EnableReportFromAllUsersFileDir, mode );

	const DWORD dwAttrib = GetFileAttributesA( path );
	return
		dwAttrib != INVALID_FILE_ATTRIBUTES && 
        !( dwAttrib & FILE_ATTRIBUTE_DIRECTORY );
#else
	return false;
#endif
}

void Initialize( const Bool _enableAtStartup, const red::AbsolutePath& rootEnginePath )
{
	Bool enableAtStartup = _enableAtStartup;

#ifdef RED_PLATFORM_WINPC
	char moduleFileName[ MAX_PATH ];
	GetModuleFileNameA( NULL, moduleFileName, MAX_PATH );
	if ( red::Strstr( moduleFileName, "launcher.exe" ) )
	{
		char userName[ 512 ];
		DWORD userNameLength = RED_ARRAY_COUNT_U32( userName );
		if ( GetUserNameA( userName, &userNameLength ) && red::Strlen( userName ) >= 3 )
		{
			if ( IsUserInGroup( rootEnginePath, "config\\debug_script_thread_safety_group_props.csv", userName ) )
			{
				s_data.m_isUserInReportingGroup = true;
				s_data.m_captureMode = CaptureMode::PropertyAccesses;
				enableAtStartup = true;
			}
			else if ( IsUserInGroup( rootEnginePath, "config\\debug_script_thread_safety_group_cross_entity.csv", userName ) || IncludeAllUsers( "cross_entity" ) )
			{
				s_data.m_isUserInReportingGroup = true;
				s_data.m_captureMode = CaptureMode::CrossEntityAccessViaEvent;
				enableAtStartup = true;
			}
			else if ( IsUserInGroup( rootEnginePath, "config\\debug_script_thread_safety_group.csv", userName ) )
			{
				s_data.m_isUserInReportingGroup = true;
				s_data.m_captureMode = CaptureMode::ObjectAccesses;
				enableAtStartup = true;
			}
		}
	}
#endif

	s_data.m_isMonitoringEnabledRequest = s_data.m_isMonitoringEnabled = enableAtStartup;

	ResetStatistics( s_data.m_previousStatistics );
	ResetStatistics( s_data.m_currentStatistics );
}

void Deinitialize()
{
	if ( s_data.m_isUserInReportingGroup )
	{
		DumpMonitoringReportToFile();
	}
}

void EnableMonitoring( const Bool enable )
{
	s_data.m_isMonitoringEnabledRequest = enable;
}

Bool IsMonitoringEnabled()
{
	return s_data.m_isMonitoringEnabledRequest;
}

// This is a hack
// If it becomes rule that we need a bunch of custom exclusion rules, then TODO: turn it into generic list of exclusions
Bool ShouldConflictBeReported( const ScriptableConflict& conflict )
{
	const CName leftMemberName = conflict.m_exclusiveAccess.GetCallstack()[ 0 ].m_memberName;
	const CName rightMemberName = conflict.m_accessFromAnotherThread.GetCallstack()[ 0 ].m_memberName;

	static const CName name_PersistentState_ApplyEditableProperties = RED_NAME_CONSTEXPR( "PersistentState::ApplyEditableProperties" );
	static const CName name_EntityNotificationType = RED_NAME_CONSTEXPR( "EntityNotificationType" );
	static const rtti::ClassType* eventClass = rtti::ITypeSystem::GetInstance().FindClass( RED_NAME_CONSTEXPR_NOREG( "redEvent" ) );

	switch ( s_data.m_captureMode )
	{
	case CaptureMode::ObjectAccesses:
		// Case description:
		// Engine has an extra sync point within single entity initialization: first OnRequestComponents(), then OnTakeControl() from inside of child job - hence access from 2 different threads
		if ( ( leftMemberName == RED_NAME_CONSTEXPR_NOREG( "OnRequestComponents" ) && rightMemberName == RED_NAME_CONSTEXPR_NOREG( "OnTakeControl" ) ) ||
			 ( leftMemberName == RED_NAME_CONSTEXPR_NOREG( "OnTakeControl" ) && rightMemberName == RED_NAME_CONSTEXPR_NOREG( "OnRequestComponents" ) ) )
		{
			return false;
		}
		break;

	case CaptureMode::PropertyAccesses:
	{
		// Case description:
		// Ignore all of the UI code issues for now which is 99% from some "Controller" class.
		// Why? Because they are most likely all (or almost all) false positives due to manual syncs inside of the tick groups that make accesses safe but which is difficult to auto-determine.
		if ( red::Strstr( conflict.m_exclusiveAccess.m_class->GetName().AsChar(), "Controller" ) )
		{
			return false;
		}

		// Case description:
		// ApplyEditableProperties and HandlePSEvents() are synced using global lock (one for all persistent states)
		// UPDATE: Not anymore (CL was backed out): awaiting fix for CYB-220830
		if ( leftMemberName == name_PersistentState_ApplyEditableProperties )
		{
			if ( const rtti::ClassType* rightClass = rtti::ITypeSystem::GetInstance().FindClass( conflict.m_accessFromAnotherThread.GetCallstack()[ 0 ].m_contextName ) )
			{
				if ( const rtti::Function* rightFunction = rightClass->FindFunction( rightMemberName ) )
				{
					if ( const rtti::Property* rightFunctionReturnProperty = rightFunction->GetReturnValue() )
					{
						const rtti::IType* rightFunctionReturnType = rightFunctionReturnProperty->GetType();
						if ( rightFunctionReturnType->GetName() == name_EntityNotificationType )
						{
							return false;
						}
					}
				}
			}
		}

		// Case description:
		// Events are OK to be accessed from multiple threads in the same tick group
		// Typical scenario: event gets created by thread A and queued, then thread B dispatches it
		if ( conflict.m_exclusiveAccess.m_class->IsA( eventClass ) )
		{
			return false;
		}

		break;
	}
	}

	return true;
}

Bool ReportConflictingAccess( const ScriptableAccessInfo& exclusiveAccess, const Uint32 exclusiveAccessThreadID, const ScriptableAccessInfo& accessFromAnotherThread, const Uint32 anotherThreadID )
{
	RED_ASSERT( exclusiveAccess.m_class == accessFromAnotherThread.m_class );

	const CallstackEntry* exclusiveAccessCallstack = &s_data.m_frameCaptureCallstackBuffer[ exclusiveAccessThreadID ][ exclusiveAccess.m_callstackStart ];
	const CallstackEntry* accessFromAnotherThreadCallstack = &s_data.m_frameCaptureCallstackBuffer[ anotherThreadID ][ accessFromAnotherThread.m_callstackStart ];

	// Avoid symmetrical duplicates

	const Bool swapAccesses =
		exclusiveAccess.m_isShared == accessFromAnotherThread.m_isShared &&
		exclusiveAccess.m_callstackSize == accessFromAnotherThread.m_callstackSize &&
		red::Memcmp( exclusiveAccessCallstack, accessFromAnotherThreadCallstack, exclusiveAccess.m_callstackSize * sizeof( CallstackEntry ) ) < 0;

	// Create conflict description

	ScriptableConflict conflict;
	if ( swapAccesses )
	{
		conflict.m_exclusiveAccess.SetFrom( accessFromAnotherThread, accessFromAnotherThreadCallstack );
		conflict.m_accessFromAnotherThread.SetFrom( exclusiveAccess, exclusiveAccessCallstack );
	}
	else
	{
		conflict.m_exclusiveAccess.SetFrom( exclusiveAccess, exclusiveAccessCallstack );
		conflict.m_accessFromAnotherThread.SetFrom( accessFromAnotherThread, accessFromAnotherThreadCallstack );
	}

	// Filter out conflicts based on whatever custom game/engine specific conditions there are

	if ( !ShouldConflictBeReported( conflict ) )
	{
		return false;
	}

	// Allocate callstack in callstack buffer before insert (NOTE: if the key exists, it won't be "insert" but "lookup")

	if ( !s_data.m_conflicts.KeyExist( conflict ) )
	{
		conflict.AllocateCallstacks();
	}

	ScriptableConflictStats& stats = s_data.m_conflicts[ conflict ];
	++stats.m_numOccurrences;

	// Record when the conflict occurred

	stats.m_tickStagesFlag |= s_data.m_tickStage.ToFlag();

	// Detect overlap (overlap is even more dangerous)

	if ( !stats.m_overlappingOccurencesDetected && ScriptableAccessInfo::OverlapTest( exclusiveAccess, accessFromAnotherThread ) )
	{
		stats.m_overlappingOccurencesDetected = true;
	}

	// Update stats

	if ( stats.m_numOccurrences == 1 )
	{
		++s_data.m_currentStatistics.m_totalFoundConflicts;
	}
	++s_data.m_currentStatistics.m_totalFoundConflictOccurrences;

	return true;
}

void ReportCrossEntityAccessViaEvent( const ScriptableAccessInfo& access )
{
	// Insert (or query existing) access info in the global set of accesses

	const CallstackEntry* callstack = &s_data.m_frameCaptureCallstackBuffer[ access.m_threadID ][ access.m_callstackStart ];
	ScriptableStoredAccessInfo tempAccess;
	tempAccess.SetFrom( access, callstack );
	
	if ( !s_data.m_crossEntityAccessesViaEvents.KeyExist( tempAccess ) )
	{
		tempAccess.AllocateCallstack();
	}

	// Update stats

	ScriptableCrossEntityAccessViaEventStats& stats = s_data.m_crossEntityAccessesViaEvents[ tempAccess ];
	++stats.m_numOccurrences;

	// Dump to file immediately (but only if this is the first occurrence) to make sure game crash doesn't prevent us from reporting it

	if ( stats.m_numOccurrences == 1 )
	{
		tempAccess.DumpToFile( stats );
	}
}

Statistics GetStatistics()
{
	return s_data.m_previousStatistics;
}

void ProcessScriptable( const ScriptableAccessList& list )
{
	if ( s_data.m_captureMode == CaptureMode::CrossEntityAccessViaEvent )
	{
		const ScriptableAccessInfo* currentExclusiveAccess = list.m_firstExclusive;
		while ( currentExclusiveAccess )
		{
			ReportCrossEntityAccessViaEvent( *currentExclusiveAccess );
			currentExclusiveAccess = currentExclusiveAccess->m_next;
		}

		const ScriptableAccessInfo* currentSharedAccess = list.m_firstShared;
		while ( currentSharedAccess )
		{
			ReportCrossEntityAccessViaEvent( *currentSharedAccess );
			currentSharedAccess = currentSharedAccess->m_next;
		}

		return;
	}

	if ( !list.m_firstExclusive )
	{
		return; // This scriptable is safe - there were no exclusive accesses at all
	}
	const Uint32 exclusiveAccessThreadID = list.m_firstExclusive->m_threadID;

	// Make sure all other writes happen from the same thread

	const ScriptableAccessInfo* currentExclusiveAccess = list.m_firstExclusive->m_next;
	while ( currentExclusiveAccess )
	{
		if ( exclusiveAccessThreadID != currentExclusiveAccess->m_threadID )
		{
			ReportConflictingAccess( *list.m_firstExclusive, exclusiveAccessThreadID, *currentExclusiveAccess, currentExclusiveAccess->m_threadID );
		}

		currentExclusiveAccess = currentExclusiveAccess->m_next;
	}

	// Make sure all reads also happen from the same thread

	const ScriptableAccessInfo* currentSharedAccess = list.m_firstShared;
	while ( currentSharedAccess )
	{
		if ( exclusiveAccessThreadID != currentSharedAccess->m_threadID )
		{
			ReportConflictingAccess( *list.m_firstExclusive, exclusiveAccessThreadID, *currentSharedAccess, currentSharedAccess->m_threadID );
		}

		currentSharedAccess = currentSharedAccess->m_next;
	}

	// Note: we skip exclusive access conflicts between any non-first exclusive access and
	// (a) any other later exclusive access or
	// (b) any other shared access
	// This is because the total number of conflicts can potentially be high (think O(n^2) wrt. number of accesses)
}

void ProcessData()
{
	PC_SCOPE( ScriptableThreadSafetyMonitor_ProcessData );

	// Group accesses by scriptables within group

	Uint32 perFrameMemoryUsage = 0;
	s_data.m_scriptableAccessLists.Clear();
	for ( Uint32 i = 0; i < MaxThreads; ++i )
	{
		s_data.m_frameCaptureLocks[ i ].Acquire();
		for ( ScriptableAccessInfo& info : s_data.m_frameCapture[ i ] )
		{
			const ScriptableAccessKey key { info.m_ID, s_data.m_captureMode == CaptureMode::ObjectAccesses ? static_cast< const void* >( info.m_class ) : static_cast< const void* >( info.m_property ) };
			ScriptableAccessList& list = s_data.m_scriptableAccessLists[ key ];
			const ScriptableAccessInfo*& first = info.m_isShared ? list.m_firstShared : list.m_firstExclusive;
			info.m_next = first;
			first = &info;
		}
		s_data.m_currentStatistics.m_numLastFrameCapturedAccesses += s_data.m_frameCapture[ i ].Size();
		perFrameMemoryUsage += s_data.m_frameCapture[ i ].Capacity() * sizeof( ScriptableAccessInfo );
		perFrameMemoryUsage += s_data.m_frameCaptureCallstackBuffer[ i ].Capacity() * sizeof( CallstackEntry );
	}

	// Determine potentially dangerous accesses

	for ( auto it = s_data.m_scriptableAccessLists.Begin(); it != s_data.m_scriptableAccessLists.End(); ++it )
	{
		ProcessScriptable( it.Value() );
	}

	// Update stats

	s_data.m_currentStatistics.m_totalCapturedCalls += s_data.m_currentStatistics.m_numLastFrameCapturedAccesses;
	s_data.m_currentStatistics.m_memoryUsage =
		math::Max( s_data.m_currentStatistics.m_memoryUsage, perFrameMemoryUsage ) +
		s_data.m_conflicts.Capacity() * sizeof( ScriptableConflict );

	// Clear data to prepare for next frame

	for ( Uint32 i = 0; i < MaxThreads; ++i )
	{
		s_data.m_frameCapture[ i ].Clear();
		s_data.m_frameCaptureCallstackBuffer[ i ].Clear();
		s_data.m_frameCaptureLocks[ i ].Release();
	}

	// Delayed toggling of monitoring

	s_data.m_isMonitoringEnabled = s_data.m_isMonitoringEnabledRequest;
}

void OnBeginFrame()
{
	const Double currentSecs = red::Clock::GetInstance().GetTimer().GetSeconds();
	if ( currentSecs - s_data.m_lastStatisticsUpdateSecs > 1.0f )
	{
		s_data.m_lastStatisticsUpdateSecs = currentSecs;
		s_data.m_previousStatistics = s_data.m_currentStatistics;
	}
	ResetStatisticsPerFrame( s_data.m_currentStatistics );
}

void OnBeginTickGroup( const Uint32 tickGroup )
{
	if ( tickGroup == 0 )
	{
		OnBeginFrame();
	}
	s_data.m_tickStage.m_group = tickGroup;
}

void OnEndTickGroup()
{
	ProcessData();
	s_data.m_tickStage.m_group = ( Uint32 ) UpdateTickGroup::COUNT;
}

void OnBeginTickBucket( const Uint32 bucketIndex )
{
	s_data.m_tickStage.m_bucket = bucketIndex;
}

void OnEndTickBucket()
{
	s_data.m_tickStage.m_bucket = ( Uint32 ) UpdateBucket::Enum::Count;
}

void OnBeginTickPhase( const Uint32 phaseIndex )
{
	s_data.m_tickStage.m_phase = phaseIndex;
}

void OnEndTickPhase()
{
	ProcessData();
	s_data.m_tickStage.m_phase = ( Uint32 ) UpdateBucketPhase::COUNT;
}

// We compress thread indices so that they start from 0 because we use them to index into an array
static Uint32 GetThreadIndexCompressed()
{
	static red::Atomic< Uint32 > counter( 0 );
	static thread_local Uint32 thisThreadIndex = counter.ExchangeAdd( 1 );
	RED_ASSERT( thisThreadIndex < MaxThreads );
	return thisThreadIndex;
}

void PushCallstack( const IScriptable* context, const CName contextName, const CName functionName, const rtti::Function* function, const Bool isShared, const Bool requiresLock )
{
	if ( !s_data.m_isMonitoringEnabled )
	{
		return;
	}

	// Push callstack entry

	RED_ASSERT( s_tlData.m_size < MaxCallstackSize );

	s_tlData.m_callstack[ s_tlData.m_size ] = { contextName, functionName };
	s_tlData.m_callstackEx[ s_tlData.m_size ] = { context, function, red::Clock::GetInstance().GetTimer().GetTicks(), isShared, requiresLock };
	++s_tlData.m_size;
}

void PushCallstack( const IScriptable* context, const CName functionName, const Bool isShared )
{
	PushCallstack(
		context,
		context ? context->GetClass()->GetName() : CName::NONE(),
		functionName,
		nullptr,
		isShared,
		!context->DEBUG_IsScriptableThreadSafe() );
}

void PushCallstack( const IScriptable* context, const rtti::Function* function )
{
	PushCallstack(
		context,
		function->GetClass() ? function->GetClass()->GetName() : CName::NONE(),
		function->GetName(),
		function,
		function->IsConst(),
		!function->IsStatic() && !function->IsNative() && !function->IsThreadSafe() && !context->DEBUG_IsScriptableThreadSafe() );
}

void PopCallstack()
{
	if ( !s_data.m_isMonitoringEnabled )
	{
		return;
	}

	if ( s_data.m_captureMode == CaptureMode::ObjectAccesses )
	{
		const Uint64 currentTicks = red::Clock::GetInstance().GetTimer().GetTicks();
		RED_ASSERT( s_tlData.m_size );

		// Record scriptable access

		const ExtendedCallstackEntry& entryEx = s_tlData.m_callstackEx[ s_tlData.m_size - 1 ];
		if ( entryEx.m_requiresLock )
		{
			const IScriptable* context = entryEx.m_context;
			const Bool isShared = entryEx.m_isShared;

			// Check if access is redundant (e.g. shared function call within another shared function call)
			// This is to minimize amount of data collected (while still not loosing any important data)

			Bool isRedundant = false;
			for ( Int32 i = ( Int32 ) s_tlData.m_size - 2; i >= 0; --i )
			{
				if ( s_tlData.m_callstackEx[ i ].m_context == context &&
					 s_tlData.m_callstackEx[ i ].m_isShared == isShared )
				{
					isRedundant = true;
					break;
				}
			}

			// Enqueue access

			if ( !isRedundant )
			{
				const Uint32 thisThreadID = GetThreadIndexCompressed();
				RED_ASSERT( thisThreadID < MaxThreads );

				// Create new access info

				ScriptableAccessInfo info;
				info.m_ID = context ? context->GetID() : SerializableID();
				info.m_class = context ? context->GetClass() : nullptr;
				info.m_isShared = isShared ? 1 : 0;
				info.m_callstackSize = s_tlData.m_size;
				info.m_threadID = thisThreadID;
				info.m_startTicks = entryEx.m_startTime;
				info.m_endTicks = currentTicks;

				// Lock shared data

				red::ScopedLock< red::SpinLock > lock( s_data.m_frameCaptureLocks[ thisThreadID ] );
				{
					// Allocate callstack

					info.m_callstackStart = s_data.m_frameCaptureCallstackBuffer[ thisThreadID ].Size();
					s_data.m_frameCaptureCallstackBuffer[ thisThreadID ].Grow( s_tlData.m_size );
					red::Memcpy( &s_data.m_frameCaptureCallstackBuffer[ thisThreadID ][ info.m_callstackStart ], s_tlData.m_callstack, s_tlData.m_size * sizeof( CallstackEntry ) );

					// Add access info

					s_data.m_frameCapture[ thisThreadID ].PushBack( info );
				}
			}
		}
	}

	// Pop callstack

	--s_tlData.m_size;
}

const IScriptable* GetParentEntity( const IScriptable* object )
{
	return object ? object->DEBUG_GetParentEntity() : nullptr;
}

void RecordPropertyAccess( const IScriptable* object, const rtti::Property* property, const Bool isShared )
{
	if ( !s_data.m_isMonitoringEnabled )
	{
		return;
	}

	if ( s_data.m_captureMode != CaptureMode::PropertyAccesses &&
		 s_data.m_captureMode != CaptureMode::CrossEntityAccessViaEvent )
	{
		return;
	}

	if ( !property )
	{
		return;
	}

	RED_ASSERT( object );
	RED_ASSERT( !property->IsInFunction() );
	RED_ASSERT( !property->GetParent()->IsScriptedStruct() );

	const Bool requiresLock = !object->DEBUG_IsScriptableThreadSafe();
	if ( !requiresLock )
	{
		return;
	}

	// In cross entity mode: filter out any other property access (this limits amount of data collected a lot)

	if ( s_data.m_captureMode == CaptureMode::CrossEntityAccessViaEvent )
	{
		if ( isShared )
		{
			// For now ignore shared accesses
			// We still need to resolve these but let's focus on exclusive accesses first
			return;
		}

		const Bool isFromEvent = s_tlData.m_callstackEx[ 0 ].m_function && s_tlData.m_callstackEx[ 0 ].m_function->IsEvent();
		if ( !isFromEvent )
		{
			return;
		}
		
		const IScriptable* srcEntity = GetParentEntity( s_tlData.m_callstackEx[ 0 ].m_context );
		if ( !srcEntity )
		{
			return;
		}

		const IScriptable* dstEntity = GetParentEntity( object );
		if ( !dstEntity ||
			 srcEntity == dstEntity )
		{
			return;
		}

		// Source and destination are both either entity or component and their parents differ
		// Note: here: parent of entity just means the same entity
	}

	const Uint32 thisThreadID = GetThreadIndexCompressed();
	RED_ASSERT( thisThreadID < MaxThreads );

	RED_ASSERT( s_tlData.m_size <= MaxCallstackSize );
	const Uint32 callstackSize = s_tlData.m_size + 1;

	// Create new access info

	ScriptableAccessInfo info;
	info.m_ID = object->GetID();
	info.m_class = object->GetClass();
	info.m_property = property;
	info.m_isShared = isShared;
	info.m_callstackSize = callstackSize;
	info.m_threadID = thisThreadID;
	info.m_startTicks =
	info.m_endTicks = red::Clock::GetInstance().GetTimer().GetTicks();

	// Update callstack

	s_tlData.m_callstack[ s_tlData.m_size ].m_contextName = property->GetParent()->GetName();
	s_tlData.m_callstack[ s_tlData.m_size ].m_memberName = property->GetName();

	// Lock shared data

	red::ScopedLock< red::SpinLock > lock( s_data.m_frameCaptureLocks[ thisThreadID ] );
	{
		// Allocate callstack

		info.m_callstackStart = s_data.m_frameCaptureCallstackBuffer[ thisThreadID ].Size();
		s_data.m_frameCaptureCallstackBuffer[ thisThreadID ].Grow( callstackSize );
		red::Memcpy( &s_data.m_frameCaptureCallstackBuffer[ thisThreadID ][ info.m_callstackStart ], s_tlData.m_callstack, callstackSize * sizeof( CallstackEntry ) );

		// Add access info

		s_data.m_frameCapture[ thisThreadID ].PushBack( info );
	}
}

void OnReadProperty( const IScriptable* object, const rtti::Property* property )
{
	RecordPropertyAccess( object, property, true );
}

void OnWriteProperty( const IScriptable* object, const rtti::Property* property )
{
	RecordPropertyAccess( object, property, false );
}

} // namespace ScriptableThreadSafetyMonitor
} // namespace debug

#ifdef DEBUG_MONITOR_SCRIPTABLE_THREAD_SAFETY
	#pragma optimize("", on)
#endif

#endif // RED_MONITOR_SCRIPTABLE_THREAD_SAFETY