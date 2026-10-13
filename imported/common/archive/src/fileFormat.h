/**
  * Copyright (c) 2016 CD Projekt Red. All Rights Reserved.
  */

#pragma once

#include "../../../common/redSystem/include/types.h"
#include "../include/common.h"

namespace archive
{
namespace fileformat
{

//
// NOTICE: For any static_asserts hit within this file, ensure that the changes have not been made
// by accident. Changes to these structures for any reason could render all existing archives invalid!
//

//
// NOTE: If any of these structures change then please update the documentation on Confluence!
//

#pragma pack(push, 1)

static constexpr Uint32 Version = archive::FileFormatVersion;

// Global alignment for files in the archive
static constexpr Uint32 GlobalAlignment = static_cast<Uint32>( 4_KB );

// Global padding byte written after files
static constexpr Uint8 PaddingByte = 0xD9;

struct MainHeader
{
	Uint32 magic;
	Uint32 version;
	Uint64 indexPosition;
	Uint32 indexSize;
	Uint64 debugPosition;
	Uint32 debugSize;
	Uint64 totalFileSize;

	static constexpr Uint32 MAGIC_VALUE = 'RADR'; // ReD ARchive
};

static_assert( sizeof(MainHeader) == 40, "Archive Main Header size has changed, verify version is correct" );

enum BuildMachine : Uint32
{
	Unknown = 0,
	Local,			// Built locally on a client machine
	Server,			// Built on a build server machine
	Official,		// Official build for release
};

struct DebugContent
{
	BuildMachine buildMachine;

	static constexpr Uint32 COMPUTER_NAME_LENGTH = 64;
	char computerName[COMPUTER_NAME_LENGTH];

	static constexpr Uint32 BUILD_NAME_LENGTH = 64;
	char buildName[BUILD_NAME_LENGTH];
};

static_assert( sizeof(DebugContent) == 132, "Archive Debug Content size has changed, verify version is correct" );

struct IndexHeader
{
	// Offsets are from the index header start position
	Uint32 fileTableOffset;
	Uint32 fileTableSize;

	// Alignment only for tables within the index
	static constexpr Uint32 TABLE_ALIGNMENT = 4;
};

static_assert( sizeof(IndexHeader) == 8, "Archive Index Header size has changed, verify this is correct" );


struct FileTableHeader
{
	Uint64 crc;
	Uint32 fileEntryCount;
	Uint32 fileSegmentCount;
	Uint32 resourceDependencyCount;
};

static_assert( sizeof(FileTableHeader) == 20, "Archive Index File Table Header size has changed, verify this is correct" );


static constexpr Uint32 FileHashBytes = ::archive::FileHash::NUM_BYTES;
static_assert( FileHashBytes == 20, "Size of the file / dependency hash has changed, please verify this is correct!" );

struct FileTableEntry
{
	Uint64 id;
	Uint64 timestamp;
	Uint32 numInlineBufferSegments;
	Uint32 segmentsStart;
	Uint32 segmentsEnd;
	Uint32 resourceDependenciesStart;
	Uint32 resourceDependenciesEnd;
	Uint8 hash[FileHashBytes];
};

static_assert( sizeof(FileTableEntry) == 56, "Archive Index File Table Entry size has changed, verify this is correct" );

//struct FileTableSegmentEntry
//{
//	Uint64 position;
//	Uint32 sizeOnDisk;
//	Uint32 sizeInMemory;
//};
using FileTableSegmentEntry = archive::FileSegment;

static_assert( sizeof(FileTableSegmentEntry) == 16, "Archive index File Table Segment Entry size has changed, verify this is correct" );


// Namespace for storing older versions of the archive file format
namespace old
{

using FileTableSegmentEntry = ::archive::fileformat::FileTableSegmentEntry;

struct FileTableHeader
{
	Uint64 crc;
	Uint32 fileEntryCount;
	Uint32 fileSegmentCount;
};

static_assert(sizeof(FileTableHeader) == 16, "Archive Index File Table Header size has changed, verify this is correct");

struct FileTableEntry
{
	Uint64 id;
	Uint64 timestamp;
	Uint32 numInlineBufferSegments;
	Uint32 segmentsStart;
	Uint32 segmentsEnd;
	Uint8 hash[FileHashBytes];
};

static constexpr Uint32 Version = 11;

} // old

#pragma pack(pop)

} // namespace fileformat

} // namespace archive
