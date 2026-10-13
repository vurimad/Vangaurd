/**
* Copyright (c) 2019 CD Projekt Red. All Rights Reserved.
*/

#pragma once

namespace fs
{
	const Uint32 c_headerMarker = 'CLZF';

	struct LZ4ChunkMetadata
	{
		Uint32 compressedOffset;	// Position of data in raw file
		Uint32 compressedSize;		// Size of data in raw file
		Uint32 uncompressedSize;	// Size after decompression
	};
}