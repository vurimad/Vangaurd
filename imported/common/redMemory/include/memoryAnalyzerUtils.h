/**
 * Copyright (c) 2018 CD Projekt Red. All Rights Reserved.
 */

#ifndef _RED_MEMORY_INCLUDE_MEMORY_ANALYZER_UTILS_H_
#define _RED_MEMORY_INCLUDE_MEMORY_ANALYZER_UTILS_H_

#include "redMemoryInternal.h"

namespace red
{
namespace memory
{
	const u32 c_defaultMarkerColor = 0x00B98034;

	RED_MEMORY_API void MarkNewFrameInMemoryAnalyzer();

	RED_MEMORY_API void PushMarkerInMemoryAnalyzer( const char* label, u32 color = c_defaultMarkerColor );
	RED_MEMORY_API void PopMarkerInMemoryAnalyzer();

	RED_MEMORY_API void WriteBookmarkInMemoryAnalyzer( const char* label, const char* description );

	RED_MEMORY_API void SetThreadInfoInMemoryAnalyzer( const char* name );

	struct RED_MEMORY_API MemoryAnalyzerScopeMarker
	{
		MemoryAnalyzerScopeMarker( const char* label )
		{
			PushMarkerInMemoryAnalyzer( label );
		}

		~MemoryAnalyzerScopeMarker()
		{
			PopMarkerInMemoryAnalyzer();
		}
	};
}
}

#define RED_MEMORY_MA_MARK_NEW_FRAME() red::memory::MarkNewFrameInMemoryAnalyzer()

#define RED_MEMORY_MA_SCOPE_MARKER_VAR( var, label ) \
	red::memory::MemoryAnalyzerScopeMarker RED_UNIQUE_NAME( RED_CONCATENATE( __scopeMarker__, var ) )( #label );

#define RED_MEMORY_MA_SCOPE_MARKER( label ) RED_MEMORY_MA_SCOPE_MARKER_VAR( __LINE__, label )

#define RED_MEMORY_MA_WRITE_BOOKMARK( label, description ) red::memory::WriteBookmarkInMemoryAnalyzer( label, description )

#define RED_MEMORY_MA_SET_THREAD_INFO( threadName ) red::memory::SetThreadInfoInMemoryAnalyzer( threadName )

#endif