/**
 * Copyright (c) 2018 CD Projekt Red. All Rights Reserved.
 */

#include "build.h"
#include "../include/memoryAnalyzerUtils.h"
#include "threadIdProvider.h"

#if defined( RED_USE_ORBIS_MEMORY_ANALYZER )
#include <mat.h>
#endif

namespace
{
	red::memory::ThreadIdProvider s_threadIdProvider;
}

namespace red
{
namespace memory
{
	void MarkNewFrameInMemoryAnalyzer()
	{
#if defined( RED_USE_ORBIS_MEMORY_ANALYZER )
		sceMatNewFrame();
#endif
	}

	void PushMarkerInMemoryAnalyzer( const char* label, u32 color )
	{
#if defined( RED_USE_ORBIS_MEMORY_ANALYZER )
		sceMatPushMarker( label, color, 0 );
#else
		RED_UNUSED( label );
		RED_UNUSED( color );
#endif
	}

	void PopMarkerInMemoryAnalyzer()
	{
#if defined( RED_USE_ORBIS_MEMORY_ANALYZER )
		sceMatPopMarker();
#endif
	}

	void WriteBookmarkInMemoryAnalyzer( const char* label, const char* description )
	{
#if defined( RED_USE_ORBIS_MEMORY_ANALYZER )
		sceMatWriteBookmark( label, description );
#else
		RED_UNUSED( label );
		RED_UNUSED( description );
#endif
	}

	void SetThreadInfoInMemoryAnalyzer( const char* name )
	{
#if defined( RED_USE_ORBIS_MEMORY_ANALYZER )
		ThreadId id = s_threadIdProvider.GetCurrentId();
		sceMatSetThreadInfo( id, name, 0 );
#else
		RED_UNUSED( name );
#endif
	}
}
}
