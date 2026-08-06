#pragma once

#ifdef RED_MONITOR_SCRIPTABLE_THREAD_SAFETY

namespace debug
{
	// Runtime monitor is a development only tool that tries to detect at runtime all of the
	// PDSAs (Potentially Dangerous Scriptable Accesses TM).
	//
	// Scriptable access is considered potentially dangerous whenever any of the scenarios occur:
	// - 2 (or more) threads write to same scriptable or scriptable property (depending on capture mode) during same tick group/phase
	// - 1 thread writes to scriptable or scriptable property (depending on capture mode) and 1 (or more) threads read from it during same tick group/phase
	//
	// There are 2 capture modes.
	// Initially developed object capture mode.
	// Later developed property capture mode which gives much more detailed info.
	//
	// About object capture mode:
	// -----------------------
	// The tool distinguishes reads from writes via scripted function attribute 'const'.
	// Every 'const' function is considered read. Any other function is considered write.
	// Note: We say it's potentially dangerous even though in reality it might be safe.
	// There are few reasons why PDSA may not actually be dangerous, for example:
	// - accesses occur during the same tick group but not at the same time (aka "lucky")
	// - accesses touch different (and logically independent!) scriptable properties (aka "secret knowledge")
	//
	// About property capture mode:
	// ----------------------------
	// It is better than object capture mode because it detect all property accesses (not just object accesses aka function calls).
	// The downside of this approach is that it introduces noticeable overhead: my tests in Release on Vertical Slice gamedef showed 35 vs regular 40 fps.
	//
	// Note: There is minimalistic integration of the tool with ImGui which allows the user
	// to dump all of the distinct PDSAs to file or logs.
	//
	// HOW TO USE:
	// -----------
	// - use non-final build
	// - enable it in one of the 2 ways:
	//   - way 1:
	//     - pass "-startScriptableThreadSafetyMonitor" argument via command line to enable monitor from the app start
	//     - at runtime bring up "ScriptableThreadSafetyMonitor" window from ImGui circle to:
	//			(1) toggle monitor on & off
	//			(2) dump monitor data to logs or file
	//			(3) watch statistics
	//   - way 2:
	//     - add your user name to //R6.Root/Mainline/engine/config/debug_script_thread_safety_group_props.csv for property capture mode
	//       or to //R6.Root/Mainline/engine/config/debug_script_thread_safety_group.csv for object capture mode
	// - check per-person daily reports generated into V:\PROGRAMMING\SCRIPTS\THREAD_SAFETY_REPORTS\SUMMARIES_PROPS
	// - generate summary daily reports by running //R6.Root/Mainline/dev/internal/ScriptThreadSafetyReportGenerator/ScriptThreadSafetyReportGenerator/bin/Release/ScriptThreadSafetyReportGenerator.exe
	// - check summary daily reports in V:\PROGRAMMING\SCRIPTS\THREAD_SAFETY_REPORTS\SUMMARIES_PROPS
	//
	namespace ScriptableThreadSafetyMonitor
	{
		struct Statistics
		{
			Uint32 m_numLastFrameCapturedAccesses;
			Uint64 m_totalCapturedCalls;
			Uint32 m_totalFoundConflicts;
			Uint32 m_totalFoundConflictOccurrences;
			Uint32 m_memoryUsage;
		};

		RED_REFLECTION_API void Initialize( const Bool enableAtStartup, const red::AbsolutePath& rootEnginePath );
		RED_REFLECTION_API void Deinitialize();

		RED_REFLECTION_API void EnableMonitoring( const Bool enable );
		RED_REFLECTION_API Bool IsMonitoringEnabled();
		RED_REFLECTION_API void DumpMonitoringReportToFile();

		RED_REFLECTION_API Statistics GetStatistics();

		RED_REFLECTION_API void PushCallstack( const IScriptable* context, const CName functionName, const Bool isShared );
		RED_REFLECTION_API void PushCallstack( const IScriptable* context, const rtti::Function* function );
		RED_REFLECTION_API void PopCallstack();

		RED_REFLECTION_API void OnReadProperty( const IScriptable* object, const rtti::Property* property );
		RED_REFLECTION_API void OnWriteProperty( const IScriptable* object, const rtti::Property* property );

		RED_REFLECTION_API void OnBeginTickGroup( const Uint32 tickGroup );
		RED_REFLECTION_API void OnEndTickGroup();
		RED_REFLECTION_API void OnBeginTickBucket( const Uint32 bucketIndex );
			RED_REFLECTION_API void OnBeginTickPhase( const Uint32 phaseIndex );
			RED_REFLECTION_API void OnEndTickPhase();
		RED_REFLECTION_API void OnEndTickBucket();

		class ScopedPushPopCallstackHelper
		{
		public:
			RED_INLINE ScopedPushPopCallstackHelper( const IScriptable* context, const rtti::Function* function )
			{
				::debug::ScriptableThreadSafetyMonitor::PushCallstack( context, function );
			}

			RED_INLINE ScopedPushPopCallstackHelper( const IScriptable* context, const CName functionName, const Bool isShared )
			{
				::debug::ScriptableThreadSafetyMonitor::PushCallstack( context, functionName, isShared );
			}

			RED_INLINE ~ScopedPushPopCallstackHelper()
			{
				::debug::ScriptableThreadSafetyMonitor::PopCallstack();
			}
		};

	}
}

#define RED_SCRIPTABLE_THREAD_SAFETY_MONITOR_BEGIN_TICK_GROUP( tickGroup )			::debug::ScriptableThreadSafetyMonitor::OnBeginTickGroup( tickGroup )
#define RED_SCRIPTABLE_THREAD_SAFETY_MONITOR_END_TICK_GROUP()						::debug::ScriptableThreadSafetyMonitor::OnEndTickGroup()
#define RED_SCRIPTABLE_THREAD_SAFETY_MONITOR_BEGIN_TICK_BUCKET( bucketIndex )		::debug::ScriptableThreadSafetyMonitor::OnBeginTickBucket( bucketIndex )
#define RED_SCRIPTABLE_THREAD_SAFETY_MONITOR_END_TICK_BUCKET()						::debug::ScriptableThreadSafetyMonitor::OnEndTickBucket()
#define RED_SCRIPTABLE_THREAD_SAFETY_MONITOR_BEGIN_TICK_PHASE( phaseIndex )			::debug::ScriptableThreadSafetyMonitor::OnBeginTickPhase( phaseIndex )
#define RED_SCRIPTABLE_THREAD_SAFETY_MONITOR_END_TICK_PHASE()						::debug::ScriptableThreadSafetyMonitor::OnEndTickPhase()
#define RED_SCRIPTABLE_THREAD_SAFETY_MONITOR_PUSH_CALLSTACK( ctx, function )		::debug::ScriptableThreadSafetyMonitor::PushCallstack( context, function )
#define RED_SCRIPTABLE_THREAD_SAFETY_MONITOR_PUSH_CALLSTACK_CUSTOM_SCOPED( ctx, name, shared ) ::debug::ScriptableThreadSafetyMonitor::ScopedPushPopCallstackHelper debug_ScriptableThreadSafetyMonitor_ScopedPushPopCallstackHelper( ctx, name, shared )
#define RED_SCRIPTABLE_THREAD_SAFETY_MONITOR_POP_CALLSTACK()						::debug::ScriptableThreadSafetyMonitor::PopCallstack()
#define RED_SCRIPTABLE_THREAD_SAFETY_MONITOR_ON_READ_PROPERTY()						::debug::ScriptableThreadSafetyMonitor::OnReadProperty( stack.m_lValuePropertyOwner, stack.m_lValueProperty )
#define RED_SCRIPTABLE_THREAD_SAFETY_MONITOR_ON_READ_PROPERTY_CUSTOM( ctx, prop )	::debug::ScriptableThreadSafetyMonitor::OnReadProperty( ctx, prop )
#define RED_SCRIPTABLE_THREAD_SAFETY_MONITOR_ON_WRITE_PROPERTY()					::debug::ScriptableThreadSafetyMonitor::OnWriteProperty( stack.m_lValuePropertyOwner, stack.m_lValueProperty )
#define RED_SCRIPTABLE_THREAD_SAFETY_MONITOR_ON_WRITE_PROPERTY_CUSTOM( ctx, prop )	::debug::ScriptableThreadSafetyMonitor::OnWriteProperty( ctx, prop )

struct READ_WRITE_HELPER
{
	template < typename OBJECT_TYPE, typename PROPERTY_TYPE, int SRC_CODE_LINE = __LINE__ >
	static const PROPERTY_TYPE& Read( const OBJECT_TYPE* object, const PROPERTY_TYPE& var, const CName propertyName )
	{
		static const rtti::Property* property = object->GetClass()->FindProperty( propertyName );
		if ( property )
		{
			::debug::ScriptableThreadSafetyMonitor::OnReadProperty( object, property );
		}
		return var;
	}

	template < typename OBJECT_TYPE, typename PROPERTY_TYPE, int SRC_CODE_LINE = __LINE__ >
	static PROPERTY_TYPE& Written( const OBJECT_TYPE* object, PROPERTY_TYPE& var, const CName propertyName )
	{
		static const rtti::Property* property = object->GetClass()->FindProperty( propertyName );
		if ( property )
		{
			::debug::ScriptableThreadSafetyMonitor::OnWriteProperty( object, property );
		}
		return var;
	}
};
#define RED_SCRIPTABLE_THREAD_SAFETY_MONITOR_READ_PROPERTY_NATIVE( prop )			READ_WRITE_HELPER::Read( this, prop, RED_NAME_CONSTEXPR( #prop + 2 ) )
#define RED_SCRIPTABLE_THREAD_SAFETY_MONITOR_WRITTEN_PROPERTY_NATIVE( prop )		READ_WRITE_HELPER::Written( this, prop, RED_NAME_CONSTEXPR( #prop + 2 ) )

#else

#define RED_SCRIPTABLE_THREAD_SAFETY_MONITOR_BEGIN_TICK_GROUP( tickGroup )
#define RED_SCRIPTABLE_THREAD_SAFETY_MONITOR_END_TICK_GROUP();
#define RED_SCRIPTABLE_THREAD_SAFETY_MONITOR_BEGIN_TICK_BUCKET( bucketIndex )
#define RED_SCRIPTABLE_THREAD_SAFETY_MONITOR_END_TICK_BUCKET()
#define RED_SCRIPTABLE_THREAD_SAFETY_MONITOR_BEGIN_TICK_PHASE( phaseIndex )
#define RED_SCRIPTABLE_THREAD_SAFETY_MONITOR_END_TICK_PHASE()
#define RED_SCRIPTABLE_THREAD_SAFETY_MONITOR_PUSH_CALLSTACK( ctx, function )
#define RED_SCRIPTABLE_THREAD_SAFETY_MONITOR_PUSH_CALLSTACK_CUSTOM_SCOPED( ctx, name, shared )
#define RED_SCRIPTABLE_THREAD_SAFETY_MONITOR_POP_CALLSTACK()
#define RED_SCRIPTABLE_THREAD_SAFETY_MONITOR_ON_READ_PROPERTY()
#define RED_SCRIPTABLE_THREAD_SAFETY_MONITOR_ON_READ_PROPERTY_CUSTOM( ctx, prop )
#define RED_SCRIPTABLE_THREAD_SAFETY_MONITOR_ON_WRITE_PROPERTY()
#define RED_SCRIPTABLE_THREAD_SAFETY_MONITOR_ON_WRITE_PROPERTY_CUSTOM( ctx, prop )

#endif