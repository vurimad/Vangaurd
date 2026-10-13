//
// Copyright (c) 2019 CD Projekt Red. All Rights Reserved.
//	

#pragma once

namespace rend {

// describes font look, part of the key in the glyph cache
struct FontStyle
{
	Uint16 size = 12;
	Uint16 outlineWidth = 0;
	// TODO hinting, etc.

	RED_FORCE_INLINE bool operator == ( const FontStyle& other ) const
	{
		return size == other.size && outlineWidth == other.outlineWidth;
	}

	RED_FORCE_INLINE bool operator != ( const FontStyle& other ) const
	{
		return !( operator ==( other ) );
	}

	RED_FORCE_INLINE Uint32 CalcHash() const
	{
		return (Uint32)size | ( (Uint32)outlineWidth << 16 );
	}
};

} // rend