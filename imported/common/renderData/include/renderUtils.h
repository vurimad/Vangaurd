/**
* Copyright (c) 2017 CD Projekt Red. All Rights Reserved.
*/
#pragma once


// Miscellaneous utilities for rendering, that don't necessarily belong anywhere better...


namespace rend
{

	// Calculate a Uint64 value based on a CName. This can be used to generate camera names (which are Uint64)
	// or to do the similar mapping in CPU-accessible material parameters.
	RED_FORCE_INLINE Uint64 CalculateNameU64( const CName name )
	{
		return name.GetHash();
	}

}
