/**
 * Copyright (c) 2020 CD Projekt Red. All Rights Reserved.
 */

#pragma  once

#include "telemetryAllocator.h"

namespace red
{
	struct CoreAllocatorStorage
	{
		CoreAllocatorStorage();

		void Initialize();
		TelemetryAllocator m_telemetryAllocator;
	};

	CoreAllocatorStorage& GetCoreAllocatorStorage();
}