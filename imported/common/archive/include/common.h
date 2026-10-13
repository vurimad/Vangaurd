/**
 * Copyright (c) 2016 CD Projekt Red. All Rights Reserved.
 */

#pragma once

#include "../../redSystem/include/types.h"
#include "../../redCompression/include/compression.h"

namespace archive
{
	static constexpr Uint32 FileFormatVersion = 12;

	enum class LoadingResult
	{
		SUCCESSFUL = 0,
		INVALID_ARCHIVE,
		INVALID_VERSION,
		INVALID_CRC,
		ERROR_IO,
	};

	enum class SavingResult
	{
		SUCCESSFUL = 0,
		ERROR_IO,
		INVALID_ARCHIVE,
		INVALID_VERSION,
		EMPTY_ARCHIVE,
		NOT_IMPLEMENTED,
	};

	enum class OpenMode
	{
		READING_ONLY,	// Read Only access for only the fast path (default for the game)
		WRITING_ONLY,	// Write only access for creating a new archive
		MODIFICATION,	// Read and Write access for modifying archive contents
		INSPECTION,		// Read only access but to all of the data in the archive
	};

	constexpr bool OpenModeIsReadOnly( OpenMode mode )
	{
		return mode == OpenMode::READING_ONLY || mode == OpenMode::INSPECTION;
	}

	constexpr bool OpenModePermitsReading( OpenMode mode )
	{
		return mode == OpenMode::READING_ONLY || mode == OpenMode::MODIFICATION || mode == OpenMode::INSPECTION;
	}

	constexpr bool OpenModePermitsWriting( OpenMode mode )
	{
		return mode == OpenMode::WRITING_ONLY || mode == OpenMode::MODIFICATION;
	}

	constexpr ::compression::ECompressionType DefaultCompressionType = ::compression::c_defaultQuickCompressionType;

#pragma pack(push, 4)

	struct FileSpan
	{
		Uint64 position;
		Uint32 size;

		constexpr FileSpan() 
			: position( 0ull )
			, size( 0 )
		{}

		constexpr FileSpan( Uint64 p, Uint32 s )
			: position( p )
			, size( s )
		{}
	};

	struct FileSegment
	{
		Uint64 position;
		Uint32 sizeOnDisk;
		Uint32 sizeInMemory;

		constexpr FileSegment()
			: position( 0ull )
			, sizeOnDisk( 0u )
			, sizeInMemory( 0u )
		{}

		constexpr FileSegment( Uint64 pos, Uint32 disk, Uint32 mem )
			: position( pos )
			, sizeOnDisk( disk )
			, sizeInMemory( mem )
		{}

		constexpr bool IsCompressed() const
		{
			return sizeOnDisk != sizeInMemory;
		}
	};

	struct FileHash
	{
		static constexpr Uint32 NUM_BYTES = 20;
		Uint8 value[NUM_BYTES];
	};

#pragma pack(pop)

	RED_INLINE bool operator == ( const FileHash& left, const FileHash& right )
	{
		return red::Memcmp( left.value, right.value, FileHash::NUM_BYTES ) == 0;
	}

	RED_INLINE Uint32 GetHash( const FileHash& value )
	{
		return red::CalculateHash32( value.value, FileHash::NUM_BYTES );
	}

	RED_INLINE bool operator == ( const FileSpan& left, const FileSpan& right )
	{
		return left.position == right.position && left.size == right.size;
	}

	RED_INLINE bool LessPosition( const FileSpan& left, const FileSpan& right )
	{
		return left.position < right.position;
	}

	RED_INLINE bool LessSize( const FileSpan& left, const FileSpan& right )
	{
		return left.size < right.size;
	}

	RED_MEMORY_POOL( PoolArchive, red::memory::DefaultAllocator, ARCHIVE_API );

} // namespace archive

#ifdef RED_CONFIGURATION_FINAL
#define RED_READONLY_ARCHIVES
#endif
