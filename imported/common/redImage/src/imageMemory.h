/**
* Copyright (c) 2016 CDProjekt Red, Inc. All Rights Reserved.
*/

/// Original code (C) RenderLab 2014 by Tomasz "RexDex" Jonarski
/// Copied with permission from author by author :)

namespace img
{

	/// memory allocation functions for the redImage 
	namespace memory
	{
		extern REDIMAGE_API void InitializeMemoryPools();

		// allocate memory, routed to internal Image pool
		extern void* Allocate( const Uint64 size, const Uint32 alignment = 8 );

		/// free memory, routed to internal Image pool
		extern void Free( void* ptr );

	} // memory

} // img

 