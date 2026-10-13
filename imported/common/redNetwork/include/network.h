/**
* Copyright (c) 2013 CDProjekt Red, Inc. All Rights Reserved.
*/

#pragma once

#include "../../redMemory/include/pool.h"
#include "../../redMemory/include/uniqueBuffer.h" 
#include "redNetworkApi.h"

#if defined( RED_PLATFORM_WIN32 ) || defined( RED_PLATFORM_WIN64 ) || defined( RED_PLATFORM_DURANGO )
#	include "platformWindows.h"
#elif defined( RED_PLATFORM_ORBIS )
#	include "platformOrbis.h"
#elif defined( RED_PLATFORM_LINUX )
#	include "platformLinux.h"
#else
#	error No red network implementation for current platform
#endif

namespace red {
	namespace Network {

		constexpr const Uint32 MAX_CONNECTIONS = 45;
		constexpr const Uint32 MAX_LISTENERS = 16;
		constexpr const Uint32 MAX_PACKET = RED_MEGA_BYTE( 16 );
		constexpr const Uint32 MEMORY_POOL_SIZE = RED_MEGA_BYTE( 10 );

		RED_MEMORY_POOL( PoolBackendNetwork, red::memory::DefaultAllocator, REDNETWORK_API );

		// Initialize memory
		REDNETWORK_API void InitializeMemoryPools();

		// Create memory buffer 
		REDNETWORK_API red::UniqueBuffer CreatePacketBuffer( const Uint32 bufferSize );
	}
}