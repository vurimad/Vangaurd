/*
 * Copyright (c) 2018 CD Projekt Red. All Rights Reserved.
 */

#include "build.h"
#include "directoryDependency.h"
#include "dataSystemContext.h"
#include "dataTypesImpl.h"
#include "dependencyCache.h"
#include "dependencyReader.h"
#include "dependencyWriter.h"

#include "../../../common/redContainers/include/circularBuffer.h"
#include "../../../common/redFileSystem/include/fileSys.h"

//-----------------------------------------------------------------------------

static bool IsValidDirectoryString( const red::StringView& directory )
{
	if ( !directory.Empty() )
	{
		if ( directory.StartsWith( "engine" ) || 
			directory.StartsWith( "base" ) ||
			directory.StartsWith( "gyms" ) ||
			directory.StartsWith( "user" ) ||
			directory.StartsWith( "test" ) ||
			directory.StartsWith( "temp" ) ||
			directory.StartsWith( "multi" ) ||
			directory.StartsWith( "dlc" ) ||
			directory.StartsWith( "ep" ) )
		{
			return true;
		}
	}
	return false;
}

static bool IsValidPatternString( const red::StringView& pattern )
{
	return !pattern.Empty();
}

dependency::DependencyUniquePtr MakeDirectoryDependency( const red::StringView& directory, const red::StringView& pattern, bool recursive, data::DepotLocation depot, dependency::Requirement require )
{
	RED_FATAL_ASSERT( IsValidDirectoryString( directory ), "Must provide a valid directory string." );
	RED_FATAL_ASSERT( IsValidPatternString( pattern ), "Must provide a valid pattern string" );
	return red::CreateUniquePtr< dependency::DirectoryDependency >( directory, pattern, recursive, depot, require );
}

//-----------------------------------------------------------------------------

RTTI_BEGIN_TYPE_IN_NAMESPACE( DirectoryDependency, dependency )
RTTI_PARENT_TYPE( Dependency )
RTTI_END_TYPE()

namespace dependency
{

DirectoryDependency::DirectoryDependency()
	: Dependency()
	, m_combinedHash( 0ull )
	, m_recursive( false )
{
}

DirectoryDependency::DirectoryDependency( const red::StringView& directory, const red::StringView& pattern, bool recursive, data::DepotLocation location, Requirement requireLevel )
	: Dependency( location, requireLevel )
	, m_directory( res::SanitizeStringAsResourcePath( directory ) )
	, m_pattern( res::SanitizeStringAsResourcePath( pattern ) )
	, m_combinedHash( 0ull )
	, m_recursive( recursive )
{
	if ( m_directory.Back() != '\\' )
	{
		m_directory += '\\';
	}

	ComputePathValues();
}

DirectoryDependency::~DirectoryDependency()
{
}

DirectoryDependency::DirectoryDependency( const DirectoryDependency& ) = default;
DirectoryDependency::DirectoryDependency( DirectoryDependency&& ) = default;
DirectoryDependency& DirectoryDependency::operator= ( const DirectoryDependency& ) = default;
DirectoryDependency& DirectoryDependency::operator= ( DirectoryDependency&& ) = default;

DependencyUniquePtr DirectoryDependency::Clone() const
{
	return red::CreateUniquePtr< DirectoryDependency >( *this );
}

bool DirectoryDependency::Load( Reader& reader )
{
	reader.ReadString( "path", m_directory );
	reader.ReadString( "pattern", m_pattern );

	Dependency::Load( reader );

	reader.ReadBool( "recursive", m_recursive );

	ComputePathValues();

	return true;
}

bool DirectoryDependency::Save( Writer& writer ) const
{
	writer.WriteString( "path", m_directory );
	writer.WriteString( "pattern", m_pattern );

	Dependency::Save( writer );

	writer.WriteBool( "recursive", m_recursive );

	return true;
}

void DirectoryDependency::Update( data::SystemContext& context )
{
	PC_SCOPE( DirectoryDependency_Update );

	data::ResourceDepot& depot = context.GetResourceDepot( GetDepotLocation() );
	dependency::Cache& cache = context.GetDepsCache( GetDepotLocation() );

	red::DynArray< res::ResourcePath > resourcePaths;
	{
		red::DynArray< String > files;
		red::DynArray< String > dirs;

		red::CircularBuffer< red::String > searchPaths;
		searchPaths.PushBack( m_directory );
		while ( !searchPaths.Empty() )
		{
			const red::String path = searchPaths.Front();
			searchPaths.PopFront();

			files.Clear();
			depot.FindDepotFiles( path, m_pattern.AsChar(), files );
			for ( const auto& file : files )
			{
				const String filePath = path + file;
				resourcePaths.PushBack( res::ResourcePath::Build( filePath ) );
			}

			if ( m_recursive )
			{
				dirs.Clear();
				depot.FindDepotSubDirectories( path, dirs );
				for ( const auto& dir : dirs )
				{
					const String searchPath = path + dir + "\\";
					searchPaths.PushBack( searchPath );
				}
			}
		}
	}
	red::alg::RemoveDuplicates( resourcePaths );

	m_scannedResources.Reserve( resourcePaths.Size() );
	for ( const auto& resPath : resourcePaths )
	{
		m_scannedResources.PushBack( { resPath, 0ull, {} } );
	}

#pragma pack(push, 1)
	struct Element
	{
		Uint64 path;
		dependency::Hash hash;
	};
	static_assert( sizeof( Element ) == sizeof( Uint64 ) + sizeof( dependency::Hash ), "Element used for hashing must have no padding" );
#pragma pack(pop)

	Uint32 hashingSize = m_scannedResources.Size() * sizeof( Element );
	Element* hashingElements = static_cast< Element* >( RED_ALLOCATE_WITHOUT_HOOKS( red::PoolBackend, hashingSize, red::memory::HookType::HookType_All ) );

	for ( Uint32 i = 0; i < m_scannedResources.Size(); ++i )
	{
		FileInfo& info = m_scannedResources[ i ];
		const bool exists = cache.Lookup( info );
		
		RED_FATAL_ASSERT( exists, "File '%hs' was moved or deleted between depot scanning and cache lookup!", info.path.ToDebugString() );

		hashingElements[ i ].path = info.path.GetHash();
		hashingElements[ i ].hash = info.hash;
		if ( info.timestamp > m_timestamp )
		{
			m_timestamp = info.timestamp;
		}
	}

	m_hash = Hash::Compute( red::BlobView( hashingElements, hashingSize ) );
	RED_FREE_WITHOUT_HOOKS( red::PoolBackend, hashingElements, red::memory::HookType::HookType_All );
	SetValid( true );

	// Free memory if we don't need to dump debug info.
	if( !context.ShouldCollectDebugInfo() )
	{
		m_scannedResources.Clear();
		m_scannedResources.Shrink();
	}
}

bool DirectoryDependency::DumpDebugInfo( Writer& writer ) const
{
	auto filesScope = writer.StartArray( "resources", m_scannedResources.Size() );
	auto& filesWriter = *filesScope;

	for ( const auto& resInfo : m_scannedResources )
	{
		auto fileScope = filesWriter.StartScope( {} );
		auto& fileWriter = *fileScope;

		fileWriter.WritePath( "path", resInfo.path );
		fileWriter.WriteTimestamp( "timestamp", resInfo.timestamp );
		fileWriter.WriteHash( "hash", resInfo.hash );
	}

	return true;
}

void DirectoryDependency::ComputePathValues()
{
	m_combinedPath = m_directory + m_pattern;
	m_combinedHash = red::CalculateHash64( m_combinedPath.Data(), m_combinedPath.Length(), RED_FNV_OFFSET_BASIS64 );
}

red::StringView DirectoryDependency::GetPathString() const
{
	return m_combinedPath;
}

Uint64 DirectoryDependency::GetPathHash() const
{
	return m_combinedHash;
}

} // dependency
