/**
 * Copyright (c) 2017 CD Projekt Red. All Rights Reserved.
 */

#include "build.h"
#include "metricsCaptureOrbis.h"

#if defined( RED_CONFIGURATION_ORBIS_USE_DBGLIB )
#include <kernel.h>
#include <libdbg.h>
#endif

namespace red
{
namespace memory
{
namespace
{
#if defined( RED_CONFIGURATION_ORBIS_USE_DBGLIB )
	void EnumerateLoadedModulesProc( SceDbgModule module, Serializer& serializer )
	{
		SceDbgModuleInfo info = {};
		info.size = sizeof( SceDbgModuleInfo );
		const auto result = sceDbgGetModuleInfo( module, &info );
		if ( result != SCE_OK )
		{
			RED_LOG_ERROR( "Failed to get info for module %ld, error code=%ld", module, result );
			return;
		}

		const char c_modulePrefix[] = "MOD_"; // Used to identify modules
		red::Uint32 moduleNameLength = static_cast< u32 >( Strlen( info.name ) );
		red::Uint64 moduleSize64 = static_cast< u64 >( info.size );
		red::Uint64 moduleBase = reinterpret_cast< u64 >( info.segmentInfo->baseAddr );

		serializer.Serialize( c_modulePrefix, sizeof( c_modulePrefix ) - 1 ); // Don't write the terminator
		serializer.Serialize( moduleSize64 );
		serializer.Serialize( moduleNameLength );
		serializer.Serialize( info.name, moduleNameLength );
		serializer.Serialize( moduleBase );
	}

	void EnumerateLoadedModules( Serializer& serializer )
	{
		const int maxModulesCount = 256;
		SceDbgModule modules[ maxModulesCount ] = {};
		size_t actualModulesCount = 0;

		const auto result = ::sceDbgGetModuleList( modules, maxModulesCount, &actualModulesCount );
		if ( result != SCE_OK )
		{
			RED_LOG_ERROR( "Failed to get module list, error code=%d", result );
			return;
		}

		for ( size_t i = 0; i < actualModulesCount; ++i )
		{
			EnumerateLoadedModulesProc( modules[ i ], serializer );
		}
	}
#endif
}

	void MetricsCaptureOrbis::WritePlatfromIdentifier( Serializer& serializer )
	{
		const char platformIdentifier[] = "ORBIS";
		const u16 platformIdentifierSize = static_cast< u16 >( sizeof( platformIdentifier ) - 1 );
		serializer.Serialize( platformIdentifierSize );
		serializer.Serialize( platformIdentifier, platformIdentifierSize );
	}

	void MetricsCaptureOrbis::WriteLoadedModules( Serializer& serializer )
	{
#if defined( RED_CONFIGURATION_ORBIS_USE_DBGLIB )
		EnumerateLoadedModules( serializer );
#endif
	}
}
}