/**
* Copyright (c) 2014-16 CD Projekt Red. All Rights Reserved.
*/

//////////////////////////////////////////////////////////////////////////
// headers

#include "build.h"
#include "profilerToolTracy.h"
#include "../../redMemory/include/hookTypes.h"

#ifdef USE_TRACY_PROFILER

//#define USE_TRACY_MEMORY_PROFILER

#define TRACY_ENABLE
#define TRACY_ON_DEMAND
#define TRACY_NO_AUTOINIT

#pragma warning( push )
#pragma warning( disable : 4996 )

#include "../../../../external/tracy/Tracy.hpp"
#include "../../../../external/tracy/TracyClient.cpp"
#include "../../../../external/tracy/TracyDynamic.hpp"

// Some code
#pragma warning( pop )

#ifdef USE_TRACY_MEMORY_PROFILER

namespace
{
	const Uint32 c_callstackDepth = 8;

	void PreAllocationCallback( red::memory::HookPreParameter & param, void* userData )
	{
		const red::memory::Block& block = *param.block;
		Uint32 size = *param.size;

		if ( block.address && size == 0 )
		{
			TracyFreeS( reinterpret_cast< void* >( block.address ), c_callstackDepth );
		}
	}

	void PostAllocationCallback( red::memory::HookPostParameter & param, void* userData )
	{
		const red::memory::Block& input = *param.inputBlock;
		const red::memory::Block& output = *param.outputBlock;

		if ( output.address )
		{
			if ( input.address )
			{
				TracyFreeS( reinterpret_cast< void* >( input.address ), c_callstackDepth );
				TracyAllocS( reinterpret_cast< void* >( output.address ), output.size, c_callstackDepth );
			}
			else
			{
				TracyAllocS( reinterpret_cast< void* >( output.address ), output.size, c_callstackDepth );
			}
		}
	}

	struct HookRegistrator
	{
		HookRegistrator()
		{
			const red::memory::HookCreationParameter param =
			{
				PreAllocationCallback,
				PostAllocationCallback,
				nullptr,
				red::memory::HookType::HookType_Memory_Profiler
			};

			m_memoryHookHandle = red::memory::CreateHook( param );
		}

		~HookRegistrator()
		{
			red::memory::RemoveHook( m_memoryHookHandle );
		}

		red::memory::HookHandle m_memoryHookHandle;
	};

	HookRegistrator s_hookRegistrator;
}

#endif

void TracyProfilerTool::StartBlock(red::InstrumentationObject* block, const char* scopeName)
{
	if (scopeName == nullptr && block != nullptr)
		scopeName = block->m_name;
	if (scopeName == nullptr)
		scopeName = "unknown";

	TracyDynamicZoneBeginS(1, "src", "func", block->m_profilerColor, scopeName, 0);
}

void TracyProfilerTool::StopBlock( red::InstrumentationObject* block, const char* scopeName )
{
	TracyDynamicZoneEnd();
}

void TracyProfilerTool::Init( const Uint32 mem )
{}

void TracyProfilerTool::Shutdown()
{}

void TracyProfilerTool::Start()
{}

void TracyProfilerTool::Stop()
{}

void TracyProfilerTool::NextFrame( red::ProfilerFrameType frameType )
{ 
	TracyInit;

	FrameMark;
}

void TracyProfilerTool::Update()
{}

#else
	RED_NO_EMPTY_FILE();
#endif

