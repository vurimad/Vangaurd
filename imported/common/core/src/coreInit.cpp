#include "build.h"
#include "../../redReflection/include/engineTime.h"

// this file is auto generated and contains registration function for rtti types in this project
#include "../rtti/core/typeRegistryFuncImpl.h"

RED_MODULE(core)
{
	red::InitializeAllocators();

	RED_INITIALIZE_MEMORY_POOL( red::PoolGalaxy, red::PoolEngine, red::AcquireTelemetryAllocator(), RED_MEGA_BYTE( 16 ) );

	// Iitialize timer
	EngineTime::Init();

	// Core RTTI classes
	RegisterCoreRTTI();
}