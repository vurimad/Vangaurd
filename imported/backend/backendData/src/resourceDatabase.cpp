/*
 * Copyright (c) 2018 CD Projekt Red. All Rights Reserved.
 */

#include "build.h"
#include "resourceDatabase.h"

#include "../../../common/redSystem/include/stopWatch.h"
#include "../../../common/redFileSystem/include/fileVersionList.h"
#include "../../../common/redFileSystem/include/fileSys.h"
#include "../../../common/redReflection/include/serializationFileTables.h"
#include "../../../common/resource/include/resourceEditorDepotImpl.h"
#include "../../../common/resource/include/singleTreeResourceDepot.h"
#include "../../../common/resource/include/resourceDepotUtils.h"
#include "../../../common/redJobs2/include/jobBuilder.h"

#include "../../backendEngine/include/resourceUtils.h"
#include "../../backendEngine/include/commandletUtils.h"
#if !defined( VANGUARD_ASSET_HOST )
#include "../../backendEngine/include/resourceDatabaseProvider.h"
#endif
#include "../../backendData/include/dependencyScannerImpl.h"


#include "fileSystemFilter.h"

#include <numeric>

namespace resdb
{

//------------------------------------------------------------------------------

Resource Resource::CreateBlank( const res::ResourcePath& path )
{
	Resource result;
	result.path = path;
	result.timestamp = 0ull;
	result.status = Status::NOT_SCANNED;
	result.isLink = false;
	result.totalSize = 0;
	result.objectSize = 0;
	result.bufferSize = 0;
	return result;
}

//------------------------------------------------------------------------------

Database::Database()
	: m_depot( nullptr )
	, m_changed( false )
{
}

Database::Database( Database&& other )
	: m_depot( other.m_depot )
	, m_cachePath( other.m_cachePath )
	, m_resources( std::move( other.m_resources ) )
	, m_forwardLinks( std::move( other.m_forwardLinks ) )
	, m_backwardLinks( std::move( other.m_backwardLinks ) )
	, m_changed( other.m_changed )
{
	other.m_depot = nullptr;
	other.m_cachePath.Clear();
	other.m_changed = false;
}

Database& Database::operator=( Database&& other )
{
	if ( this != &other )
	{
		m_depot = other.m_depot;
		other.m_depot = nullptr;
		m_cachePath = std::move( other.m_cachePath );
		m_resources = std::move( other.m_resources );
		m_forwardLinks = std::move( other.m_forwardLinks );
		m_backwardLinks = std::move( other.m_backwardLinks );
		m_changed = other.m_changed;
		other.m_changed = false;
	}
	return *this;
}

Database::~Database() = default;

//------------------------------------------------------------------------------

void Database::Initialise( res::IBackendResourceDepot& depot, const red::AbsolutePath& cachePath )
{
	job::Counter counter;
	Initialise( depot, cachePath, counter );
	job::FlushCounter( counter );
}

void Database::Initialise( res::IBackendResourceDepot& depot, const red::AbsolutePath& cachePath, job::Counter& counter )
{
	m_depot = &depot;
	m_cachePath = cachePath;

	job::Builder builder;

	builder.DispatchJob( "ResourceDatabase/Load", [this]( const job::RunContext& )
	{
		if ( !m_cachePath.Empty() && GFileManager->FileExist( m_cachePath ) )
		{
			bool loaded = Load();
			if ( !loaded )
			{
				// If we failed to load this file then delete it so we don't fail the next time
				GFileManager->DeleteFile( m_cachePath );
			}
		}
	} );

	// Always do full scan after loading
	FullScan( builder );

	builder.DispatchJob( "ResourceDatabase/Save", [this]( const job::RunContext& )
	{
		if ( !m_cachePath.Empty() && m_changed )
		{
			Save();
		}

		InitialiseRecursiveLookup();
	} );

	counter += builder.ExtractWaitCounter();
}

void Database::Initialise_NoUpdate( res::IBackendResourceDepot& depot, const red::AbsolutePath& cachePath, job::Counter& counter )
{
	m_depot = &depot;
	m_cachePath = cachePath;

	job::Builder builder;

	builder.DispatchJob( "ResourceDatabase/Load", [this]( const job::RunContext& )
	{
		if ( !m_cachePath.Empty() && GFileManager->FileExist( m_cachePath ) )
		{
			bool loaded = Load();
			if ( !loaded )
			{
				// If we failed to load this file then delete it so we don't fail the next time
				GFileManager->DeleteFile( m_cachePath );
			}
		}
	} );

	counter += builder.ExtractWaitCounter();
}

void Database::Shutdown()
{
	if ( !m_cachePath.Empty() && m_changed )
	{
		Save();
	}
}

void Database::Update()
{
	job::Counter counter;
	Update( counter );
	job::FlushCounter( counter );
}

void Database::Update( job::Counter& counter )
{
	job::Builder builder;

	if ( m_watcher )
	{
		UpdateScan( builder );
	}
	else
	{
		FullScan( builder );
	}

	builder.DispatchJob( "ResourceDatabase/Save", [this]( const job::RunContext& )
	{
		if ( !m_cachePath.Empty() && m_changed )
		{
			Save();
		}
	} );

	counter += builder.ExtractWaitCounter();
}

void Database::InitFileSystemWatcher()
{
	m_watcher = red::CreateUniquePtr< FileSystemWatcher >( GFileManager, m_depot );
	GFileManager = m_watcher.Get();
}

void Database::ShutFileSystemWatcher()
{
	GFileManager = m_watcher->GetWrappedFileManager();
	m_watcher = nullptr;
}

void Database::EnqueueCommittedPaths( red::ArraySpan< const res::ResourcePath > paths )
{
	if ( !m_watcher )
	{
		return;
	}
	for ( const res::ResourcePath& path : paths )
	{
		m_watcher->PushResourcePath( path );
	}
}

//------------------------------------------------------------------------------

namespace prv
{

static bool ShouldIgnoreResource( const res::ResourcePath& path )
{
	if ( IsTransactionArtifactPath( path.ToStringView() ) )
	{
		return true;
	}

	// Ignore all files in temp as they are only local and temporary
	// NOTE: This was ignoring only the terrain directory before
	if ( path.ToStringView().StartsWith( "temp\\" ) )
	{
		return true;
	}

	return false;
}

static bool ShouldNotScanResource( const res::ResourcePath& path )
{
	if ( IsBinkResource( path ) )
	{
		return true;
	}

	if ( IsICUResource( path ) )
	{
		return true;
	}

	if ( IsAudioResource( path ) )
	{
		return true;
	}

	return false;
}

static bool ScanResource( res::IBackendResourceDepot& depot, resdb::Resource& resource, red::DynArray< resdb::Link >& links )
{
	const res::ResourcePath& resourcePath = resource.path;
	// Translating directly here so not to follow links
	const red::AbsolutePath absolutePath = depot.TranslateResourcePath( resourcePath );
	const Uint64 timestamp = GFileManager->GetFileTimeStamp( absolutePath );
	if ( timestamp == resource.timestamp )
	{
		// Nothing to scan, existing entry is up to date
		return false;
	}

	resource.timestamp = timestamp;
	if ( timestamp == 0ull )
	{
		// File doesn't exist, don't bother opening it
		resource.totalSize = resource.objectSize = resource.bufferSize = 0;
		resource.status = Resource::Status::FILE_NOT_FOUND;
		return true;
	}

	resource.isLink = red::paths::GetExtension( resourcePath.ToStringView() ) == "link";

	red::SharedPtr< IFile > file = depot.CreateResourceReader( resourcePath, false );
	if ( file == nullptr )
	{
		RED_LOG_ERROR( "ResourceDatabase: Failed to load '%hs'. Check that other processes are not modifying this file or the depot itself!", resourcePath.ToDebugString() );
		resource.totalSize = resource.objectSize = resource.bufferSize = 0;
		resource.status = Resource::Status::IO_ERROR;
		return true;
	}

	// We know the file exists, so it's only a link if the name says so
	resource.totalSize = static_cast< Uint32 >( file->GetSize() );
	resource.objectSize = resource.totalSize;
	resource.bufferSize = 0;

	if ( ShouldNotScanResource( resourcePath ) )
	{
		// Cannot scan these resources as binary but need them in the database for looking up
		resource.status = Resource::Status::NOT_A_RESOURCE;
		return true;
	}

	if ( CanBinaryScanResource( resourcePath ) )
	{
		if ( resource.totalSize >= sizeof( serialization::FileTables::Header ) )
		{
			serialization::FileTables::Header header;
			file->Serialize( &header, sizeof( serialization::FileTables::Header ) );
			file->Seek( 0ll );

			if ( header.m_magic == serialization::FileTables::FILE_MAGIC &&
				header.m_version >= VER_MINIMAL || header.m_version <= VER_CURRENT )
			{
				resource.objectSize = header.m_objectsEnd;
				resource.bufferSize = header.m_buffersEnd - header.m_objectsEnd;
			}
		}
	}

	red::DynArray< dependency::prv::ScanDependency > dependencies;
	resource.status = dependency::prv::ScanResource( file, resourcePath, dependencies );
	if ( dependency::prv::Success( resource.status ) )
	{
		for ( const auto& dep : dependencies )
		{
			links.PushBack( { resourcePath, dep.path, dep.type, dep.location, dep.inplace } );
		}
	}
	// Doing a manual remove duplicates here because Link doesn't have a default < operator
	std::sort( links.Begin(), links.End(), SortByTo );
	auto endIter = std::unique( links.Begin(), links.End() );
	links.Remove( endIter, links.End() );

	return true;
}

} // prv

struct Database::FullScanState
{
	red::DynArray< red::DynArray< Link > > allLinks;
	red::DynArray< resdb::Resource > resources;
	red::DynArray< res::ResourcePath > resourcePaths;
	red::StopWatch timer;
	red::Atomic< Bool > changed;
};

void Database::ComputeResources( FullScanStatePtr state )
{
	auto& timer = state->timer;
	auto& resourcePaths = state->resourcePaths;
	auto& resources = state->resources;
	auto& allLinks = state->allLinks;
	auto& changed = state->changed;
	auto& depot = GetResourceDepot();

	timer.Reset();

	resourcePaths = commandlet::ScanResourceDepotDirectories( depot, "" );
	{
		auto endIter = std::remove_if( resourcePaths.Begin(), resourcePaths.End(), prv::ShouldIgnoreResource );
		resourcePaths.Remove( endIter, resourcePaths.End() );
	}
	std::sort( resourcePaths.Begin(), resourcePaths.End() );

	RED_LOG( "ResourceDatabase: (FullScan) Finding and filtering resources took %1.3lf ms", timer.GetDeltaMS() );
	timer.Reset();

	resources.Reserve( resourcePaths.Size() );

	{
		auto oldIter = m_resources.Begin(), oldEnd = m_resources.End();
		auto pathIter = resourcePaths.Begin(), pathEnd = resourcePaths.End();
		while ( !( oldIter == oldEnd || pathIter == pathEnd ) )
		{
			if ( *pathIter == *oldIter )
			{
				resources.PushBack( *oldIter );
				++pathIter;
				++oldIter;
			}
			else
			{
				if ( *pathIter < *oldIter )
				{
					resources.PushBack( Resource::CreateBlank( *pathIter ) );
					++pathIter;
				}
				else // ( *oldIter < *pathIter )
				{
					++oldIter;
				}
				changed.SetValue( true );
			}
		}

		if ( oldIter != oldEnd || pathIter != pathEnd )
		{
			changed.SetValue( true );
		}

		while ( pathIter != pathEnd )
		{
			resources.PushBack( Resource::CreateBlank( *pathIter ) );
			++pathIter;
		}
	}

	allLinks.Resize( resources.Size() );

	RED_LOG( "ResourceDatabase: (FullScan) Merging resources list took %1.3lf ms", timer.GetDeltaMS() );
	timer.Reset();
}

void Database::ScanResource( FullScanStatePtr state, Uint32 index )
{
	auto& resource = state->resources[ index ];
	auto& resLinks = state->allLinks[ index ];
	auto& depot = GetResourceDepot();
	auto& changed = state->changed;

	if ( prv::ScanResource( depot, resource, resLinks ) )
	{
		changed.SetValue( true );
	}
	else
	{
		resLinks.PushBack( GetForwardLinks( resource.path ) );
	}
}

void Database::ApplyResults( FullScanStatePtr state )
{
	auto& timer = state->timer;
	auto& resourcePaths = state->resourcePaths;
	auto& resources = state->resources;
	auto& allLinks = state->allLinks;
	auto& changed = state->changed;

	RED_LOG( "ResourceDatabase: (FullScan) Scanning resources took %1.3lf ms", timer.GetDeltaMS() );
	timer.Reset();

	if ( changed.GetValue() )
	{
		m_changed = true;
	}

	m_resources = std::move( resources );
	m_resourcePaths = std::move( resourcePaths );

	Uint32 updatedLinkCount = std::accumulate( allLinks.Begin(), allLinks.End(), 0u, []( Uint32 result, const auto& resLinks ) { return result + resLinks.Size(); } );

	red::DynArray< Link > links;
	links.Reserve( updatedLinkCount );
	for ( const auto& resLinks : allLinks )
	{
		links.PushBack( resLinks );
	}

	m_forwardLinks = std::move( links );
	m_backwardLinks = m_forwardLinks;
	std::sort( m_forwardLinks.Begin(), m_forwardLinks.End(), SortByFrom );
	std::sort( m_backwardLinks.Begin(), m_backwardLinks.End(), SortByTo );
	InitialiseRecursiveLookup();

	RED_LOG( "ResourceDatabase: (FullScan) Finalising resources and link tables took %1.3lf ms", timer.GetDeltaMS() );
	timer.Reset();

	RED_FATAL_ASSERT( std::is_sorted( m_resources.Begin(), m_resources.End() ) );
	RED_FATAL_ASSERT( std::is_sorted( m_resourcePaths.Begin(), m_resourcePaths.End() ) );

	RED_LOG( "ResourceDatabase: (FullScan) Verifying sorting took %0.3lf ms", timer.GetDeltaMS() );
}

void Database::FullScan( job::Builder& builder )
{
	auto state = FullScanStatePtr::Create();

	builder.DispatchJob( "ResourceDatabase/FullScan/ScanResourceDepot", [this, state]( const job::RunContext& )
	{
		ComputeResources( state );
	} );

	builder.DispatchParallelForJob( "ResourceDatabase/FullScan/ScanResources", [state]() { return state->resources.Size(); }, [this, state]( Uint32 index, const job::RunContext& )
	{
		ScanResource( state, index );
	} );

	builder.DispatchJob( "ResourceDatabase/FullScan/ApplyResults", [this, state]( const job::RunContext& )
	{
		ApplyResults( state );
	} );
}


struct Database::IncrementalScanState
{
	red::DynArray< red::DynArray< Link > > updatedLinks;
	red::DynArray< red::ArraySpan< Link > > originalLinks;
	red::DynArray< resdb::Resource > resources;
	red::DynArray< res::ResourcePath > resourcePaths;
	red::StopWatch timer;
	red::Atomic< Bool > changed;
};

void Database::ComputeDifferences( IncrementalScanStatePtr state )
{
	auto& timer = state->timer;
	auto& resourcePaths = state->resourcePaths;
	auto& resources = state->resources;
	auto& updatedLinks = state->updatedLinks;
	auto& originalLinks = state->originalLinks;
	auto& changed = state->changed;
	auto& depot = GetResourceDepot();

	{
		auto endIter = std::remove_if( resourcePaths.Begin(), resourcePaths.End(), prv::ShouldIgnoreResource );
		std::sort( resourcePaths.Begin(), endIter );
		endIter = std::unique( resourcePaths.Begin(), endIter );
		resourcePaths.Remove( endIter, resourcePaths.End() );
	}

	if ( !resourcePaths.Empty() )
	{
		changed.SetValue( true );
	}

	resources.Reserve( resourcePaths.Size() );

	{
		auto oldIter = m_resources.Begin(), oldEnd = m_resources.End();
		auto pathIter = resourcePaths.Begin(), pathEnd = resourcePaths.End();
		while ( !( oldIter == oldEnd || pathIter == pathEnd ) )
		{
			if ( *oldIter == *pathIter )
			{
				resources.PushBack( *oldIter );
				++oldIter;
				++pathIter;
			}
			else if ( *pathIter < *oldIter )
			{
				resources.PushBack( Resource::CreateBlank( *pathIter ) );
				++pathIter;
			}
			else // *oldIter < *pathIter
			{
				++oldIter;
			}
		}

		while ( pathIter != pathEnd )
		{
			resources.PushBack( Resource::CreateBlank( *pathIter ) );
			++pathIter;
		}
	}

	updatedLinks.Resize( resources.Size() );

	RED_LOG( "ResourceDatabase: (FastScan) Finding and filtering resources took %1.3lf ms", timer.GetDeltaMS() );
	timer.Reset();

	originalLinks.Reserve( m_resources.Size() );
	
	{
		auto linkIter = m_forwardLinks.Begin(), linkEnd = m_forwardLinks.End();
		for ( const auto& resPath : m_resourcePaths )
		{
			auto startIter = linkIter;
			while ( linkIter != linkEnd && linkIter->from == resPath )
			{
				++linkIter;
			}
			auto endIter = linkIter;
			originalLinks.PushBack( red::MakeArraySpan( startIter, endIter ) );
		}
	}

	RED_LOG( "ResourceDatabase: (FastScan) Splatting links took %1.3lf ms", timer.GetDeltaMS() );
	timer.Reset();
}

void Database::ScanResource( IncrementalScanStatePtr state, Uint32 index )
{
	auto& resource = state->resources[ index ];
	auto& resLinks = state->updatedLinks[ index ];
	auto& depot = GetResourceDepot();
	auto& changed = state->changed;

	if ( prv::ScanResource( depot, resource, resLinks ) )
	{
		changed.SetValue( true );
	}
	else
	{
		resLinks.PushBack( GetForwardLinks( resource.path ) );
	}
}

void Database::ApplyResults( IncrementalScanStatePtr state )
{
	auto& timer = state->timer;
	auto& resourcePaths = state->resourcePaths;
	auto& updatedResources = state->resources;
	auto& updatedLinks = state->updatedLinks;
	auto& originalLinks = state->originalLinks;
	auto& changed = state->changed;

	RED_LOG( "ResourceDatabase: (FastScan) Scanning resources took %1.3lf ms", timer.GetDeltaMS() );
	timer.Reset();

	if ( changed.GetValue() )
	{
		m_changed = true;
	}

	red::DynArray< Resource > oldResources{ std::move( m_resources ) };
	m_resources.Reserve( oldResources.Size() + updatedResources.Size() );

	m_resourcePaths.Clear();
	m_resourcePaths.Reserve( m_resources.Size() );

	Uint32 updatedLinkCount = std::accumulate( updatedLinks.Begin(), updatedLinks.End(), 0u, []( Uint32 result, const auto& resLinks ) { return result + resLinks.Size(); } );
	red::DynArray< Link > links;
	links.Reserve( m_forwardLinks.Size() + updatedLinkCount );

	{
		auto oldLinkIter = originalLinks.Begin();
		auto newLinkIter = updatedLinks.Begin();

		auto oldIter = oldResources.Begin(), oldEnd = oldResources.End();
		auto changedIter = updatedResources.Begin(), changedEnd = updatedResources.End();
		while ( !( oldIter == oldEnd || changedIter == changedEnd ) )
		{
			if ( *changedIter == *oldIter )
			{
				if ( changedIter->timestamp != 0ull )
				{
					m_resources.PushBack( *changedIter );
					m_resourcePaths.PushBack( changedIter->path );
					links.PushBack( *newLinkIter );
				}
				++oldIter;
				++oldLinkIter;
				++changedIter;
				++newLinkIter;
			}
			else if ( *changedIter < *oldIter )
			{
				if ( changedIter->timestamp != 0ull )
				{
					m_resources.PushBack( *changedIter );
					m_resourcePaths.PushBack( changedIter->path );
					links.PushBack( *newLinkIter );
				}
				++changedIter;
				++newLinkIter;
			}
			else // ( *oldIter < *pathIter )
			{
				m_resources.PushBack( *oldIter );
				m_resourcePaths.PushBack( oldIter->path );
				links.PushBack( *oldLinkIter );
				++oldIter;
				++oldLinkIter;
			}
		}

		while ( oldIter != oldEnd )
		{
			m_resources.PushBack( *oldIter );
			m_resourcePaths.PushBack( oldIter->path );
			++oldIter;
			links.PushBack( *oldLinkIter );
			++oldLinkIter;
		}

		while ( changedIter != changedEnd )
		{
			m_resources.PushBack( *changedIter );
			m_resourcePaths.PushBack( changedIter->path );
			++changedIter;
			links.PushBack( *newLinkIter );
			++newLinkIter;
		}
	}

	m_forwardLinks = std::move( links );
	m_backwardLinks = m_forwardLinks;
	std::sort( m_forwardLinks.Begin(), m_forwardLinks.End(), SortByFrom );
	std::sort( m_backwardLinks.Begin(), m_backwardLinks.End(), SortByTo );
	InitialiseRecursiveLookup();

	RED_LOG( "ResourceDatabase: (FastScan) Finalising resources and link tables took %1.3lf ms", timer.GetDeltaMS() );
	timer.Reset();

	RED_FATAL_ASSERT( std::is_sorted( m_resources.Begin(), m_resources.End() ) );
	RED_FATAL_ASSERT( std::is_sorted( m_resourcePaths.Begin(), m_resourcePaths.End() ) );

	RED_LOG( "ResourceDatabase: (FastScan) Verifying sorting took %0.3lf ms", timer.GetDeltaMS() );
}

void Database::UpdateScan( job::Builder& builder )
{
	auto state = IncrementalScanStatePtr::Create();

	state->resourcePaths = m_watcher->GetChangedFiles();
	if ( state->resourcePaths.Empty() )
	{
		RED_LOG( "ResourceDatabase: (FastScan) No files modified, nothing to scan" );
		return;
	}

	builder.DispatchJob( "ResourceDatabase/UpdateScan/ComputeDifferences", [this, state]( const job::RunContext& )
	{
		ComputeDifferences( state );
	} );

	builder.DispatchParallelForJob( "ResourceDatabase/UpdateScan/ScanResources", [state]() { return state->resources.Size(); }, [this, state]( Uint32 index, const job::RunContext& )
	{
		ScanResource( state, index );
	} );

	builder.DispatchJob( "ResourceDatabase/UpdateScan/ApplyResults", [this, state]( const job::RunContext& )
	{
		ApplyResults( state );
	} );
}

//------------------------------------------------------------------------------

#pragma pack(push, 1)
namespace fileformat
{

//
// File Structure
//
// Header
// [ Resource ... ]
// [ Link ... ]
// [ Path Offsets ... ]
// [ Strings ... ]
//

struct Header
{
	Uint32 magic;
	Uint32 version;
	Uint32 resourceOffset;
	Uint32 resourceCount;
	Uint32 linkOffset;
	Uint32 linkCount;
	Uint32 pathsOffset;
	Uint32 pathsCount;
	Uint32 stringsOffset;
	Uint32 stringsBytes;

	static constexpr Uint32 MAGIC_VALUE = 'BDSR'; // Resource Database 'RS' 'DB' backwards for endianness
	static constexpr Uint32 CURRENT_VERSION = 14;
};

struct Resource
{
	Uint32 pathIndex;
	Uint64 timestamp;
	Uint8 status;
	Uint8 isLink;

	Uint32 totalSize;
	Uint32 objectSize;
	Uint32 bufferSize;
};

static_assert( sizeof(Resource) == 26, "" );

struct Link
{
	Uint64 fromIndex : 30;
	Uint64 toIndex : 30;
	Uint64 isSoft : 1;
	Uint64 isInner : 1;
	Uint64 isInplace : 1;
	Uint64 emptyBits : 1; // NOTE: 2 bits free space
};

static_assert( sizeof( Link ) == 8, "" );

bool CheckHeaderEntries( const Header& header, const Uint32 fileSize )
{
	if ( header.resourceOffset == 0 || header.linkOffset == 0 || header.pathsOffset == 0 || header.stringsOffset == 0 )
	{
		return false;
	}
	if ( header.resourceOffset >= fileSize || header.resourceOffset + header.resourceCount * sizeof( fileformat::Resource ) > fileSize )
	{
		return false;
	}
	if ( header.linkOffset >= fileSize || header.linkOffset + header.linkCount * sizeof( fileformat::Link ) > fileSize )
	{
		return false;
	}
	if ( header.pathsOffset >= fileSize || header.pathsOffset + header.pathsCount * sizeof( Uint32 ) > fileSize )
	{
		return false;
	}
	if ( header.stringsOffset >= fileSize || header.stringsOffset + header.stringsBytes > fileSize )
	{
		return false;
	}

	return true;
}

} // fileformat
#pragma pack(pop)

const char* Database::GetDefaultFilename()
{
	return "ResourceDatabase.cache";
}

bool Database::Load()
{
	RED_FATAL_ASSERT( !m_cachePath.Empty() );
	red::StopWatch mainTimer;
	red::StopWatch timer;

	red::Blob fileBuffer;
	if ( !red::LoadFileToBuffer( m_cachePath, fileBuffer ) )
	{
		RED_LOG_ERROR( "ResourceDatabase: Could not load the Directory Listing Cache file '%hs'", m_cachePath.AsChar() );
		return false;
	}

	const Uint32 fileSize = fileBuffer.Size();
	if ( fileSize <= sizeof( fileformat::Header ) )
	{
		RED_LOG_ERROR( "ResourceDatabase: Invalid Resource Database Cache file '%hs'", m_cachePath.AsChar() );
		return false;
	}

	const auto& header = fileBuffer.As< const fileformat::Header >();
	if ( header.magic != fileformat::Header::MAGIC_VALUE ||
		header.version != fileformat::Header::CURRENT_VERSION )
	{
		RED_LOG_ERROR( "ResourceDatabase: Invalid Resource Database Cache file '%hs'", m_cachePath.AsChar() );
		return false;
	}

	if ( !fileformat::CheckHeaderEntries( header, fileSize ) )
	{
		RED_LOG_ERROR( "ResourceDatabase: Invalid Resource Database Cache file '%hs'", m_cachePath.AsChar() );
		return false;
	}

	RED_LOG( "ResourceDatabase: Reading and checking file took %0.3lf ms", timer.GetDeltaMS() );
	timer.Reset();

	const auto* pathStart = fileBuffer.Pointer< const Uint32 >( header.pathsOffset );
	red::ArraySpan< const Uint32 > pathOffsets( pathStart, header.pathsCount );

	red::BlobView pathStrings = fileBuffer.Range( header.stringsOffset, header.stringsBytes );

	red::DynArray< res::ResourcePath > allResourcePaths;
	allResourcePaths.Reserve( header.pathsCount );
	for ( Uint32 offset : pathOffsets )
	{
		const char* str = pathStrings.Pointer< char >( offset );
		allResourcePaths.PushBack( res::ResourcePath::Build( str ) );
	}

	RED_LOG( "ResourceDatabase: Processing resource paths took %0.3lf ms", timer.GetDeltaMS() );
	timer.Reset();

	const auto* resourceStart = fileBuffer.Pointer< const fileformat::Resource >( header.resourceOffset );
	red::ArraySpan< const fileformat::Resource > fileResources( resourceStart, header.resourceCount );

	red::DynArray< Resource > resources;
	resources.Reserve( header.resourceCount );
	for ( const auto& fileRes : fileResources )
	{
		if ( fileRes.pathIndex > allResourcePaths.Size() )
		{
			RED_LOG_ERROR( "ResourceDatabase: Path index %u for resource is out of range (0,%u)", fileRes.pathIndex, allResourcePaths.Size() );
			return false;
		}

		Resource& res = resources.EmplaceBack();
		res.path = allResourcePaths[ fileRes.pathIndex ];
		res.timestamp = fileRes.timestamp;
		res.status = static_cast< Resource::Status >( fileRes.status );
		res.isLink = fileRes.isLink != 0;
		res.totalSize = fileRes.totalSize;
		res.objectSize = fileRes.objectSize;
		res.bufferSize = fileRes.bufferSize;
	}

	RED_LOG( "ResourceDatabase: Processing resources took %0.3lf ms", timer.GetDeltaMS() );
	timer.Reset();

	const auto* linkStart = fileBuffer.Pointer< const fileformat::Link >( header.linkOffset );
	red::ArraySpan< const fileformat::Link > fileLinks( linkStart, header.linkCount );

	red::DynArray< Link > links;
	links.Reserve( fileLinks.Size() );
	for ( const auto& fileLink : fileLinks )
	{
		const Uint32 fromIndex = fileLink.fromIndex;
		const Uint32 toIndex = fileLink.toIndex;
		if ( fromIndex > allResourcePaths.Size() || toIndex > allResourcePaths.Size() )
		{
			RED_LOG_ERROR( "ResourceDatabase: Path index %u or %u for links is out of range (0,%u)", fromIndex, toIndex, allResourcePaths.Size() );
			return false;
		}

		Link& link = links.EmplaceBack();
		link.from = allResourcePaths[ fromIndex ];
		link.to = allResourcePaths[ toIndex ];
		link.type = fileLink.isSoft != 0 ? Link::Type::SOFT : Link::Type::HARD;
		link.location = fileLink.isInner != 0 ? Link::Location::INNER : Link::Location::PRIMARY;
		link.inplace = fileLink.isInplace != 0;
	}

	RED_LOG( "ResourceDatabase: Processing links took %0.3lf ms", timer.GetDeltaMS() );
	timer.Reset();

	m_resources = std::move( resources );
	m_forwardLinks = std::move( links );
	m_backwardLinks = m_forwardLinks;
	std::sort( m_backwardLinks.Begin(), m_backwardLinks.End(), SortByTo );

	RED_LOG( "ResourceDatabase: Sorting links took %0.3lf ms", timer.GetDeltaMS() );
	timer.Reset();

	RED_LOG( "ResourceDatabase: Loading Resource Database Cache '%hs' took %0.3lf ms", m_cachePath.AsChar(), mainTimer.GetDeltaMS() );

	const bool isSorted = std::is_sorted( m_resources.Begin(), m_resources.End() ) && 
		std::is_sorted( m_forwardLinks.Begin(), m_forwardLinks.End(), SortByFrom );
	if ( !isSorted )
	{
		RED_LOG_ERROR( "ResourceDatabase: File '%hs' is corrupted", m_cachePath.AsChar() );
		return false;
	}

	RED_LOG( "ResourceDatabase: Verifying sorting took %0.3lf ms", timer.GetDeltaMS() );

	m_changed = false;
	return true;
}

bool Database::Save()
{
	RED_FATAL_ASSERT( !m_cachePath.Empty() );
	red::StopWatch timer;

	RED_FATAL_ASSERT( std::is_sorted( m_resources.Begin(), m_resources.End() ) );
	RED_FATAL_ASSERT( std::is_sorted( m_resourcePaths.Begin(), m_resourcePaths.End() ) );
	RED_FATAL_ASSERT( std::is_sorted( m_forwardLinks.Begin(), m_forwardLinks.End(), SortByFrom ) );
	RED_FATAL_ASSERT( std::is_sorted( m_backwardLinks.Begin(), m_backwardLinks.End(), SortByTo ) );

	Uint32 written = 0;
	io::NativeFileHandle writer;
	if ( !writer.Open( m_cachePath.AsChar(), io::eOpenFlag_WriteNew ) )
	{
		RED_LOG_ERROR( "Could not open the cache file for writing '%hs'", m_cachePath.AsChar() );
		return false;
	}

	red::DynArray< res::ResourcePath > resourcePaths;
	resourcePaths.Reserve( m_resources.Size() );
	red::HashMap< res::ResourcePath, Uint32 > pathIndexMap;
	pathIndexMap.Reserve( m_resources.Size() );
	Uint32 stringBytes = 0;

	auto PushString = [&resourcePaths, &pathIndexMap, &stringBytes]( const res::ResourcePath& path ) mutable -> Uint32
	{
		Uint32 index = resourcePaths.Size();
		if ( !pathIndexMap.Find( path, index ) )
		{
			pathIndexMap.Insert( path, index );
			resourcePaths.PushBack( path );
			stringBytes += path.ToStringView().Length() + 1;
		}
		return index;
	};


	red::DynArray< fileformat::Resource > fileResources;
	fileResources.Reserve( m_resources.Size() );
	for ( const auto& res : m_resources )
	{
		fileformat::Resource& fileRes = fileResources.EmplaceBack();
		fileRes.pathIndex = PushString( res.path );
		fileRes.timestamp = res.timestamp;
		fileRes.status = static_cast< Uint8 >( res.status );
		fileRes.isLink = res.isLink ? 1 : 0;
		fileRes.totalSize = res.totalSize;
		fileRes.objectSize = res.objectSize;
		fileRes.bufferSize = res.bufferSize;
	}


	red::DynArray< fileformat::Link > fileLinks;
	fileLinks.Reserve( m_forwardLinks.Size() );
	for ( const auto& link : m_forwardLinks )
	{
		fileformat::Link& fileLink = fileLinks.EmplaceBack();
		fileLink.fromIndex = PushString( link.from );
		fileLink.toIndex = PushString( link.to );
		fileLink.isSoft = ( link.type != Link::Type::HARD ) ? 1 : 0;
		fileLink.isInner = ( link.location != Link::Location::PRIMARY ) ? 1 : 0;
		fileLink.isInplace = link.inplace ? 1 : 0;
		fileLink.emptyBits = 0;
	}


	RED_FATAL_ASSERT( resourcePaths.Size() < (1u << 31), "Exceeded the maximum number of path strings allowed" );

	red::DynArray< Uint32 > pathIndexes;
	pathIndexes.Reserve( resourcePaths.Size() );

	red::Blob stringsBuffer{ stringBytes };
	Uint32 stringsOffset = 0;
	for ( const auto& path : resourcePaths )
	{
		const Uint32 initialOffset = stringsOffset;
		char* writePtr = stringsBuffer.Pointer< char >( initialOffset );
		const red::StringView str = path.ToStringView();
		const Uint32 strLength = str.Length();

		RED_FATAL_ASSERT( initialOffset + strLength + 1 <= stringsBuffer.Size() );

		red::Memcpy( writePtr, str.Data(), strLength );
		writePtr[ strLength ] = '\0';
		stringsOffset += strLength + 1;

		pathIndexes.PushBack( initialOffset );
	}

	// Write out blank header first so the file is only valid when its written out correctly in full
	fileformat::Header header;
	red::Memzero( &header, sizeof( fileformat::Header ) );
	RED_VERIFY( writer.Write( &header, sizeof ( fileformat::Header ), written ) && written == sizeof( fileformat::Header ) );

	header.magic = fileformat::Header::MAGIC_VALUE;
	header.version = fileformat::Header::CURRENT_VERSION;
	header.resourceOffset = sizeof(fileformat::Header);
	header.resourceCount = fileResources.Size();
	header.linkOffset = header.resourceOffset + fileResources.DataSize();
	header.linkCount = fileLinks.Size();
	header.pathsOffset = header.linkOffset + fileLinks.DataSize();
	header.pathsCount = pathIndexes.Size();
	header.stringsOffset = header.pathsOffset + pathIndexes.DataSize();
	header.stringsBytes = stringsOffset;

	RED_VERIFY( writer.Write( fileResources.TypedData(), fileResources.DataSize(), written ) && written == fileResources.DataSize() );
	RED_FATAL_ASSERT( header.linkOffset == writer.Tell() );
	RED_VERIFY( writer.Write( fileLinks.TypedData(), fileLinks.DataSize(), written ) && written == fileLinks.DataSize() );
	RED_FATAL_ASSERT( header.pathsOffset == writer.Tell() );
	RED_VERIFY( writer.Write( pathIndexes.TypedData(), pathIndexes.DataSize(), written ) && written == pathIndexes.DataSize() );
	RED_FATAL_ASSERT( header.stringsOffset == writer.Tell() );
	RED_VERIFY( writer.Write( stringsBuffer.Data(), stringsBuffer.Size(), written ) && written == stringsBuffer.Size() );
	const auto endOfFile = writer.Tell();

	// Write out correct header
	writer.Seek( 0, io::eSeekOrigin_Set );
	RED_VERIFY( writer.Write( &header, sizeof ( fileformat::Header ), written ) && written == sizeof( fileformat::Header ) );

	writer.Flush();
	writer.Truncate( endOfFile );
	writer.Close();

	RED_LOG( "ResourceDatabase: Saving Resource Database Cache '%hs' took %0.3lf ms", m_cachePath.AsChar(), timer.GetDeltaMS() );

	m_changed = false;
	return true;
}

void Database::Clear()
{
	m_resources.Clear();
	m_forwardLinks.Clear();
	m_backwardLinks.Clear();
	InitialiseRecursiveLookup();
}

//------------------------------------------------------------------------------

const Resource* Database::LookupResource( const res::ResourcePath& path ) const
{
	auto iter = std::lower_bound( m_resources.Begin(), m_resources.End(), path );
	if ( iter != m_resources.End() )
	{
		const auto& resource = *iter;
		if ( resource == path )
		{
			return &resource;
		}
	}
	return nullptr;
}

Resources Database::LookupResources( red::ArraySpan< const res::ResourcePath > paths ) const
{
	Resources result;
	result.Reserve( paths.Size() );
	for ( const auto& path : paths )
	{
		result.PushBack( LookupResource( path ) );
	}
	return result;
}

Resources Database::LookupFromResources( Links links ) const
{
	Resources result;
	result.Reserve( links.Size() );
	for ( const auto& link : links )
	{
		result.PushBack( LookupResource( link.from ) );
	}
	return result;
}

Resources Database::LookupToResources( Links links ) const
{
	Resources result;
	result.Reserve( links.Size() );
	for ( const auto& link : links )
	{
		result.PushBack( LookupResource( link.to ) );
	}
	return result;
}

Links Database::GetAllForwardLinks() const
{
	return m_forwardLinks;
}

Links Database::GetAllBackwardLinks() const
{
	return m_backwardLinks;
}

red::ArraySpan< const Resource > Database::GetResources() const
{
	return m_resources;
}

const red::DynArray< res::ResourcePath >& Database::GetResourcePaths() const
{
	return m_resourcePaths;
}

res::IBackendResourceDepot& Database::GetResourceDepot() const
{
	return *m_depot;
}

bool Database::HasChanged() const
{
	return m_changed;
}

namespace
{

RED_INLINE bool LinkFromPredicate_lower( const Link& link, const res::ResourcePath& path )
{
	return link.from < path;
}

RED_INLINE bool LinkFromPredicate_upper( const res::ResourcePath& path, const Link& link )
{
	return path < link.from;
}

RED_INLINE bool LinkToPredicate_lower( const Link& link, const res::ResourcePath& path )
{
	return link.to < path;
}

RED_INLINE bool LinkToPredicate_upper( const res::ResourcePath& path, const Link& link )
{
	return path < link.to;
}

}

Links Database::GetForwardLinks( const res::ResourcePath& path ) const
{
	auto startIter = std::lower_bound( m_forwardLinks.Begin(), m_forwardLinks.End(), path, LinkFromPredicate_lower );
	if ( startIter == m_forwardLinks.End() )
	{
		return Links();
	}

	auto endIter = std::upper_bound( m_forwardLinks.Begin(), m_forwardLinks.End(), path, LinkFromPredicate_upper );

	Uint32 startOffset = static_cast< Uint32 >( startIter - m_forwardLinks.Begin() );
	Uint32 endOffset = static_cast< Uint32 >( endIter - m_forwardLinks.Begin() );

	return Links( m_forwardLinks ).Slice( startOffset, endOffset );
}

Links Database::GetBackwardLinks( const res::ResourcePath& path ) const
{
	auto startIter = std::lower_bound( m_backwardLinks.Begin(), m_backwardLinks.End(), path, LinkToPredicate_lower );
	if ( startIter == m_backwardLinks.End() )
	{
		return Links();
	}

	auto endIter = std::upper_bound( m_backwardLinks.Begin(), m_backwardLinks.End(), path, LinkToPredicate_upper );

	Uint32 startOffset = static_cast< Uint32 >( startIter - m_backwardLinks.Begin() );
	Uint32 endOffset = static_cast< Uint32 >( endIter - m_backwardLinks.Begin() );

	return Links( m_backwardLinks ).Slice( startOffset, endOffset );
}


res::ResourcePath Database::ResolveResourcePath( const res::ResourcePath& path ) const
{
	Int32 linkCount = 0;
	res::ResourcePath resPath = path;
	const Resource* resource = LookupResource( resPath );
	while ( resource == nullptr || resource->timestamp == 0ull )
	{
		if ( linkCount == res::MAX_LINK_FILE_RECURSION )
		{
			RED_LOG_ERROR( "ResourceDatabase: Maximum number of links (%u) exceeded while resolving %hs", res::MAX_LINK_FILE_RECURSION, path.ToDebugString() );
			return res::ResourcePath();
		}

		red::String linkPathStr = resPath.ToString() + ".link";
		if ( linkPathStr.Length() >= res::ResourcePath::MAX_LENGTH )
		{
			RED_LOG_ERROR( "ResourceDatabase: Link path too long '%hs' length %u", linkPathStr.AsChar(), linkPathStr.Length() );
			return res::ResourcePath();
		}

		res::ResourcePath linkPath = res::ResourcePath::Build( linkPathStr );
		Links forwardLinks = GetForwardLinks( linkPath );
		if ( forwardLinks.Empty() )
		{
			// Can't find any link file in the database so exit with invalid path
			return res::ResourcePath();
		}

		RED_FATAL_ASSERT( forwardLinks.Size() == 1, "Link file entries should have exactly one dependency" );
		resPath = forwardLinks.Front().to;
		resource = LookupResource( resPath );
		++linkCount;
	}
	RED_FATAL_ASSERT( resource->timestamp != 0ull, "Resource timestamp for %hs is 0!", resource->path.ToDebugString() );
	return resource->path;
}

ResourcePaths Database::GetForwardResourcePaths( const res::ResourcePath& path ) const
{
	ResourcePaths result;

	auto startIter = std::lower_bound( m_forwardLinks.Begin(), m_forwardLinks.End(), path, LinkFromPredicate_lower );
	if ( startIter == m_forwardLinks.End() )
	{
		return result;
	}

	auto endIter = std::upper_bound( m_forwardLinks.Begin(), m_forwardLinks.End(), path, LinkFromPredicate_upper );

	result.Reserve( red::Distance< Uint32 >( startIter, endIter ) );
	for ( auto iter = startIter; iter != endIter; ++iter )
	{
		auto followedPath = ResolveResourcePath( iter->to );
		if ( followedPath.IsValid() )
		{
			result.PushBack( followedPath );
		}
	}

	red::alg::RemoveDuplicates( result );

	return result;
}

ResourcePaths Database::ExtractFromPaths( Links links )
{
	ResourcePaths result;
	result.Reserve( links.Size() );
	for ( const auto& link : links )
	{
		result.PushBack( link.from );
	}
	return result;
}

ResourcePaths Database::ExtractToPaths( Links links )
{
	ResourcePaths result;
	result.Reserve( links.Size() );
	for ( const auto& link : links )
	{
		result.PushBack( link.to );
	}
	return result;
}

//------------------------------------------------------------------------------

void Database::InitialiseRecursiveLookup()
{
	RED_SCOPE_LOCK( m_recursiveGuard );
	m_recursiveVisited.Clear();
	m_recursiveForwardDeps.Clear();
	m_recursiveVisited.Reserve( m_resources.Size() );
	m_recursiveForwardDeps.Reserve( m_resources.Size() );
}

const red::Set< res::ResourcePath >& Database::CollectRecursive( const res::ResourcePath& path )
{
	auto iter = m_recursiveForwardDeps.Find( path );

	if ( iter == m_recursiveForwardDeps.End() )
	{
		red::Set< res::ResourcePath > result;

		auto addResult = m_recursiveVisited.Insert( path );
		if ( addResult.IsSuccessful() )
		{
			const auto fwdLinks = GetForwardLinks( path );
			result.Reserve( fwdLinks.Size() );
			for ( const auto& link : fwdLinks )
			{
				result.InsertUnsorted( link.to );
			}
			result.MakeClean();

			for ( const auto& link : fwdLinks )
			{
				result.Union( CollectRecursive( link.to ) );
			}
		}

		return m_recursiveForwardDeps[ path ] = std::move( result );
	}
	return iter.Value();
}

const red::Set< res::ResourcePath >& Database::GetRecursiveForwardPaths( const res::ResourcePath& path )
{
	RED_SCOPE_LOCK( m_recursiveGuard );

	auto iter = m_recursiveForwardDeps.Find( path );
	if ( iter != m_recursiveForwardDeps.End() )
	{
		return iter.Value();
	}
	return CollectRecursive( path );
}

//------------------------------------------------------------------------------

#if !defined( VANGUARD_ASSET_HOST )
// Wrapper for a resource database to make it accessible to the ResourceBackend
class BACKEND_DATA_API DatabaseWrapper : public IResourceDatabaseProvider
{
	RTTI_DECLARE_TYPE( DatabaseWrapper );
	RED_USE_MEMORY_POOL( red::PoolBackend );

public:
	DatabaseWrapper();

	virtual red::AbsolutePath GetDefaultFilePath() const override;

	virtual void SetReadOnly( bool value ) override;
	virtual void Initialise( res::IBackendResourceDepot& depot, const red::AbsolutePath& cachePath, job::Counter& counter ) override;
	virtual void Shutdown() override;
	virtual void Update() override;
	virtual void GetForwardLinks( const res::ResourcePath& path, red::DynArray<res::ResourcePath>& outLinks ) const override;
	virtual void GetBackwardLinks( const res::ResourcePath& path, red::DynArray<res::ResourcePath>& outLinks ) const override;

private:

	resdb::Database m_database;

	bool m_readOnly;
};

DatabaseWrapper::DatabaseWrapper()
	: m_readOnly( false )
{
}

red::AbsolutePath DatabaseWrapper::GetDefaultFilePath() const
{
	return GFileManager->GetCacheDirectory().AddFilePath( resdb::Database::GetDefaultFilename() );
}

void DatabaseWrapper::SetReadOnly( bool value )
{
	m_readOnly = value;
}

void DatabaseWrapper::Initialise( res::IBackendResourceDepot& depot, const red::AbsolutePath& cachePath, job::Counter& counter )
{
	if ( m_readOnly )
	{
		m_database.Initialise_NoUpdate( depot, cachePath, counter );
	}
	else
	{
		m_database.Initialise( depot, cachePath, counter );
	}
}

void DatabaseWrapper::Shutdown()
{
	m_database.Shutdown();
}

void DatabaseWrapper::Update()
{
	m_database.Update();
}

void DatabaseWrapper::GetForwardLinks( const res::ResourcePath& path, red::DynArray<res::ResourcePath>& outLinks ) const
{
	outLinks.PushBack( resdb::Database::ExtractToPaths( m_database.GetForwardLinks( path ) ) );
}

void DatabaseWrapper::GetBackwardLinks( const res::ResourcePath& path, red::DynArray<res::ResourcePath>& outLinks ) const
{
	outLinks.PushBack( resdb::Database::ExtractFromPaths( m_database.GetBackwardLinks( path ) ) );
}
#endif

} // resdb

#if !defined( VANGUARD_ASSET_HOST )
RTTI_BEGIN_TYPE_IN_NAMESPACE( DatabaseWrapper, resdb );
RTTI_PARENT_TYPE( resdb::IResourceDatabaseProvider );
RTTI_END_TYPE();
#endif


