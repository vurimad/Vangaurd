/**
 * Copyright (c) 2016 CD Projekt Red. All Rights Reserved.
 */

#pragma once

namespace archive
{
namespace utils
{
	ARCHIVE_API Uint64 GetAlignedPosition( Uint64 position, Uint32 alignment );
	ARCHIVE_API Uint32 GetAlignedSize( Uint32 size, Uint32 alignment );
	ARCHIVE_API Uint32 GetAlignedPaddingBytes( Uint64 position, Uint32 alignment );
} // namespace utils
} // namespace archive
