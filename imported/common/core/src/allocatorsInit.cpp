/**
 * Copyright (c) 2020 CD Projekt Red. All Rights Reserved.
 */

#include "build.h"

#include "allocatorsInit.h"
#include "allocators.h"

namespace red
{
	CoreAllocatorStorage::CoreAllocatorStorage()
	{
	}

	void CoreAllocatorStorage::Initialize()
	{
		TelemetryAllocatorParameter initParameters = {
			&memory::AcquireSystemAllocator(),
			&memory::AcquireDefaultAllocator()
		};
		m_telemetryAllocator.Initialize( initParameters );
	}

	static CoreAllocatorStorage c_telemetryAllocatorStorage;

	void InitializeAllocators()
	{
		c_telemetryAllocatorStorage.Initialize();
	}

	CoreAllocatorStorage& GetCoreAllocatorStorage()
	{
		return c_telemetryAllocatorStorage;
	}
}
