/**
* Copyright (c) 2017 CDProjekt Red, Inc. All Rights Reserved.
*/

#include "build.h"
#include "network.h"

using namespace red::Network;

void red::Network::InitializeMemoryPools()
{
	RED_INITIALIZE_MEMORY_POOL( PoolBackendNetwork, red::PoolBackend, red::memory::AcquireDefaultAllocator(), MEMORY_POOL_SIZE );
}

red::UniqueBuffer red::Network::CreatePacketBuffer( const Uint32 bufferSize )
{
	return red::CreateUniqueBuffer< PoolBackendNetwork >( bufferSize, 8 );
}
