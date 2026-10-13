/*
 * Copyright (c) 2017 CD Projekt Red. All Rights Reserved.
 */

#include "build.h"
#include "dependencyLooseFileDatabase.h"

#include "dependencyJsonReader.h"
#include "dependencyJsonWriter.h"

#include "../../../common/redFileSystem/include/fileSys.h"
#include "../../../common/redSystem/include/stopWatch.h"

namespace dependency
{

void CreateHiddenDatabaseDirectory( const red::AbsolutePath& path )
{
	if ( GFileManager->CreatePath( path ) )
	{
		auto wcharPath = path.ToUtf16String();
		DWORD attr = ::GetFileAttributesW( wcharPath.AsChar() );

		if ( ( attr & FILE_ATTRIBUTE_HIDDEN ) == 0 )
		{
			if ( ::SetFileAttributesW( wcharPath.AsChar(), attr | FILE_ATTRIBUTE_HIDDEN ) != 0 )
			{
				RED_LOG_WARNING( "Failed to set the dependency database directory %hs to hidden", path.AsChar() );
			}
		}
	}
}

//------------------------------------------------------------------------------

const char* LooseFileDatabase::DefaultDatabaseName()
{
	return "_dependencies\\";
}

LooseFileDatabase::LooseFileDatabase( const red::AbsolutePath& path, Uint32 settingsHash )
	: m_rootPath( path )
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

LooseFileDatabase::~LooseFileDatabase()
{
	m_entryStorage.Uninitialize();

	m_metrics.Report();
}

bool LooseFileDatabase::Load()
{
	WriteLock lock( m_guard );

	// Nothing to do, we load on demand
	m_changed = false;

	return true;
}

bool LooseFileDatabase::Save()
{
	CreateHiddenDatabaseDirectory( m_rootPath );

	WriteLock lock( m_guard );

	bool allSaved = true;

	auto keys = m_map.GetKeys();
	for ( const auto& key : keys )
	{
		auto* entry = m_map.GetRef( key );
		if ( !SaveEntry( key, entry ) )
		{
			allSaved = false;
		}
	}

	m_changed = !allSaved;

	return allSaved;
}

void LooseFileDatabase::Clear()
{
	WriteLock lock( m_guard );
	m_map.Clear();
	m_entryStorage.Reset();
	m_changed = true; // Since it no longer represents what is on disk

	m_metrics.Reset();
}

bool LooseFileDatabase::HasChanged()
{
	ReadLock lock( m_guard );
	return m_changed;
}

void LooseFileDatabase::SetResourceInfo( const ResourceInfo& info )
{
	Key key = info.GetPath();
	Entry* entry = nullptr;
	{
		WriteLock lock( m_guard );
		entry = RED_NEW( Entry, m_entryStorage ) { info, 0ull };
		m_map.Set( key, entry );
	}

	// Save the entry each time for now
	if ( !SaveEntry( key, entry ) )
	{
		m_changed = true;
	}
}

void LooseFileDatabase::ClearResourceInfo( const res::ResourcePath& resourcePath )
{
	WriteLock lock( m_guard );

	m_map.Remove( resourcePath );
	m_changed = true;
}

const ResourceInfo* LooseFileDatabase::GetResourceInfo( const res::ResourcePath& resourcePath )
{
	EntryMap::iterator iter = m_map.End();

	{
		ReadLock lock( m_guard );
		iter = m_map.Find( resourcePath );
	}

	if ( iter == m_map.End() )
	{
		WriteLock lock( m_guard );
		// Lock and find again just in case something else loaded it between the first find and now
		iter = m_map.Find( resourcePath );
		if ( iter == m_map.End() )
		{
			red::AbsolutePath path = ComputePath( resourcePath );

			Uint64 timestamp = GFileManager->GetFileTimeStamp( path );
			ResourceInfo info;
			if ( !ReadFile( path, info ) )
			{
				return nullptr;
			}

			Entry* entry = RED_NEW( Entry, m_entryStorage ){ info, timestamp };
			auto result = m_map.Set( resourcePath, entry );
			iter = result.Iterator();
		}
	}

	return &(iter.Value()->info);
}

const ResourceInfo* LooseFileDatabase::GetResourceInfo( const res::ResourcePath& resourcePath ) const
{
	ReadLock lock( m_guard );

	EntryMap::const_iterator iter = m_map.Find( resourcePath );

	if ( iter != m_map.End() )
	{
		return &(iter.Value()->info);
	}

	return nullptr;
}

red::DynArray< const ResourceInfo* > LooseFileDatabase::GetAllResourceInfos() const
{
	ReadLock lock( m_guard );

	red::DynArray< const ResourceInfo* > result;
	result.Reserve( m_map.Size() );

	for ( const auto iter: m_map )
	{
		result.PushBack( &(iter.Value()->info) );
	}

	return result;
}

bool LooseFileDatabase::SaveEntry( const Key& key, Entry* entry )
{
	if ( entry->timestamp == 0ull )
	{
		const red::AbsolutePath path = ComputePath( key );

		if ( !WriteFile( path, entry->info ) )
		{
			RED_LOG_ERROR( "Failed to write dependency file %hs for %hs", path.AsChar(), key.ToDebugString() );
			return false;
		}

		entry->timestamp = GFileManager->GetFileTimeStamp( path );
	}
	return true;
}

red::AbsolutePath LooseFileDatabase::ComputePath( const res::ResourcePath& resourcePath ) const
{
	const Uint64 resourceHash = resourcePath.GetHash();

	String filePath = String::Printf( "%.2x\\%.2x\\%.16llx.deps",
		static_cast<Uint8>( resourceHash >> 56 ), static_cast<Uint8>( resourceHash >> 48 ), resourceHash );

	return m_rootPath.AddFilePath( filePath );
}

bool LooseFileDatabase::WriteFile( const red::AbsolutePath& path, const ResourceInfo& info ) const
{
	red::StopWatch timer;

	WriterScope writer = CreateJsonWriter( path );

	writer->WriteNumber( "version", ResourceInfo::CURRENT_VERSION );

	bool result = info.Save( *writer );

	m_metrics.FileSaved( timer.GetDeltaMS() );

	return result;
}

bool LooseFileDatabase::ReadFile( const red::AbsolutePath& path, ResourceInfo& info ) const
{
	bool result = false;
	if ( GFileManager->FileExist( path ) )
	{
		red::StopWatch timer;

		ReaderScope reader = CreateJsonReader( path );

		if ( !reader->HasErrors() )
		{
			Uint32 version = 0;
			if ( reader->HasValue( "version" ) )
			{
				reader->ReadNumber( "version", version );
			}
			if ( version == ResourceInfo::CURRENT_VERSION )
			{
				result = info.Load( *reader );
			}
			else
			{
				RED_LOG_ERROR( "Attempting to load incomaptible version of dependencies file. Loading: %u expected: %u", version, ResourceInfo::CURRENT_VERSION );
			}
		}

		m_metrics.FileLoaded( timer.GetDeltaMS() );

		if ( !result )
		{
			GFileManager->DeleteFile( path );
		}
	}

	return result;
}

//------------------------------------------------------------------------------

LooseFileDatabase::Metrics::Metrics()
	: m_filesLoaded( 0 )
	, m_filesSaved( 0 )
	, m_loadingTimeMs( 0.0 )
	, m_savingTimeMs( 0.0 )
{
}

void LooseFileDatabase::Metrics::Reset()
{
	m_filesLoaded = 0;
	m_filesSaved = 0;
	m_loadingTimeMs = 0.0;
	m_savingTimeMs = 0.0;
}

void LooseFileDatabase::Metrics::FileLoaded( double deltaMs )
{
	RED_SCOPE_LOCK( m_guard );
	m_filesLoaded++;
	m_loadingTimeMs += deltaMs;
}

void LooseFileDatabase::Metrics::FileSaved( double deltaMs )
{
	RED_SCOPE_LOCK( m_guard );
	m_filesSaved++;
	m_savingTimeMs += deltaMs;
}

void LooseFileDatabase::Metrics::Report()
{
	RED_LOG( "LooseDependencyDatabase: Saved '%u' files took %0.3lf ms", m_filesSaved, m_savingTimeMs );
	RED_LOG( "LooseDependencyDatabase: Loaded '%u' files took %0.3lf ms", m_filesLoaded, m_loadingTimeMs );
}

} // dependency
