/**
 * Copyright (c) 2018 CD Projekt Red. All Rights Reserved.
 */

#ifndef _RED_MEMORY_MEMORY_ANALYZER_H_
#define _RED_MEMORY_MEMORY_ANALYZER_H_

#if defined( RED_PLATFORM_ORBIS )
#include "memoryAnalyzerOrbis.h"
#endif

namespace red
{
namespace memory
{
#if defined( RED_PLATFORM_ORBIS )
	using MemoryAnalyzer = MemoryAnalyzerOrbis;
#else
	class HookHandler;
	class DummyMemoryAnalyzer
	{
	public:
		void Initialize( HookHandler* ) {}
	};

	using MemoryAnalyzer = DummyMemoryAnalyzer;
#endif
}
}

#endif