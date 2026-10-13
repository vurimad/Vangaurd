/*
 * Copyright (c) 2017 CD Projekt Red. All Rights Reserved.
 */

#include "build.h"
#include "dependencyBinaryDatabase.h"

#include "dependencyBinaryWriter.h"
#include "dependencyBinaryReader.h"

#include "../../../common/redFileSystem/include/fileSys.h"
#include "../../../common/redSystem/include/stopWatch.h"

#include "../../../common/archive/include/common.h" // For archive::FileHash

namespace dependency
{

//------------------------------------------------------------------------------

const char* BinaryDatabase::DefaultDatabaseName()
{
	return "dependencies.binary_deps";
}

BinaryDatabase::BinaryDatabase( const red::AbsolutePath& path, Uint32 settingsHash )
	: m_path( path )
	, m_settingsHash( settingsHash )
	, m_changed( false )
{
	red::memory::DynamicLinearAllocatorParameter param{ 
		&red::memory::AcquireSystemAllocator(),
		RED_KILO_BYTE( 64 ),
		red::memory::Flags::Flags_CPU_Read_Write 
	};
	m_entryStorage.Initialize( param );
}

BinaryDatabase::~BinaryDatabase()
{
	m_entryStorage.Uninitialize();
}

bool BinaryDatabase::Load()
{
	WriteLock lock( m_guard );

	red::StopWatch timer;

	red::Blob fileBuffer;
	if ( !red::LoadFileToBuffer( m_path, fileBuffer ) )
	{
		RED_LOG_ERROR( "BinaryDependencyDatabase: Could not open the binary dependencies file for reading '%hs'", m_path.AsChar() );
		return false;
	}

	if ( fileBuffer.Size() < sizeof( prv::fileformat::Header ) )
	{
		RED_LOG_ERROR( "BinaryDependencyDatabase: Invalid binary dependencies file '%hs'", m_path.AsChar() );
		return false;
	}

	const auto& header = fileBuffer.As< const prv::fileformat::Header >();
	if ( header.magic != prv::fileformat::Header::MAGIC_VALUE || 
		header.entriesIndexOffset + header.entriesIndexSize > fileBuffer.Size() ||
		header.entriesCount * sizeof( prv::fileformat::IndexEntry ) > header.entriesIndexSize ||
		header.entriesOffset + header.entriesSize > fileBuffer.Size() ||
		header.stringIndexOffset + header.stringIndexSize > fileBuffer.Size() ||
		header.stringsCount * sizeof( prv::fileformat::IndexEntry ) > header.stringIndexSize ||
		header.stringDataOffset + header.stringDataSize > fileBuffer.Size() )
	{
		RED_LOG_ERROR( "BinaryDependencyDatabase: Invalid binary dependencies file '%hs'", m_path.AsChar() );
		return false;
	}
	if ( header.version != ResourceInfo::CURRENT_VERSION )
	{
		RED_LOG_ERROR( "BinaryDependencyDatabase: Attempting to load out of date binary dependencies file '%hs'. Loading: %u expected: %u", m_path.AsChar(), header.version, ResourceInfo::CURRENT_VERSION );
		return false;
	}

	if ( header.entriesCount == 0 )
	{
		RED_LOG_WARNING( "BinaryDependencyDatabase: No dependencies stored in binary dependencies file '%hs'", m_path.AsChar() );
		return true;
	}

	RED_LOG( "BinaryDependencyDatabase: Reading depenendencies from disk took %0.3lf ms", timer.GetDeltaMS() );
	timer.Reset();

	// TODO: Not sure I need this string index "map" because as long as strings in the data buffer are null terminated
	// then just storing the offset and scanning forward will be fast and good enough
	const auto* stringsIndexPtr = fileBuffer.Pointer< const prv::fileformat::IndexEntry >( header.stringIndexOffset );
	red::ArraySpan< const prv::fileformat::IndexEntry > stringEntries{ stringsIndexPtr, static_cast< Uint32 >( header.stringIndexSize / sizeof(prv::fileformat::IndexEntry) ) };
	red::BlobView stringData = fileBuffer.View( header.stringDataOffset, header.stringDataSize );
	prv::StringsUnmapper stringsUnmapper( stringEntries, stringData );

	m_map.Reserve( header.entriesCount );

	const auto* entriesIndexPtr = fileBuffer.Pointer< const prv::fileformat::IndexEntry >( header.entriesIndexOffset );
	red::ArraySpan< const prv::fileformat::IndexEntry > indexEntries{ entriesIndexPtr, static_cast< Uint32 >( header.entriesIndexSize / sizeof(prv::fileformat::IndexEntry) ) };
	red::BlobView entriesData = fileBuffer.View( header.entriesOffset, header.entriesSize );

	for ( const auto& indexEntry : indexEntries )
	{
		prv::BinaryReadContext readContext( entriesData.Range( indexEntry.offset, indexEntry.size ) );
		auto binaryReader = prv::CreateBinaryReader( stringsUnmapper, readContext );

		ResourceInfo* info = RED_NEW( ResourceInfo, m_entryStorage )();
		if ( !info->Load( *binaryReader ) )
		{
			RED_LOG_ERROR( "BinaryDependencyDatabase: Failed to read ResourceInfo entry in binary dependencies file '%hs'", m_path.AsChar() );
			return false;
		}
		m_map.Set( info->GetPath(), info );
	}

	RED_LOG( "BinaryDependencyDatabase: Processing depenendencies took %0.3lf ms", timer.GetDeltaMS() );

	m_changed = false;
	return true;
}

bool BinaryDatabase::Save()
{
	WriteLock lock( m_guard );

	if ( !m_changed )
	{
		RED_LOG( "BinaryDependencyDatabase: Skipped saving '%hs', nothing has changed.", m_path.AsChar() );
		return true;
	}

	auto fileWriter = GFileManager->CreateFileWriter( m_path, FOF_AbsolutePath );
	if ( !fileWriter )
	{
		RED_LOG_ERROR( "BinaryDependencyDatabase: Could not open the binary dependencies file for writing '%hs'", m_path.AsChar() );
		return false;
	}

	// TODO: Move this into jobs
	bool allSaved = true;

	red::StopWatch timer;

	red::DynArray< ResourceInfo* > resourceInfos = m_map.GetValues();
	red::DynArray< prv::MappingContext > mappingContexts;
	mappingContexts.Resize( resourceInfos.Size() );

	for ( auto index : resourceInfos.Indices() )
	{
		const ResourceInfo* info = resourceInfos[ index ];
		auto& context = mappingContexts[ index ];
		auto mappingWriter = prv::CreateMappingWriter( context );
		if ( ! info->Save( *mappingWriter ) )
		{
			RED_LOG_ERROR( "BinaryDependencyDatabase: Failed to map info for '%hs'", info->GetPath().ToDebugString() );
			allSaved = false;
		}
	}

	RED_LOG( "BinaryDependencyDatabase: Collecting strings in resource dependencies took %0.3lf ms", timer.GetDeltaMS() );
	timer.Reset();

	// We want a map of <string data> -> <offset in buffer>
	prv::StringsMapping stringsMapping;
	stringsMapping.AddCollectedStrings( mappingContexts );

	red::DynArray< prv::fileformat::IndexEntry > indexEntries;
	indexEntries.Resize( mappingContexts.Size() );

	Uint64 totalMemoryRequired = 0;
	for ( auto index : mappingContexts.Indices() )
	{
		const auto& mappingContext = mappingContexts[ index ];
		auto& indexEntry = indexEntries[ index ];
		indexEntry.offset = static_cast< Uint32 >( totalMemoryRequired );
		indexEntry.size = mappingContext.m_memoryRequired;
		totalMemoryRequired += mappingContext.m_memoryRequired;
	}

	RED_FATAL_ASSERT( totalMemoryRequired < std::numeric_limits< Uint32 >::max(), "Attempting to sue too much memory when saving dependencies %llu", totalMemoryRequired );

	RED_LOG( "BinaryDependencyDatabase: Resolving strings into buffer and computing indexes took %0.3lf ms", timer.GetDeltaMS() );
	timer.Reset();

	red::Blob buffer( static_cast< Uint32 >( totalMemoryRequired ) );

	for ( auto index : resourceInfos.Indices() )
	{
		const ResourceInfo* info = resourceInfos[ index ];
		const auto& indexEntry = indexEntries[ index ];
		prv::BinaryWriteContext writeContext( buffer.Span( indexEntry.offset, indexEntry.size ) );
		auto binaryWriter = prv::CreateBinaryWriter( stringsMapping, writeContext );
		if ( ! info->Save( *binaryWriter ) )
		{
			allSaved = false;
		}
	}

	RED_LOG( "BinaryDependencyDatabase: Saving dependencies into binary format took %0.3lf ms", timer.GetDeltaMS() );
	timer.Reset();

	const auto& stringsIndex = stringsMapping.GetStringsIndex();
	const auto stringsBuffer = stringsMapping.GetStringsBuffer();

	prv::fileformat::Header header;
	header.magic = prv::fileformat::Header::MAGIC_VALUE;
	header.version = ResourceInfo::CURRENT_VERSION;

	header.entriesCount = indexEntries.Size();
	header.entriesIndexOffset = sizeof( prv::fileformat::Header );
	header.entriesIndexSize = header.entriesCount * sizeof( prv::fileformat::IndexEntry );
	RED_FATAL_ASSERT( header.entriesIndexSize == indexEntries.DataSize() );
	header.entriesOffset = header.entriesIndexOffset + header.entriesIndexSize;
	header.entriesSize = buffer.Size();

	header.stringsCount = stringsIndex.Size();
	header.stringIndexOffset = header.entriesOffset + header.entriesSize;
	header.stringIndexSize = header.stringsCount * sizeof( prv::fileformat::IndexEntry );
	RED_FATAL_ASSERT( header.stringIndexSize == stringsIndex.DataSize() );
	header.stringDataOffset = header.stringIndexOffset + header.stringIndexSize;
	header.stringDataSize = stringsBuffer.Size();

	fileWriter->Serialize( &header, sizeof( prv::fileformat::Header ) );
	RED_FATAL_ASSERT( header.entriesIndexOffset == fileWriter->GetOffset() );
	fileWriter->Serialize( indexEntries.Data(), indexEntries.DataSize() );
	RED_FATAL_ASSERT( header.entriesOffset == fileWriter->GetOffset() );
	fileWriter->Serialize( buffer.Data(), buffer.Size() );
	RED_FATAL_ASSERT( header.stringIndexOffset == fileWriter->GetOffset() );
	fileWriter->Serialize( const_cast< void* >( stringsIndex.Data() ), stringsIndex.DataSize() );
	RED_FATAL_ASSERT( header.stringDataOffset == fileWriter->GetOffset() );
	fileWriter->Serialize( const_cast< void* >( stringsBuffer.Data() ), stringsBuffer.Size() );
	// NOTE: Have to const cast here even though the serialiser won't modify the data because of bad design

	fileWriter.Reset(); // Flush to disk

	RED_LOG( "BinaryDependencyDatabase: Writing depenendencies to disk took %0.3lf ms", timer.GetDeltaMS() );

	m_changed = ! allSaved;
	return allSaved;
}

void BinaryDatabase::Clear()
{
	WriteLock lock( m_guard );
	m_map.Clear();
	m_entryStorage.Reset();
	m_changed = false; // No need to serialise empty file
}

bool BinaryDatabase::HasChanged()
{
	ReadLock lock( m_guard );
	return m_changed;
}

void BinaryDatabase::SetResourceInfo( const ResourceInfo& info )
{
	res::ResourcePath key = info.GetPath();
	ResourceInfo* entry = nullptr;
	{
		WriteLock lock( m_guard );
		entry = RED_NEW( ResourceInfo, m_entryStorage ) { info };
		m_map.Set( key, entry );
	}
	m_changed = true;
}

void BinaryDatabase::ClearResourceInfo( const res::ResourcePath& resourcePath )
{
	WriteLock lock( m_guard );
	m_map.Remove( resourcePath );
	m_changed = true;
}

const ResourceInfo* BinaryDatabase::FindEntry( const res::ResourcePath& resourcePath ) const
{
	ReadLock lock( m_guard );
	EntryMap::const_iterator iter = m_map.Find( resourcePath );

	if ( iter != m_map.End() )
	{
		return iter.Value();
	}
	else
	{
		return nullptr;
	}
}

const ResourceInfo* BinaryDatabase::GetResourceInfo( const res::ResourcePath& resourcePath )
{
	return FindEntry( resourcePath );
}

const ResourceInfo* BinaryDatabase::GetResourceInfo( const res::ResourcePath& resourcePath ) const
{
	return FindEntry( resourcePath );
}

red::DynArray< const ResourceInfo* > BinaryDatabase::GetAllResourceInfos() const
{
	ReadLock lock( m_guard );
	return m_map.GetValues();
}

//------------------------------------------------------------------------------

} // dependency
