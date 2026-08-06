#pragma once

#include "serializable.h"

#if defined( RED_CONFIGURATION_DEBUG ) || defined( RED_CONFIGURATION_NOPTS ) || defined( RED_CONFIGURATION_RELEASE )
	#define RED_ENABLE_SERIALIZABLE_DEBUG
#endif

#ifdef RED_ENABLE_SERIALIZABLE_DEBUG

namespace serializableDebug
{
	// Enables debug based on command line option 'debugObjects'
	// Note: enabling it right from the engine startup time assures all of the applicable serializable instances will be captured
	RED_REFLECTION_API void Initialize();

	// Enables debug
	// Note 1: enabling debug involves significant additional cost - each single serializable needs to be tracked
	// Note 2: once enabled, only captures objects created from this point in time
	RED_REFLECTION_API void Enable();
	RED_REFLECTION_API void Disable();
	RED_REFLECTION_API Bool IsEnabled();

	// Registers serializable in the debug (to be invoked on object creation)
	// Note: currently only called from rtti::ClassType::CreateObject which, for example, guarantees all script side created objects will be covered
	RED_REFLECTION_API void Register( const ISerializable* serializable, const rtti::ClassType* cls );
	// Unregisters serializable from the debug (to be invoked on object destruction)
	// Note: currently called from ISerializable destructor which shall cover all of the cases
	RED_REFLECTION_API void Unregister( const ISerializable* serializable );

	struct ClassStats
	{
		// Current number of instances
		Uint32 count : 31;
		// Indicates if the number of instances ever dropped (since when the tool was enabled)
		Uint32 everDropped : 1;

		ClassStats()
			: count( 0 )
			, everDropped( 0 )
		{}
	};
	struct SerializableStats
	{
		// Number of objects per class
		red::HashMap< const rtti::ClassType*, ClassStats > classStats{ red::PoolDebug() };
	};
	// Captures current state of all serializables created via rtti::ClassType::CreateObject
	RED_REFLECTION_API void CaptureStatistics( SerializableStats& stats );

	// Dumps stats to a file in current binary folder
	RED_REFLECTION_API Bool DumpStatsToFile();

	typedef red::FixedSizeFunction< Bool ( const ISerializable& ) > FilterFunction;
	typedef red::FixedSizeFunction< void ( const red::DynArray< const ISerializable* >& ) > VisitorFunction;

	// Visits all serializables of a specific class
	RED_REFLECTION_API void VisitObjects( FilterFunction&& filter, VisitorFunction&& visitor );
}

#endif