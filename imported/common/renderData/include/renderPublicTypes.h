/**
* Copyright (c) 2016 CD Projekt Red. All Rights Reserved.
*/

#pragma once


// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

namespace GpuWrapApi
{
	enum eBufferChunkType : Uint32;
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -


struct QueryOnlyBoxId final
{
	union {
		Uint32 hash;
		struct
		{
			Uint16 index;
			Uint16 generation;
		};
	};

	QueryOnlyBoxId( Uint32 hash = UINT32_MAX )
		: hash( hash )
	{

	}

	RED_FORCE_INLINE Bool operator == ( const QueryOnlyBoxId& other ) const { return hash == other.hash; }
	RED_FORCE_INLINE Bool operator != ( const QueryOnlyBoxId& other ) const { return hash != other.hash; }

	RENDER_DATA_API static QueryOnlyBoxId Invalid();
};

struct QueryOnlyBoxIdSortPredicate
{
	bool operator()( const QueryOnlyBoxId& a, const QueryOnlyBoxId& b )
	{
		return a.index > b.index;
	}
};

struct QueryOnlyBoxCreationInfo
{
	Box bounds = Box::UNIT();
	Float activationDistance = 100.f;
	
    // will be used only in non-final builds
    CName usage;
};

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
