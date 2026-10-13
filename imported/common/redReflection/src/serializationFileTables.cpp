/**
* Copyright (c) 2026 RED Vanguard, All Rights Reserved.
*/

#include "build.h"
#include "../../../common/redSystem/include/crc.h"
#include "../../../common/redFileSystem/include/fileVersionList.h"
#include "../../../common/redFileSystem/include/file.h"
#include "version.h"
#include "serializationFileTables.h"

using red::DynArray;

namespace serialization
{

	const Uint32 FileTables::FILE_MAGIC = 'W2RC';
	const Uint32 FileTables::FILE_VERSION = VER_CURRENT;

	#ifndef RED_CONFIGURATION_FINAL
		#define DEBUG_MEM_ZERO( x ) red::Memzero( &(x), sizeof(x) )
	#else
		#define DEBUG_MEM_ZERO( x ) 
	#endif

	namespace Helpers
	{
		// helper for loading data table with validation
		template< typename T >
		static Bool LoadChunkData( IFile& file, DynArray< T >& outData, const FileTables::Chunk& chunk, const Uint64 baseOffset )
		{
			if ( chunk.m_count )
			{
				outData.Resize( chunk.m_count );

				file.Seek( baseOffset + chunk.m_offset );
				file.Serialize( outData.Data(), outData.DataSize() );

	#ifndef RED_CONFIGURATION_FINAL
				// compute buffer CRC
				const Uint32 crcValue = red::CalculateCRC32( outData.Data(), outData.DataSize() );

				// validate data by computing CRC of the loaded buffer
				if ( chunk.m_crc != crcValue )
				{
					RED_LOG_ERROR( "Core: !!! FILE CORRUPTION !!!" );
					RED_LOG_ERROR( "Core: Chunk CRC mismatch in '%hs'", file.GetFileNameForDebug() );
					return false;
				}
	#endif
			}

			// valid
			return true;
		}

		// helper for saving data table
		template< typename T >
		static void SaveChunkData( IFile& file, const DynArray< T >& data, FileTables::Chunk& outChunk, const Uint64 baseOffset )
		{
			if ( data.Empty() )
			{
				outChunk.m_count = 0;
				outChunk.m_crc = 0;
				outChunk.m_offset = 0;
			}
			else
			{
				// setup chunk
				outChunk.m_offset = static_cast< Uint32 >( file.GetOffset() - baseOffset );
				outChunk.m_count = data.Size();

				// compute CRC of the saved data
				outChunk.m_crc = red::CalculateCRC32( data.Data(), data.DataSize() );	

				// save data
				file.Serialize( (void*)data.Data(), data.DataSize() );
			}
		}
	}

	FileTables::FileTables()
		: m_strings( red::PoolEngine() )
		, m_names( red::PoolEngine() )
		, m_imports( red::PoolEngine() )
		, m_exports( red::PoolEngine() )
		, m_properties( red::PoolEngine() )
		, m_buffers( red::PoolEngine() )
		, m_inplace( red::PoolEngine() )
		, m_softImportBase( 0 )
	{
	}

	void FileTables::Save( IFile& file, const Uint64 headerOffset ) const
	{
		PC_SCOPE( SaveFileData );

		// write placeholder header
		Header header;
		red::Memzero( &header, sizeof(header) );

		// save the initial header
		{
			DEBUG_MEM_ZERO( header );
			file.Seek( headerOffset );
			file.Serialize( &header, sizeof(header) );
		}

		// setup header identification data
		header.m_version = file.GetVersion();
		header.m_magic = FILE_MAGIC;
		header.m_gpuSize = m_gpuSize;

		// save data tables
		header.m_numChunks = 6; // keep up to date
		Helpers::SaveChunkData( file, m_strings, header.m_chunks[ eChunkType_Strings ], headerOffset );
		Helpers::SaveChunkData( file, m_names, header.m_chunks[ eChunkType_Names ], headerOffset );
		Helpers::SaveChunkData( file, m_imports, header.m_chunks[ eChunkType_Imports ], headerOffset );
		Helpers::SaveChunkData( file, m_properties, header.m_chunks[ eChunkType_Properties ], headerOffset );
		Helpers::SaveChunkData( file, m_exports, header.m_chunks[ eChunkType_Exports ], headerOffset );
		Helpers::SaveChunkData( file, m_buffers, header.m_chunks[ eChunkType_Buffers ], headerOffset );
		Helpers::SaveChunkData( file, m_inplace, header.m_chunks[ eChunkType_InplaceData ], headerOffset );	

		// compute the size of the object data (preloaded)
		header.m_objectsEnd = (Uint32)( file.GetOffset() - headerOffset );
		for ( const Export& e : m_exports )
		{
			const Uint32 endOffset = e.m_dataOffset + e.m_dataSize;
			header.m_objectsEnd = math::Max< Uint32 >( header.m_objectsEnd, endOffset );
		}

		// compute the end position of the buffered data (preloaded)
		header.m_buffersEnd = header.m_objectsEnd;
		for ( const Buffer& b : m_buffers )
		{
			// only locally stored data
			if ( b.m_dataOffset )
			{
				const Uint32 endOffset = b.m_dataOffset + b.m_dataSizeOnDisk;
				header.m_buffersEnd = math::Max< Uint32 >( header.m_buffersEnd, endOffset );
			}
		}

		// compute header CRC
		header.m_crc = CalcHeaderCRC( header );

		// save it again, preserve file offset at the end of the data
		const Uint64 currentOffset = file.GetOffset();
		file.Seek( headerOffset );
		file.Serialize( &header, sizeof(header) );
		file.Seek( currentOffset );
	}

	Bool FileTables::Load( IFile& file, Uint32& outVersion )
	{
		PC_SCOPE( LoadFileData );

		// file is to small to load data
		if ( file.GetSize() < sizeof(Header) )
		{
			RED_LOG_WARNING( "Core: File '%hs' is to small to contain any serialized data", file.GetFileNameForDebug() );
			return false;
		}

		// load header preamble
		Header header;
		DEBUG_MEM_ZERO( header );
		const Uint64 baseOffset = file.GetOffset();
		file.Serialize( &header, sizeof(Header) );

		if ( !CheckHeader( file.GetFileNameForDebug(), header, outVersion ) )
		{
			RED_LOG_ERROR( "Core: Header validation failed for file '%hs'", file.GetFileNameForDebug() );
			return false;
		}

		// create the tables using the chunk information and load chunk data
		if ( !Helpers::LoadChunkData( file, m_strings, header.m_chunks[ eChunkType_Strings ], baseOffset ) ) return false;
		if ( !Helpers::LoadChunkData( file, m_names, header.m_chunks[ eChunkType_Names ], baseOffset ) ) return false;
		if ( !Helpers::LoadChunkData( file, m_imports, header.m_chunks[ eChunkType_Imports ], baseOffset ) ) return false;
		if ( !Helpers::LoadChunkData( file, m_exports, header.m_chunks[ eChunkType_Exports ], baseOffset ) ) return false;
		if ( !Helpers::LoadChunkData( file, m_buffers, header.m_chunks[ eChunkType_Buffers ], baseOffset ) ) return false;
		if ( !Helpers::LoadChunkData( file, m_inplace, header.m_chunks[ eChunkType_InplaceData ], baseOffset ) ) return false;

		m_gpuSize = header.m_gpuSize;

		// check for data overrides
	#ifndef RED_CONFIGURATION_FINAL
		{
			const Uint64 minOffset = (file.GetOffset() - baseOffset);
			Uint64 curOffset = minOffset;

			// check exports
			for ( Uint32 i=0; i<m_exports.Size(); ++i )
			{
				if ( m_exports[i].m_dataOffset != curOffset )
				{
					RED_LOG_ERROR( "Core: !!! FILE CORRUPTION !!!" );
					RED_LOG_ERROR( "Core: Export %d at offset %d aliased with previous data (overlap: %d) in '%hs'", 
						i, m_exports[i].m_dataOffset, curOffset - m_exports[i].m_dataOffset, file.GetFileNameForDebug() );

					return false;
				}

				curOffset += m_exports[i].m_dataSize;
			}

			// validate header inter-offset
			if ( curOffset != header.m_objectsEnd )
			{
				RED_LOG_ERROR( "Core: !!! FILE CORRUPTION !!!" );
				RED_LOG_ERROR( "Core: Size of the object data: %d, expected %d in '%hs'", 
					curOffset, header.m_objectsEnd, file.GetFileNameForDebug() );

				return false;
			}

			// check buffers
			for ( Uint32 i=0; i<m_buffers.Size(); ++i )
			{
				if ( !m_buffers[i].m_dataOffset )
					continue;

				if ( m_buffers[i].m_dataOffset != curOffset )
				{
					RED_LOG_ERROR( "Core: !!! FILE CORRUPTION !!!" );
					RED_LOG_ERROR( "Core: Buffer %d at offset %d aliased with previous data (overlap: %d) in '%hs'", 
						i, m_buffers[i].m_dataOffset, curOffset - m_buffers[i].m_dataOffset, file.GetFileNameForDebug() );

					return false;
				}

				curOffset += m_buffers[i].m_dataSizeOnDisk;
			}
		}
	#endif

		return true;
	}

	Bool FileTables::CheckHeader( const char* fileNameForDebug, const Header& header, Uint32& outVersion ) const
	{
		const char* const sanitizedFileName = fileNameForDebug ? fileNameForDebug : "<Unknown file>";

		// validate header and magic
		if ( header.m_magic != FILE_MAGIC )
		{
			RED_LOG_WARNING( "Core: File %s: magic is invalid (%08X != %08X), file is not a resource.", sanitizedFileName, header.m_magic, FILE_MAGIC );
			return false;
		}

		// validate header version - should not be older than the CRC version
		if ( header.m_version > VER_CURRENT )
		{
			RED_LOG_WARNING( "Core: File %s: version is invalid (%d > %d), file is from newer version of the engine.", sanitizedFileName, header.m_version, VER_CURRENT );
			return false;
		}

	#ifndef RED_CONFIGURATION_FINAL
		// calculate current header CRC
		CRCValue crc = CalcHeaderCRC( header );
		if ( header.m_crc && crc != header.m_crc )
		{
			RED_LOG_ERROR( "Core: file: %s !!! FILE CORRUPTION !!!", sanitizedFileName );
			return false;
		}
	#endif

		// done
		outVersion = header.m_version;

		return true;
	}

	FileTables::CRCValue FileTables::CalcHeaderCRC( const Header& header )
	{
		// tricky bit - the CRC field cannot be included in the CRC calculation - calculate the CRC without it
		Header tempHeader;
		tempHeader = header;
		tempHeader.m_crc = 0xDEADBEEF; // special hacky stuff

		return red::CalculateCRC32( &tempHeader, sizeof(tempHeader) );
	}

} // serialization