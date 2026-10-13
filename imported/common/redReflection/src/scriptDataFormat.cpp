/**
* Copyright (c) 2026 RED Vanguard, All Rights Reserved.
*/
#include "build.h"
#include "../../redSystem/include/crc.h"
#include "scriptDataFormat.h"
#include "version.h"
#include "../../redFileSystem/include/file.h"

using red::DynArray;

namespace Helpers
{
	// helper for loading compressed data table with validation
	// it's relatively safe
	template< typename T >
	static Bool LoadScriptsChunkData( IFile& file, DynArray< T >& outData, const CScriptDataFormat::Chunk& chunk, const Uint64 baseOffset )
	{
		// no data in the chunk
		if ( !chunk.m_count )
			return true;

		// allocate space
		outData.Resize( chunk.m_count );

		// load data
		file.Seek( baseOffset + chunk.m_offset );
		file.Serialize( outData.Data(), outData.DataSize() );

		// compute buffer CRC - we always validate this for the scripts
		const Uint32 crcValue = red::CalculateCRC32( outData.Data(), outData.DataSize() );

		// validate data by computing CRC of the loaded buffer
		if ( chunk.m_crc != crcValue )
		{
			RED_LOG_ERROR( "Core: !!! FILE CORRUPTION !!!" );
			RED_LOG_ERROR( "Core: Chunk CRC mismatch in '%hs'", file.GetFileNameForDebug() );
			return false;
		}

		// valid
		return true;
	}

	// helper for saving data table with compression
	template< typename T >
	static void SaveScriptChunkData( IFile& file, const DynArray< T >& data, CScriptDataFormat::Chunk& outChunk, const Uint64 baseOffset )
	{
		// no data
		if ( data.Empty() )
		{
			outChunk.m_crc = 0;
			outChunk.m_count = 0;
			outChunk.m_offset = 0;
			return;
		}

		// setup chunk
		outChunk.m_offset = static_cast< Uint32 >( file.GetOffset() - baseOffset );
		outChunk.m_count = data.Size();

		// compute CRC of the saved data
		outChunk.m_crc = red::CalculateCRC32( data.Data(), data.DataSize() );

		// store source data
		file.Serialize( (void*)data.Data(), data.DataSize() );
	}
}

const Uint32 CScriptDataFormat::FILE_MAGIC = 'SDER';
const Uint32 CScriptDataFormat::FILE_VERSION = 13;

CScriptDataFormat::CScriptDataFormat()
	: m_strings( red::PoolScript() )
	, m_names( red::PoolScript() )
	, m_tweakDBIDIs( red::PoolScript() )
	, m_resRefs( red::PoolScript() )
	, m_objects( red::PoolScript() )
{
}

CScriptDataFormat::~CScriptDataFormat()
{
}

Bool CScriptDataFormat::ValidateHeader( IFile& file )
{
	// base data offset (all header offsets are relative to this)
	const Uint64 baseOffset = (Uint32) file.GetOffset();

	// size of the header preamble
	const Uint32 preamble = sizeof(Uint32) * 3;

	// load header preamble
	Header header;
	red::Memzero( &header, sizeof( header ) );
	file.Serialize( &header, preamble );

	// read rest of the header
	file.Serialize( (Uint8*)&header + preamble, sizeof( header ) - preamble );

	// calculate current header CRC
	const CRCValue crc = CalcHeaderCRC( header );

	return header.m_magic == FILE_MAGIC && header.m_version == FILE_VERSION && crc == header.m_crc;
}

const Bool CScriptDataFormat::Load( IFile& file )
{
	// base data offset (all header offsets are relative to this)
	const Uint64 baseOffset = (Uint32) file.GetOffset();

	// size of the header preamble
	const Uint32 preamble = sizeof(Uint32) * 3;

	// load header preamble
	Header header;
	red::Memzero( &header, sizeof(header) );
	file.Serialize( &header, preamble );

	// validate header and magic
	if ( header.m_magic != FILE_MAGIC )
	{
		RED_LOG_ERROR( "Core: Script file magic is invalid (%08X != %08X), file is not a resource.", header.m_magic, FILE_MAGIC );
		return false;
	}

	// validate header version - should not be older than the CRC version
	if ( header.m_version != FILE_VERSION )
	{
		RED_LOG_WARNING( "Core: Script file version is invalid (%d != %d).", header.m_version, FILE_VERSION );
		return false;
	}

	// read rest of the header
	file.Serialize( (Uint8*)&header + preamble, sizeof(header)-preamble );

	// calculate current header CRC
	CRCValue crc = CalcHeaderCRC( header );
	if ( crc != header.m_crc )
	{
		RED_LOG_ERROR( "Core: !!! FILE CORRUPTION !!!" );
		RED_LOG_ERROR( "Core: Script file header CRC mismatch in '%hs'", file.GetFileNameForDebug() );
		return false;
	}

	// create the tables using the chunk information and load chunk data
	if ( !Helpers::LoadScriptsChunkData( file, m_strings, header.m_chunks[ (Uint32) EChunkType::Strings ], baseOffset ) ) return false;
	if ( !Helpers::LoadScriptsChunkData( file, m_names, header.m_chunks[ (Uint32) EChunkType::Names ], baseOffset ) ) return false;
	if ( !Helpers::LoadScriptsChunkData( file, m_tweakDBIDIs, header.m_chunks[ (Uint32) EChunkType::TweakDBIDs ], baseOffset ) ) return false;
	if ( !Helpers::LoadScriptsChunkData( file, m_resRefs, header.m_chunks[ (Uint32) EChunkType::ResRefs ], baseOffset ) ) return false;
	if ( !Helpers::LoadScriptsChunkData( file, m_objects, header.m_chunks[ (Uint32) EChunkType::Objects ], baseOffset ) ) return false;

	// loaded
	return true;
}

const Bool CScriptDataFormat::Save( IFile& file ) const
{
	const Uint64 baseOffset = file.GetOffset();

	// write placeholder header
	Header header;
	red::Memzero( &header, sizeof(header) );

	// save the initial header
	file.Serialize( &header, sizeof(header) );

	// setup header identification data
	header.m_version = FILE_VERSION;
	header.m_magic = FILE_MAGIC;

	// save file identification data - only when the file is saved to disk
	// never store this data when saved to memory (as a sub-part of another file)

	// get time stamp
	red::Clock::GetInstance().GetUTCTime( header.m_timeStamp );
	RED_FATAL_ASSERT( header.m_version == FILE_VERSION, "Trying to save new data using old file version (%d)", header.m_version );

	// get the build version
	header.m_buildVersion = atoi( APP_LAST_P4_CHANGE );

	// save data tables
	header.m_numChunks = 6; // keep up to date
	Helpers::SaveScriptChunkData( file, m_strings, header.m_chunks[ (Uint32) EChunkType::Strings ], baseOffset );
	Helpers::SaveScriptChunkData( file, m_names, header.m_chunks[ (Uint32) EChunkType::Names ], baseOffset );
	Helpers::SaveScriptChunkData( file, m_tweakDBIDIs, header.m_chunks[ (Uint32) EChunkType::TweakDBIDs ], baseOffset );
	Helpers::SaveScriptChunkData( file, m_resRefs, header.m_chunks[ (Uint32) EChunkType::ResRefs ], baseOffset );
	Helpers::SaveScriptChunkData( file, m_objects, header.m_chunks[ (Uint32) EChunkType::Objects ], baseOffset );

	// compute header CRC
	header.m_crc = CalcHeaderCRC( header );

	// save it again, preserve file offset at the end of the data
	const Uint64 currentOffset = file.GetOffset();
	file.Seek( baseOffset );
	file.Serialize( &header, sizeof(header) );
	file.Seek( currentOffset );

	// saved
	return true;
}

CScriptDataFormat::CRCValue CScriptDataFormat::CalcHeaderCRC( const Header& header )
{
	// tricky bit - the CRC field cannot be included in the CRC calculation - calculate the CRC without it
	Header tempHeader;
	tempHeader = header;
	tempHeader.m_crc = 0xDEADBEEF; // special hacky stuff

	return red::CalculateCRC32( &tempHeader, sizeof(tempHeader) );
}

