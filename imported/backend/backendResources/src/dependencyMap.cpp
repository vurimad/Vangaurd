/*
 * Copyright (c) 2017 CD Projekt Red. All Rights Reserved.
 */

#include "build.h"
#include "dependencyMap.h"

#include "../../../common/redFileSystem/include/fileSys.h"
#include "../../../common/resource/include/resourceEditorDepotImpl.h"

#include "../../backendEngine/include/commandletUtils.h"
#include "../../backendEngine/include/resourceUtils.h"

namespace commandlet
{

DependencyEntry::DependencyEntry()
	: m_level( std::numeric_limits<Uint32>::max() )
	, m_status( Status::NOT_SCANNED )
	, m_isLink( false )
{}

DependencyEntry::DependencyEntry( const res::ResourcePath& resourcePath )
	: m_path( resourcePath )
	, m_level( std::numeric_limits<Uint32>::max() )
	, m_status( Status::NOT_SCANNED )
	, m_isLink( false )
{}

const char* DependencyEntry::GetStatusMessage() const
{
	switch ( m_status )
	{
	case Status::NOT_SCANNED: return "Not scanned";
	case Status::FILE_NOT_FOUND: return "File not found";
	case Status::IO_ERROR: return "Read error";
	case Status::EMPTY: return "File empty";
	case Status::INVALID_VERSION: return "Invalid file version";
	case Status::TABLE_FAILURE: return "Error loading file tables";
	case Status::PARSE_FAILURE: return "Error parsing file";
	case Status::NOT_A_RESOURCE: return "Not a resource";
	case Status::DATA_ERROR: return "Data error";
	case Status::LINK_ERROR: return "Link error";
	default: return "";
	};
}

bool operator==( const DependencyEntry& left, const DependencyEntry& right )
{
	return left.Path() == right.Path();
}

bool operator!=( const DependencyEntry& left, const DependencyEntry& right )
{
	return left.Path() != right.Path();
}

bool operator<( const DependencyEntry& left, const DependencyEntry& right )
{
	// Sort entries by path, which means by its path hash identifier
	return left.Path() < right.Path();
}


DependencyMap::DependencyMap( res::ResourceEditorDepot* editorDepot, DependencyOrder order )
	: m_editorDepot( editorDepot )
	, m_ordering( order )
{
}

void DependencyMap::ScanResourceRecursive( const res::ResourcePath& path )
{
	if ( m_entryMap.KeyExist( path ) )
	{
		return;
	}

	DependencyEntry entry = DependencyEntry::ScanResource( path, m_editorDepot, m_ordering );

	auto result = m_entryMap.Insert( path, entry );
	RED_FATAL_ASSERT( result.IsSuccessful() );

	if ( entry.IsOK() )
	{
		for ( const auto& depPath : entry.Dependencies() )
		{
			ScanResourceRecursive( depPath );
		}
	}
}

void DependencyMap::PrepareRecursiveDependencies()
{
	m_recursiveDepsCache.Reserve( m_entryMap.Size() );
	m_recursiveVisitedDeps.Reserve( m_entryMap.Size() );
}

const red::Set<res::ResourcePath>& DependencyMap::CollectRecursive( const res::ResourcePath & path )
{
	if ( !m_recursiveDepsCache.KeyExist( path ) )
	{
		red::Set<res::ResourcePath> result;

		auto addResult = m_recursiveVisitedDeps.Insert( path );
		if ( addResult.IsSuccessful() )
		{
			const DependencyEntry& entry = m_entryMap[ path ];
			const auto depPaths = entry.Dependencies();
			result.Reserve( depPaths.Size() );
			for ( const auto& depPath : depPaths )
			{
				result.InsertUnsorted( depPath );
			}
			result.MakeClean();

			for ( const auto& depPath : depPaths )
			{
				result.Union( CollectRecursive( depPath ) );
			}
		}
		else
		{
			RED_LOG_ERROR( "Recursive reverse dependency found in: %hs", path.ToDebugString() );
		}

		m_recursiveDepsCache[ path ] = result;
	}
	return m_recursiveDepsCache[ path ];
}

const DependencyEntry* DependencyMap::Get( const res::ResourcePath& path ) const
{
	return m_entryMap.FindPtr( path );
}

red::DynArray<const DependencyEntry*> DependencyMap::Get( red::ArraySpan<const res::ResourcePath> paths ) const
{
	red::DynArray<const DependencyEntry*> result;
	result.Reserve( paths.Size() );
	for ( const auto& path : paths )
	{
		result.PushBack( m_entryMap.FindPtr( path ) );
	}
	return result;
}

red::DynArray<res::ResourcePath> DependencyMap::DetectBadLevelReferences( const res::ResourcePath& path ) const
{
	const DependencyEntry& entry = m_entryMap[ path ];
	const auto depPaths = entry.Dependencies();

	red::DynArray<res::ResourcePath> result;
	result.Reserve( depPaths.Size() ); // Pessimistic guess

	for ( const auto& depPath : depPaths )
	{
		const DependencyEntry& depEntry = m_entryMap[ depPath ];
		if ( entry.Level() < depEntry.Level() )
		{
			result.PushBack( depPath );
		}
	}

	return result;
}

red::DynArray<res::ResourcePath> DependencyMap::DetectReverseReferences( const res::ResourcePath& path ) const
{
	red::DynArray<res::ResourcePath> result;

	for ( const auto& pair : m_entryMap )
	{
		const auto& entry = pair.Value();
		const auto depPaths = entry.Dependencies();
		if ( std::find( std::begin( depPaths ), std::end( depPaths ), path ) != std::end( depPaths ) )
		{
			result.PushBack( entry.Path() );
		}
	}

	return result;
}


//------------------------------------------------------------------------------

DependencyEntry DependencyEntry::ScanResource( const res::ResourcePath& path, res::ResourceEditorDepot* editorDepot, DependencyOrder order )
{
	DependencyEntry result{ path };

	result.m_status = dependency::prv::ScanResource( editorDepot, path, result.m_dependencies );

	// Once we've loaded the file, check if it's real or just a link
	result.m_isLink = editorDepot->ResourceIsLink( path );

	// Check what level it is
	result.m_level = editorDepot->GetResourceLevel( path );

	if ( order == DependencyOrder::BY_HASH )
	{
		auto& list = result.m_dependencies;
		std::sort( list.Begin(), list.End() );
	}
	else if ( order == DependencyOrder::BY_PATH )
	{
		auto& list = result.m_dependencies;
		std::sort( list.Begin(), list.End(), 
			[]( const res::ResourcePath& left, const res::ResourcePath& right ) -> bool 
		{
			return left.ToStringView() < right.ToStringView();
		} );
	}

	return result;
}

} // commandlet
