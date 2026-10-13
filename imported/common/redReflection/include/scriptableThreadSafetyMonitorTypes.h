#pragma once

#ifdef RED_MONITOR_SCRIPTABLE_THREAD_SAFETY

#include "scriptable.h"
#include "scriptableThreadSafetyMonitor.h"

#include "../../engine/include/updateTickGroup.h"
#include "../../engine/include/updateTickBucket.h"

//#define DEBUG_MONITOR_SCRIPTABLE_THREAD_SAFETY

namespace debug
{
namespace ScriptableThreadSafetyMonitor
{

static constexpr Uint32 MaxThreads = 64;
static constexpr Uint32 MaxCallstackSize = 1024;

struct CallstackEntry
{
	CName m_contextName;
	CName m_memberName; // Function or property name (depends on capturing mode)

	RED_INLINE void operator = ( const CallstackEntry& other )
	{
		m_contextName = other.m_contextName;
		m_memberName = other.m_memberName;
	}
};

struct ScriptableBaseAccessInfo
{
	const rtti::ClassType* m_class;
	const rtti::Property* m_property;
	Uint32 m_callstackStart : 22; // Offset into callstack buffer (different buffer depending on stage)
	Uint32 m_callstackSize : 9;
	Uint32 m_isShared : 1;
};

struct TickStage
{
	typedef Uint64 BitMaskType;
	static constexpr Uint32 BitMaskTypeBits = sizeof( BitMaskType ) * 8;

	Uint16 m_group : 8;
	Uint16 m_bucket : 8;
	Uint16 m_phase : 16;

	static_assert( 1 + ( Uint32 ) UpdateTickGroup::COUNT + UpdateBucket::Enum::Count * ( Uint32 ) UpdateBucketPhase::COUNT <= BitMaskTypeBits, "Not enough bits in mask to encode all tick groups, buckets and phases." );

	RED_INLINE void Init()
	{
		m_group = ( Uint32 ) UpdateTickGroup::COUNT;
		m_bucket = ( Uint32 ) UpdateBucket::Enum::Count;
		m_phase = ( Uint32 ) UpdateBucketPhase::COUNT;
	}

	// Converts tick stage to unique flag
	RED_INLINE BitMaskType ToFlag() const
	{
		Uint32 index = 0;
		if ( m_group != ( Uint32 ) UpdateTickGroup::COUNT )
		{
			index += 1;
			index += m_group;
		}
		else if ( m_bucket != UpdateBucket::Enum::Count )
		{
			index += 1;
			index += ( Uint32 ) UpdateTickGroup::COUNT;
			index += m_bucket * ( Uint32 ) UpdateBucketPhase::COUNT;
			index += m_phase;
		}
		return ( BitMaskType ) 1 << index;
	}

	static void BitmaskToJSONValues( FILE* file, const BitMaskType bitmask );

private:

	void ToString( char* dst, const Uint32 size ) const;
	RED_INLINE static const char* GetTickGroupName( UpdateTickGroup group )
	{
		switch( group )
		{
			case UpdateTickGroup::FrameBegin                       : return "FrameBegin";
			case UpdateTickGroup::Multiplayer_UpdateStateSnapshots : return "Multiplayer_UpdateStateSnapshots";
			case UpdateTickGroup::EntityUpdateState                : return "EntityUpdateState";
			case UpdateTickGroup::PreBuckets                       : return "PreBuckets";
			case UpdateTickGroup::Internal_Buckets				   : return "Buckets";
			case UpdateTickGroup::PostBuckets                      : return "PostBuckets";
			case UpdateTickGroup::CameraUpdate                     : return "CameraUpdate";
			case UpdateTickGroup::PlayerAimUpdate                  : return "PlayerAimUpdate";
			case UpdateTickGroup::PostPlayerAimUpdate              : return "PostPlayerAimUpdate";
			case UpdateTickGroup::MappinsUpdate                    : return "MappinsUpdate";
			case UpdateTickGroup::BlackboardCallbacks_SecondPass   : return "BlackboardCallbacks_SecondPass";
			case UpdateTickGroup::PreRenderUpdate                  : return "PreRenderUpdate";
			case UpdateTickGroup::Multiplayer_CaptureStateSnapshots: return "Multiplayer_CaptureStateSnapshots";
		}

		return "<none-tick-group>";
	}

	RED_INLINE static const char* GetTickBucketName( const UpdateBucket::Enum bucket )
	{
		return UpdateBucket::name[ bucket ];
	}

	RED_INLINE static const char* GetTickPhaseName( const UpdateBucketPhase phase )
	{
		switch( phase )
		{
			case UpdateBucketPhase::EntitiesPreTick           : return "Entities_PreTick";
			case UpdateBucketPhase::EntitiesServiceEvents     : return "Entities_ServiceEvents";
			case UpdateBucketPhase::PrePhysicsTick            : return "PrePhysicsTick";
			case UpdateBucketPhase::UpdateTransformPrePhysics : return "UpdateTransformPrePhysics";
			case UpdateBucketPhase::PhysicsFlushBufferedState : return "PhysicsFlushBufferedState";
			case UpdateBucketPhase::PostPhysicsSyncResults    : return "PostPhysicsSyncResults";
			case UpdateBucketPhase::UpdateTransformPostPhysics: return "UpdateTransformPostPhysics";
			case UpdateBucketPhase::AnimationUpdate			  : return "AnimationUpdate";
			case UpdateBucketPhase::PostPhysicsTick           : return "PostPhysicsTick";
			case UpdateBucketPhase::EntitiesPostTick          : return "Entities_PostTick";
			case UpdateBucketPhase::EntitiesPostServiceEvents : return "Entities_PostServiceEvents";
		}
		return "<none-tick-phase>";
	}
};

struct ScriptableAccessInfo : ScriptableBaseAccessInfo
{
	SerializableID m_ID;
	const ScriptableAccessInfo* m_next = nullptr;
	Uint64 m_startTicks;
	Uint64 m_endTicks;
	Uint32 m_threadID;

	RED_INLINE static Bool OverlapTest( const ScriptableAccessInfo& a, const ScriptableAccessInfo& b )
	{
		return a.m_startTicks <= b.m_endTicks && b.m_startTicks <= a.m_endTicks;
	}
};

struct ScriptableCrossEntityAccessViaEventStats
{
	Uint32 m_numOccurrences = 0;
};

struct ScriptableStoredAccessInfo : public ScriptableBaseAccessInfo
{
	// Temporary callstack ptr; used when checking if matching entry exists in hashmap
	const CallstackEntry* m_tempCallstackPtr = nullptr;

	RED_INLINE void operator = ( const ScriptableStoredAccessInfo& other )
	{
		m_class = other.m_class;
		m_property = other.m_property;
		m_isShared = other.m_isShared;
		m_callstackStart = other.m_callstackStart;
		m_callstackSize = other.m_callstackSize;
	}

	RED_INLINE Bool operator == ( const ScriptableStoredAccessInfo& other ) const
	{
		return 
			m_class == other.m_class &&
			m_property == other.m_property &&
			m_isShared == other.m_isShared &&
			m_callstackSize == other.m_callstackSize &&
			!red::Memcmp( GetCallstack(), other.GetCallstack(), m_callstackSize * sizeof( CallstackEntry ) );
	}

	RED_INLINE void SetFrom( const ScriptableAccessInfo& other, const CallstackEntry* otherCallstack )
	{
		m_class = other.m_class;
		m_property = other.m_property;
		m_isShared = other.m_isShared;
		m_callstackStart = other.m_callstackStart;
		m_callstackSize = other.m_callstackSize;
		m_tempCallstackPtr = otherCallstack;
	}

	Uint32 CalcHash() const;
	const CallstackEntry* GetCallstack() const;
	void AllocateCallstack();

	void DumpToFile( const ScriptableCrossEntityAccessViaEventStats& stats ) const;
	void ToJSON( FILE* file, const ScriptableCrossEntityAccessViaEventStats& stats ) const;
	void CallstackToJSON( FILE* file, const Uint32 indentation, const char* elementName ) const;
};

struct ScriptableConflictStats
{
	Uint32 m_numOccurrences = 0;
	// Overlapping means: start-end times of 2 events overlap
	Bool m_overlappingOccurencesDetected = false;
	// Tick stages where the conflict occurred (encoded: group, bucket and phase)
	TickStage::BitMaskType m_tickStagesFlag = 0;
};

// Describes single thread-safety conflict
// Contains conflicting callstacks from 2 different threads that tried to access the same object (at the ~same time)
struct ScriptableConflict
{
	ScriptableStoredAccessInfo m_exclusiveAccess;
	ScriptableStoredAccessInfo m_accessFromAnotherThread;

	RED_INLINE Uint32 CalcHash() const
	{
		return red::CombineHashes32( m_exclusiveAccess.CalcHash(), m_accessFromAnotherThread.CalcHash() );
	}

	RED_INLINE Bool operator == ( const ScriptableConflict& other ) const
	{
		return
			m_exclusiveAccess == other.m_exclusiveAccess &&
			m_accessFromAnotherThread == other.m_accessFromAnotherThread;
	}

	void AllocateCallstacks();
	void ToJSON( FILE* file, const ScriptableConflictStats& stats ) const;
};

// Key used to distinguish different accesses
struct ScriptableAccessKey
{
	const SerializableID m_ID;
	const void* m_classOrProperty; // Class in "object capture mode" and property in "property capture mode"; just anything that uniquely identifies accessed thing (either object or property)

	RED_INLINE Uint32 CalcHash() const
	{
		return red::CalculateHash32( this, sizeof( *this ) );
	}

	RED_INLINE Bool operator == ( const ScriptableAccessKey& other ) const
	{
		return !red::Memcmp( this, &other, sizeof( *this ) );
	}
};

// Lists of accesses for a specific scriptable
struct ScriptableAccessList
{
	const ScriptableAccessInfo* m_firstShared = nullptr;
	const ScriptableAccessInfo* m_firstExclusive = nullptr;
};

enum class CaptureMode
{
	// Captures const & non-const function calls
	ObjectAccesses,
	// Captures reads & writes of individual properties
	PropertyAccesses,
	// Captures reads & writes of individual properties but only between entities (and their components)
	CrossEntityAccessViaEvent,
};

struct Data
{
	// Is monitoring enabled? Off by default
	Bool m_isMonitoringEnabled = false;
	// Does user want to enable monitoring? Off by default
	Bool m_isMonitoringEnabledRequest = false;

	// Mode in which capturing occurs
	CaptureMode m_captureMode = CaptureMode::ObjectAccesses;

	// Is current (PC) user in thread-safety reporting group? If yes, then make sure to enable data collection for them + make sure to dump their report on exit
	Bool m_isUserInReportingGroup = false;

	// Current tick stage
	TickStage m_tickStage;

	// Capture is done per thread to avoid synchronization
	red::StaticArray< red::DynArray< ScriptableAccessInfo >, MaxThreads > m_frameCapture;
	red::StaticArray< red::DynArray< CallstackEntry >, MaxThreads >  m_frameCaptureCallstackBuffer; // Callstack can be of variable length, so store them in single (per-thread) array for more optimal memory usage

	// FIXME: Should not be needed once we make sure data processing is properly only done between tick groups
	// ISSUE: However, some threads touch scripts outside of any tick group which is a problem
	red::SpinLock m_frameCaptureLocks[ MaxThreads ];

	// Temporary container used to group accesses by scriptables; only used during data processing
	red::HashMap< ScriptableAccessKey, ScriptableAccessList > m_scriptableAccessLists;

	// Container used to collect conflicts (and filter out duplicated conflicts)
	red::HashMap< ScriptableConflict, ScriptableConflictStats > m_conflicts;
	red::DynArray< CallstackEntry > m_conflictsCallstackBuffer{ red::PoolDebug() }; // Callstack can be of variable length, so store them in single array for more optimal memory usage

	// Container used to collect forbidden cross entity accesses via events
	red::HashMap< ScriptableStoredAccessInfo, ScriptableCrossEntityAccessViaEventStats > m_crossEntityAccessesViaEvents;

	// Currently collected stats
	Statistics m_currentStatistics;
	// Stats visible to user
	Statistics m_previousStatistics;
	// Last stats update time
	Double m_lastStatisticsUpdateSecs = 0;

	RED_INLINE Data()
		: m_scriptableAccessLists( red::PoolDebug() )
		, m_conflicts( red::PoolDebug() )
		, m_conflictsCallstackBuffer( red::PoolDebug() )
		, m_crossEntityAccessesViaEvents( red::PoolDebug() )
		, m_frameCapture( MaxThreads, red::PoolDebug() )
		, m_frameCaptureCallstackBuffer( MaxThreads, red::PoolDebug() ) 
	{
		m_tickStage.Init();
	}
};

struct ExtendedCallstackEntry
{
	const IScriptable* m_context;
	const rtti::Function* m_function;
	Uint64 m_startTime;
	Bool m_isShared;
	Bool m_requiresLock;
};

struct ThreadLocalData
{
	// Current thread's callstack size
	Uint32 m_size = 0;
	// Current thread's callstack
	CallstackEntry m_callstack[ MaxCallstackSize ];
	// Extended callstack info
	ExtendedCallstackEntry m_callstackEx[ MaxCallstackSize ];
};

} // namespace ScriptableThreadSafetyMonitor
} // namespace debug

#endif