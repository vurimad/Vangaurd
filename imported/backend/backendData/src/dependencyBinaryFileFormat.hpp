/*
* Copyright (c) 2019 CD Projekt Red. All Rights Reserved.
*/

#pragma once

namespace dependency
{
namespace prv
{
namespace fileformat
{

#pragma pack(push, 1)

//
// File Structure
//
// Header
// [ Index ] - indexes into ranges within the records buffer
// [ Records ] - serialised ResourceInfo records
// [ Strings Index ] - offsets into the buffer where strings begin
// [ Strings Buffer ] - string data stored in memory
//

struct Header
{
	Uint32 magic;
	Uint32 version;
	Uint32 entriesCount;
	Uint32 entriesIndexOffset;
	Uint32 entriesIndexSize;
	Uint32 entriesOffset;
	Uint32 entriesSize;
	Uint32 stringsCount;
	Uint32 stringIndexOffset;
	Uint32 stringIndexSize;
	Uint32 stringDataOffset;
	Uint32 stringDataSize;

	static constexpr Uint32 MAGIC_VALUE = 'NIBD'; // DBIN backwards
};

struct IndexEntry
{
	Uint32 offset;
	Uint32 size;
};

#pragma pack(pop)

} // fileformat
} // prv
} // dependency
