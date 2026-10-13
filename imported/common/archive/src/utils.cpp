/*
 * Copyright (c) 2016 CD Projekt Red. All Rights Reserved.
 */

#include "build.h"
#include "utils.h"

namespace archive
{
namespace utils
{

Uint64 GetAlignedPosition( Uint64 position, Uint32 alignment )
{
	const Uint64 remainder = position % alignment;
	if ( remainder != 0 )
	{
		return position + alignment - remainder;
	}
	return position;
}

Uint32 GetAlignedSize( Uint32 size, Uint32 alignment )
{
	const Uint32 remainder = size % alignment;
	if ( remainder != 0 )
	{
		return size + alignment - remainder;
	}
	return size;
}

Uint32 GetAlignedPaddingBytes( Uint64 position, Uint32 alignment )
{
	const Uint32 remainder = static_cast<Uint32>( position % alignment );
	if ( remainder != 0 )
	{
		return alignment - remainder;
	}
	return 0;
}

} // namespace utils
} // namespace archive
