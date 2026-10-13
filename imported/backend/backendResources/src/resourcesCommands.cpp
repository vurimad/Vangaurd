/*
 * Copyright (c) 2019 CD Projekt Red. All Rights Reserved.
 */

#include "build.h"
#include "resourcesCommands.h"

#include "../../../common/redSystem/include/stopWatch.h"
#include "../../../common/redContainers/include/fundamentalStringParser.h"
#include "../../../common/redContainers/include/string/stringUtils.h"
#include "../../../common/redReflection/include/gatheredResource.h"
#include "../../backendData/include/dependencyScannerImpl.h"
#include "../../backendData/include/dependencyUtils.h"


//------------------------------------------------------------------------------

ResourcesCommands::ResourcesCommands()
{
}

ResourcesCommands::~ResourcesCommands()
{
}

void ResourcesCommands::Initialise( commandlet::BackendDepotHolder& depotHolder, PrintFunction&& print )
{
	m_printFunction = print;
	m_depot = depotHolder.GetDepot();

	red::AbsolutePath resdbCachePath = depotHolder.GetCachePath().AddFilePath( resdb::Database::GetDefaultFilename() );
	m_database.Initialise( *m_depot, resdbCachePath );

	for ( const auto& resource : m_database.GetResourcePaths() )
	{
		red::String ext = red::paths::ExtractExtension( resource.ToStringView() );
		m_extensions[ ext ]++;
	}

	m_allResourcePaths = m_database.GetResourcePaths();
	m_gatheredResources = res::prv::GetAllGatheredResources();
	std::sort( m_gatheredResources.Begin(), m_gatheredResources.End(), []( const res::GatheredResource* left, const res::GatheredResource* right ) -> bool
	{
		return res::SortByDirectory( left->GetPath(), right->GetPath() );
	} );

	auto allResources = m_allResourcePaths; // Copy, link map modifies this
	m_fileLinkMap.Initialise( *m_depot, allResources );
}

void ResourcesCommands::Shutdown()
{
}


//------------------------------------------------------------------------------

namespace
{

using resources_commands_helper::Hash;
using resources_commands_helper::HashParam;

res::ResourcePath ParseResourcePath( red::StringView param )
{
	const String paramStr = param.ToString();
	const AnsiChar* str = paramStr.AsChar();

	Uint64 resourceHash = 0;
	if ( paramStr.Length() > 2 && ( paramStr.BeginsWith( "0x" ) || paramStr.BeginsWith( "0X" ) ) )
	{
		const AnsiChar* hexStr = str + 2;
		if ( GParseHex( hexStr, resourceHash ) )
		{
			return res::ResourcePath::Build( resourceHash );
		}
	}

	if ( GParseUint64( str, resourceHash ) )
	{
		return res::ResourcePath::Build( resourceHash );
	}

	return res::ResourcePath::Build( param );
}

}

//------------------------------------------------------------------------------

red::DynArray< res::ResourcePath > ResourcesCommands::ParseParamsToResourcePaths( CommandParameters params )
{
	red::DynArray< res::ResourcePath > paths;

	for ( const auto& param : params )
	{
		if ( param.EndsWith( '\\' ) || param.EndsWith( '/' ) )
		{
			// All files in the directory
			String basePath = param.ToString();
			paths.PushBack( res::ResourcePath::Build( basePath ) ); // Note: Sometimes we actually want to know if something is including the directory itself as a resource
			paths.PushBack( commandlet::ScanResourceDepotDirectories( *m_depot, basePath ) );
		}
		else if ( red::IsGlobString( param ) )
		{
			// Glob for files in a directory
			String basePath = red::paths::ExtractParentPath( param );
			const char* pattern = red::paths::GetFileName( param ).Data();
			paths.PushBack( commandlet::ScanResourceDepotDirectories( *m_depot, basePath, pattern ) );
		}
		else
		{
			// Single file
			paths.PushBack( ParseResourcePath( param ) );
		}
	}

	return paths;
}

namespace
{

static constexpr const char LINK_ASYNC[] = "Async";
static constexpr const char LINK_INNER[] = "Inner";
static constexpr const char LINK_INPLACE[] = "Inplace";

static String LinkPropertiesToString( const resdb::Link& link )
{
	red::DynArray< red::StringView > strings{ red::PoolBackend() };
	if ( link.type == resdb::Link::Type::SOFT )
	{
		strings.PushBack( LINK_ASYNC );
	}
	if ( link.location == resdb::Link::Location::INNER )
	{
		strings.PushBack( LINK_INNER );
	}
	if ( link.inplace )
	{
		strings.PushBack( LINK_INPLACE );
	}
	return red::StrJoin( strings, ", " );
}

}

void ResourcesCommands::PrintForwardDeps( const res::ResourcePath& resourcePath )
{
	resdb::Links links = m_database.GetForwardLinks( resourcePath );
	for ( const auto& link : links )
	{
		m_printFunction( "\t" );
		m_printFunction( link.to.ToStringView() );
		m_printFunction( " - " );
		m_printFunction( LinkPropertiesToString( link ) );
		m_printFunction( "\n" );
	}
}

void ResourcesCommands::PrintReverseDeps( const res::ResourcePath& resourcePath )
{
	resdb::Links links = m_database.GetBackwardLinks( resourcePath );
	for ( const auto& link : links )
	{
		m_printFunction( "\t" );
		m_printFunction( link.from.ToStringView() );
		m_printFunction( " - " );
		m_printFunction( LinkPropertiesToString( link ) );
		m_printFunction( "\n" );
	}
}

void ResourcesCommands::PrintResolvedDeps( const res::ResourcePath& resourcePath )
{
	resdb::ResourcePaths resourcePaths = m_database.GetForwardResourcePaths( resourcePath );
	if ( !resourcePaths.Empty() )
	{
		red::DynArray< red::StringView > strings{ resourcePaths.Size() };
		std::transform( resourcePaths.Begin(), resourcePaths.End(), strings.Begin(), 
			[]( const res::ResourcePath& resPath ) -> red::StringView { return resPath.ToStringView(); } );
		std::sort( strings.Begin(), strings.End() );
		String text = red::StrJoin( strings, "\n\t" );

		m_printFunction( "\t" );
		m_printFunction( text );
		m_printFunction( "\n" );
	}
}


void ResourcesCommands::PrintTraceReverseDeps( const res::ResourcePath& resourcePath, const String& prefix, red::HashSet< res::ResourcePath >& visitedPaths, red::HashSet< res::ResourcePath >& allVisitedPaths )
{
	m_printFunction( prefix );
	m_printFunction( resourcePath.ToStringView() );
	m_printFunction( "\n" );

	allVisitedPaths.Insert( resourcePath );
	visitedPaths.Insert( resourcePath );

	resdb::Links links = m_database.GetBackwardLinks( resourcePath );
	if ( !links.Empty() )
	{
		red::DynArray< res::ResourcePath > paths{ links.Size() };
		std::transform( links.begin(), links.end(), paths.Begin(), 
			[]( const resdb::Link& link ) -> res::ResourcePath { return link.from; } );
		red::alg::RemoveDuplicates( paths );
		std::sort( paths.Begin(), paths.End(), res::SortByPathString );

		const String indent = prefix + "\t";
		for ( const auto& path : paths )
		{
			if ( visitedPaths.Exist( path ) )
			{
				m_printFunction( indent );
				m_printFunction( "(Recursion - " );
				m_printFunction( path.ToStringView() );
				m_printFunction( ")\n" );
			}
			else if ( allVisitedPaths.Exist( path ) )
			{
				m_printFunction( indent );
				m_printFunction( "(Already Visited - " );
				m_printFunction( path.ToStringView() );
				m_printFunction( ")\n" );
			}
			else
			{
				PrintTraceReverseDeps( path, indent, visitedPaths, allVisitedPaths );
			}
		}
	}

	visitedPaths.Remove( resourcePath );
}

void ResourcesCommands::PrintTraceForwardDeps( const res::ResourcePath& resourcePath, const String& prefix, red::HashSet< res::ResourcePath >& visitedPaths, red::HashSet< res::ResourcePath >& allVisitedPaths )
{
	m_printFunction( prefix );
	m_printFunction( resourcePath.ToStringView() );
	m_printFunction( "\n" );

	allVisitedPaths.Insert( resourcePath );
	visitedPaths.Insert( resourcePath );

	resdb::Links links = m_database.GetForwardLinks( resourcePath );
	if ( !links.Empty() )
	{
		red::DynArray< res::ResourcePath > paths{ links.Size() };
		std::transform( links.begin(), links.end(), paths.Begin(), 
			[]( const resdb::Link& link ) -> res::ResourcePath { return link.to; } );
		red::alg::RemoveDuplicates( paths );
		std::sort( paths.Begin(), paths.End(), res::SortByPathString );

		const String indent = prefix + "\t";
		for ( const auto& path : paths )
		{
			if ( visitedPaths.Exist( path ) )
			{
				m_printFunction( indent );
				m_printFunction( "(Recursion - " );
				m_printFunction( path.ToStringView() );
				m_printFunction( ")\n" );
			}
			else if ( allVisitedPaths.Exist( path ) )
			{
				m_printFunction( indent );
				m_printFunction( "(Already Visited - " );
				m_printFunction( path.ToStringView() );
				m_printFunction( ")\n" );
			}
			else
			{
				PrintTraceForwardDeps( path, indent, visitedPaths, allVisitedPaths );
			}
		}
	}

	visitedPaths.Remove( resourcePath );
}

void ResourcesCommands::PrintLoadingOrderList( const res::ResourcePath& resourcePath )
{
	// What we are doing here is printing a list of resources in the way they would be submitted
	// to the IO system for loading. Normally we load resources depth-first, well LIFO/FILO.
	// As in a resource's dependencies need to be loaded first and then the resource is considered loaded.
	// However the resources are submitted to IO in more of a breadth first manner, which is what 
	// we want to describe here.
	// Resources will not be included more than once because they should only need to be loaded once.

	red::DynArray< res::ResourcePath > resources;
	resources.PushBack( resourcePath );

	red::HashSet< res::ResourcePath > visited;
	visited.Insert( resourcePath );

	for ( Uint32 i = 0; i < resources.Size(); ++i )
	{
		const auto& path = resources[i];

		resdb::Links links = m_database.GetForwardLinks( path );
		for ( const auto& link : links )
		{
			if ( link.type == resdb::Link::Type::HARD && link.location == resdb::Link::Location::PRIMARY )
			{
				const auto& linkPath = link.to;
				if ( visited.Insert( linkPath ).IsSuccessful() )
				{
					resources.PushBack( linkPath );
				}
			}
		}
	}

	m_printFunction( "\n" );
	m_printFunction( resourcePath.ToStringView() );
	m_printFunction( "\n" );

	for ( Uint32 i = 1; i < resources.Size(); ++i )
	{
		const auto& path = resources[i];

		m_printFunction( "\t" );
		m_printFunction( path.ToStringView() );
		m_printFunction( "\n" );
	}
}

//------------------------------------------------------------------------------

void ResourcesCommands::Cmd_ForwardDependencies( CommandParameters params )
{
	auto paths = ParseParamsToResourcePaths( params );

	for ( const auto& resourcePath : paths )
	{
		m_printFunction( "\n" );
		m_printFunction( resourcePath.ToStringView() );
		m_printFunction( "\n" );

		PrintForwardDeps( resourcePath );
	}
}

void ResourcesCommands::Cmd_ReverseDependencies( CommandParameters params )
{
	auto paths = ParseParamsToResourcePaths( params );

	for ( const auto& resourcePath : paths )
	{
		m_printFunction( "\n" );
		m_printFunction( resourcePath.ToStringView() );
		m_printFunction( "\n" );

		PrintReverseDeps( resourcePath );
	}
}

void ResourcesCommands::Cmd_ResolvedDependencies( CommandParameters params )
{
	auto paths = ParseParamsToResourcePaths( params );

	for ( const auto& resourcePath : paths )
	{
		m_printFunction( "\n" );
		m_printFunction( resourcePath.ToStringView() );
		m_printFunction( "\n" );

		PrintResolvedDeps( resourcePath );
	}
}

void ResourcesCommands::Cmd_ResourceInfo( CommandParameters params )
{
	auto paths = ParseParamsToResourcePaths( params );

	for ( const auto& resourcePath : paths )
	{
		const resdb::Resource* resource = m_database.LookupResource( resourcePath );

		m_printFunction( "\n" );
		m_printFunction( resourcePath.ToStringView() );
		m_printFunction( "\n" );
		String hashString = String::Printf( "0x%016llX\n", resourcePath.GetHash() );
		m_printFunction( hashString );
		if ( resource == nullptr )
		{
			m_printFunction( "\tNot Found!\n" );
			continue;
		}

		auto sizeInfo = String::Printf( "\tTotal Size: %u\n\tObject Size: %u\n\tBuffer Size: %u\n", resource->totalSize, resource->objectSize, resource->bufferSize );
		m_printFunction( sizeInfo );

		m_printFunction( "Dependencies:\n" );
		PrintForwardDeps( resourcePath );

		m_printFunction( "Dependents:\n" );
		PrintReverseDeps( resourcePath );
	}
}

void ResourcesCommands::Cmd_TopResources( CommandParameters params )
{
	red::ArraySpan< const resdb::Resource > resources = m_database.GetResources();

	static constexpr Uint32 DEFAULT_ELEMENTS = 20;
	Uint32 elements = DEFAULT_ELEMENTS;
	if ( !params.Empty() )
	{
		red::StringView param = params.PopFront();
		elements = std::atol( param.Data() );
		if ( elements == 0 )
		{
			elements = DEFAULT_ELEMENTS;
		}
	}

	using SortFunction = bool (*)(const resdb::Resource&, const resdb::Resource&);
	SortFunction sortFunction;
	String sortName;
	Uint32 sortParamHash = params.Empty() ? 0 : HashParam( params.PopFront() );
	switch ( sortParamHash )
	{
	case Hash( "object" ):
	case Hash( "objectsize" ):
	case Hash( "object-size" ):
		sortFunction = resdb::SortByObjectSize;
		sortName = "object size";
		break;

	case Hash( "buffer" ):
	case Hash( "buffersize" ):
	case Hash( "buffer-size" ):
		sortFunction = resdb::SortByBufferSize;
		sortName = "buffer size";
		break;

	case Hash( "size" ):
	case Hash( "totalsize" ):
	case Hash( "total-size" ):
	default:
		sortFunction = resdb::SortByTotalSize;
		sortName = "total size";
		break;
	};

	red::DynArray< resdb::Resource > topResources{ elements };
	std::partial_sort_copy( resources.begin(), resources.end(), topResources.Begin(), topResources.End(), sortFunction );

	auto desc = String::Printf( "Top %u resources (by %hs)\n", elements, sortName.AsChar() );
	desc += "Total Size\tObject Size\tBuffer Size\tResource\n";
	m_printFunction( desc );

	for ( const auto& res : topResources )
	{
		auto text = String::Printf( "%10u\t%10u\t%10u\t%hs\n", res.totalSize, res.objectSize, res.bufferSize, res.path.ToDebugString() );
		m_printFunction( text );
	}
}

void ResourcesCommands::Cmd_LinkMapping( CommandParameters params )
{
	red::Map< res::ResourcePath, res::ResourcePath > sortedMap;
	for ( const auto link : m_fileLinkMap.Mapping() )
	{
		sortedMap.InsertUnsorted( link.Key(), link.Value() );
	}
	sortedMap.MakeClean();

	for ( const auto link : sortedMap )
	{
		auto text = String::Printf( "%hs -> %hs\n", link.Key().ToDebugString(), link.Value().ToDebugString() );
		m_printFunction( text );
	}
}

void ResourcesCommands::Cmd_MissingLinkResources( CommandParameters params )
{
	auto missingResources = m_fileLinkMap.MissingResources();
	std::sort( missingResources.Begin(), missingResources.End(), res::SortByDirectory );
	m_printFunction( "Missing Resources (from link files):\n" );
	for ( const auto missingPath : missingResources )
	{
		auto text = String::Printf( "%hs\n", missingPath.ToDebugString() );
		m_printFunction( text );
	}
}

void ResourcesCommands::Cmd_MissingResources( CommandParameters params )
{
	red::DynArray< res::ResourcePath > toResources;
	if ( !params.Empty() )
	{
		auto paths = ParseParamsToResourcePaths( params );
		red::DynArray< resdb::Link > links;
		for ( const auto& path : paths )
		{
			links.PushBack( m_database.GetForwardLinks( path ) );
		}
		toResources = resdb::Database::ExtractToPaths( links );
	}
	else
	{
		toResources = resdb::Database::ExtractToPaths( m_database.GetAllForwardLinks() );
	}

	red::alg::RemoveDuplicates( toResources );

	auto diskResources = m_database.GetResourcePaths();

	red::DynArray< res::ResourcePath > missingResources;
	missingResources.Reserve( toResources.Size() );

	auto toIter = toResources.Begin(), toEnd = toResources.End();
	auto diskIter = diskResources.Begin(), diskEnd = diskResources.End();
	while ( toIter != toEnd && diskIter != diskEnd )
	{
		if ( *toIter < *diskIter )
		{
			missingResources.PushBack( *toIter );
			++toIter;
		}
		else if ( *diskIter < *toIter )
		{
			++diskIter;
		}
		else
		{
			++toIter;
			++diskIter;
		}
	}
	while ( toIter != toEnd )
	{
		missingResources.PushBack( *toIter );
		++toIter;
	}

	std::sort( missingResources.Begin(), missingResources.End(), res::SortByDirectory );
	m_printFunction( "Missing Resources (from resources):\n" );
	for ( const auto missingPath : missingResources )
	{
		auto text = String::Printf( "%hs\n", missingPath.ToDebugString() );
		m_printFunction( text );
	}
}

void ResourcesCommands::Cmd_ShowExtensionsHistogram( CommandParameters params )
{
	struct Entry
	{
		Uint32 count;
		red::StringView ext;
	};
	red::DynArray< Entry > entries;
	for ( const auto ext : m_extensions )
	{
		entries.PushBack( { ext.Value(), ext.Key() } );
	}

	std::sort( entries.Begin(), entries.End(), []( const Entry& left, const Entry& right ) -> bool
	{
		return left.count > right.count;
	} );

	for ( const auto& entry : entries )
	{
		auto text = String::Printf( "%10u - %hs\n", entry.count, entry.ext.Data() );
		m_printFunction( text );
	}
}

namespace
{

red::DynArray< red::String > GetDeprecatedExtensions()
{
	red::DynArray< red::String > deprecatedExtensions;

	red::DynArray< const rtti::ClassType* > resourceClasses;
	GetRttiSystem().EnumClasses( ClassID<CResource>(), resourceClasses );

	deprecatedExtensions.Reserve( resourceClasses.Size() );
	for ( const rtti::ClassType* resourceClass : resourceClasses )
	{
		CResource* defaultResource = resourceClass->GetDefaultObject< CResource >();
		if ( defaultResource )
		{
			String ext = defaultResource->GetExtension();
			String deprecatedExt = defaultResource->GetDeprecatedExtension();
			if ( !deprecatedExt.Empty() && ext != deprecatedExt )
			{
				deprecatedExtensions.PushBack( deprecatedExt );
			}
		}
	}

	return deprecatedExtensions;
}

}

void ResourcesCommands::Cmd_ShowDeprecatedExtensions( CommandParameters params )
{
	auto deprecatedExtensions = GetDeprecatedExtensions();

	m_printFunction( "Deprecated Extensions\n" );
	for ( const auto& ext : deprecatedExtensions )
	{
		auto text = String::Printf( "\t%hs\n", ext.AsChar() );
		m_printFunction( text );
	}
}

void ResourcesCommands::Cmd_ShowDeprecatedResources( CommandParameters params )
{
	auto deprecatedExtensions = GetDeprecatedExtensions();

	red::HashMap< red::String, red::DynArray< res::ResourcePath > > deprecatedResources;
	deprecatedResources.Reserve( deprecatedExtensions.Size() );
	for ( const auto& ext : deprecatedExtensions )
	{
		deprecatedResources.Insert( ext, {} );
	}

	for ( const auto& path : m_allResourcePaths )
	{
		const red::String ext = red::paths::ExtractExtension( path.ToStringView() );
		auto* elem = deprecatedResources.FindPtr( ext );
		if ( elem )
		{
			elem->PushBack( path );
		}
	}

	for ( auto& depRes : deprecatedResources )
	{
		const String& ext = depRes.Key();
		auto extString = String::Printf( "Deprecated extension: '%hs'\n", ext.AsChar() );
		m_printFunction( extString );

		auto& resources = depRes.Value();
		std::sort( resources.Begin(), resources.End(), res::SortByDirectory );
		for ( const auto& path : resources )
		{
			auto pathString = String::Printf( "\t%hs\n", path.ToDebugString() );
			m_printFunction( pathString );
		}
	}
}

void ResourcesCommands::Cmd_ShowGatheredResources( CommandParameters params )
{
	m_printFunction( "Gathered Resources:\n" );
	for ( const auto* gatheredResource : m_gatheredResources )
	{
		red::DynArray< red::StringView > flags;
		if ( gatheredResource->IsEngine() ) flags.PushBack( "Engine" );
		if ( gatheredResource->IsBackend() ) flags.PushBack( "Backend" );
		if ( gatheredResource->IsFallback() ) flags.PushBack( "Fallback" );
		if ( gatheredResource->IsDebug() ) flags.PushBack( "Debug" );
		if ( gatheredResource->IsCooked() ) flags.PushBack( "Cooked" );

		auto flagString = red::StrJoin( flags, " " );

		const auto& path = gatheredResource->GetPath();
		auto text = String::Printf( "%hs\t%hs\n", path.ToDebugString(), flagString.AsChar() );
		m_printFunction( text );
	}
}

void ResourcesCommands::Cmd_FilterResource( CommandParameters params )
{
	for ( const auto& pattern : params )
	{
		if ( !red::IsGlobString( pattern ) )
		{
			continue;
		}

		red::String text{ "Matching '" };
		text.Append( pattern.Data(), pattern.Length() );
		text += "'\n";
		m_printFunction( text );

		for ( const auto& resourcePath : m_allResourcePaths )
		{
			if ( red::GlobMatch( resourcePath.ToStringView(), pattern ) )
			{
				auto matched = String::Printf( "\t%hs\n", resourcePath.ToDebugString() );
				m_printFunction( matched );
			}
		}
	}
}

void ResourcesCommands::Cmd_TraceReverseDependencies( CommandParameters params )
{
	auto paths = ParseParamsToResourcePaths( params );

	for ( const auto& resourcePath : paths )
	{
		m_printFunction( "\n" );
		PrintTraceReverseDeps( resourcePath, "", red::HashSet< res::ResourcePath >(), red::HashSet< res::ResourcePath >() );
		m_printFunction( "\n" );
	}
}

void ResourcesCommands::Cmd_TraceForwardDependencies( CommandParameters params )
{
	auto paths = ParseParamsToResourcePaths( params );

	for ( const auto& resourcePath : paths )
	{
		m_printFunction( "\n" );
		PrintTraceForwardDeps( resourcePath, "", red::HashSet< res::ResourcePath >(), red::HashSet< res::ResourcePath >() );
		m_printFunction( "\n" );
	}
}

void ResourcesCommands::Cmd_ListAllDependencies( CommandParameters params )
{
	auto paths = ParseParamsToResourcePaths( params );

	for ( const auto& resourcePath : paths )
	{
		const auto allDeps = m_database.GetRecursiveForwardPaths( resourcePath );

		m_printFunction( "\n" );
		m_printFunction( resourcePath.ToStringView() );
		m_printFunction( "\n" );
		for ( const auto& depPath : allDeps )
		{
			auto text = String::Printf( "\t%hs\n", depPath.ToDebugString() );
			m_printFunction( text );
		}
	}
}

void ResourcesCommands::Cmd_ShowLoadingOrderList( CommandParameters params )
{
	auto paths = ParseParamsToResourcePaths( params );
	for ( const auto& resourcePath : paths )
	{
		m_printFunction( "\n" );
		PrintLoadingOrderList( resourcePath );
		m_printFunction( "\n" );
	}
}

void ResourcesCommands::Cmd_FindResources( CommandParameters params )
{
	if ( params.Empty() )
	{
		m_printFunction( "Error: Missing argument" );
		return;
	}

	auto paths = ParseParamsToResourcePaths( params );

	for ( const auto& path : paths )
	{
		const String output = String::Printf( "0x%016llX -> %hs\n", path.GetHash(), path.ToDebugString() );
		m_printFunction( output );
	}
}

void ResourcesCommands::Cmd_ListResources( CommandParameters params )
{
	auto paths = ParseParamsToResourcePaths( params );
	for ( const auto resourcePath : paths )
	{
		m_printFunction( resourcePath.ToDebugString() );
		m_printFunction( "\n" );
	}
}

void ResourcesCommands::Cmd_ListScriptResources( CommandParameters params )
{
	auto paths = dependency::GetResourcesFromScripts();
	for ( const auto resourcePath : paths )
	{
		m_printFunction( resourcePath.ToDebugString() );
		m_printFunction( "\n" );
	}
}

void ResourcesCommands::Cmd_ListTweakResources( CommandParameters params )
{
	auto paths = dependency::GetResourcesFromTweakDB();
	for ( const auto resourcePath : paths )
	{
		m_printFunction( resourcePath.ToDebugString() );
		m_printFunction( "\n" );
	}
}

void ResourcesCommands::Cmd_ListCodeResources( CommandParameters params )
{
	auto paths = dependency::GetResourcesFromGame();
	for ( const auto resourcePath : paths )
	{
		m_printFunction( resourcePath.ToDebugString() );
		m_printFunction( "\n" );
	}
}

void ResourcesCommands::Cmd_AreResourcesReachable( CommandParameters params )
{
}

void ResourcesCommands::Cmd_ListBrokenDirectoryResourcePaths( CommandParameters params )
{
	const auto& links = m_database.GetAllBackwardLinks();
	auto iter = links.begin(), end = links.end(); 
	while ( iter != end )
	{
		const auto& link = *iter;
		if ( link.to.ToStringView().EndsWith( "\\" ) )
		{
			m_printFunction( link.to.ToDebugString() );
			m_printFunction( "\n" );

			do
			{
				m_printFunction( "\t" );
				m_printFunction( iter->from.ToDebugString() );
				m_printFunction( "\n" );
				++iter;
			}
			while ( iter != end && iter->to == link.to );
		}

		while ( iter != end && iter->to == link.to )
		{
			++iter;
		}
	}
}

void ResourcesCommands::Cmd_ListMostReferencedResources( CommandParameters params )
{
	const auto& allResourcePaths = m_database.GetResourcePaths();

	struct Info
	{
		res::ResourcePath path;
		Uint32 count;
	};
	red::DynArray< Info > referenceCounts;
	referenceCounts.Reserve( allResourcePaths.Size() );

	{
		const auto& backwardLinks = m_database.GetAllBackwardLinks();
		auto iter = backwardLinks.begin(), end = backwardLinks.end();
		while ( iter != end )
		{
			auto start = iter;
			Uint32 count = 0;
			do
			{
				++count;
				++iter;
			}
			while ( start->to == iter->to );

			referenceCounts.PushBack( { start->to, count } );
		}
	}

	std::sort( referenceCounts.Begin(), referenceCounts.End(), []( const Info& left, const Info& right ) -> bool
	{
		if ( left.count == right.count )
		{
			return left.path < right.path;
		}
		return left.count > right.count;
	} );

	for ( auto iter = referenceCounts.Begin(), end = referenceCounts.End(); iter != end && iter->count >= 1000; ++iter )
	{
		m_printFunction( iter->path.ToDebugString() );
		m_printFunction( String::Printf( "\t%8d\n", iter->count ) );
	}
}


//------------------------------------------------------------------------------

bool ResourcesCommands::ExecuteCommand( Uint32 commandHash, CommandParameters arguments )
{
	switch ( commandHash )
	{
	case Hash( "depends" ):
	case Hash( "deps" ):
	case Hash( "forward" ):
		Cmd_ForwardDependencies( arguments );
		break;

	case Hash( "reverse-depends" ):
	case Hash( "reverse-deps"):
	case Hash( "reverse" ):
		Cmd_ReverseDependencies( arguments );
		break;

	case Hash( "resolve" ):
		Cmd_ResolvedDependencies( arguments );
		break;

	case Hash( "all-deps" ):
		Cmd_ListAllDependencies( arguments );
		break;

	case Hash( "trace" ):
		Cmd_TraceReverseDependencies( arguments );
		break;

	case Hash( "deps-tree" ):
		Cmd_TraceForwardDependencies( arguments );
		break;

	case Hash( "loading-order" ):
		Cmd_ShowLoadingOrderList( arguments );
		break;

	case Hash( "info" ):
	case Hash( "resource" ):
		Cmd_ResourceInfo( arguments );
		break;

	case Hash( "linkmap" ):
	case Hash( "link-map" ):
	case Hash( "link-mapping" ):
		Cmd_LinkMapping( arguments );
		break;

	case Hash( "extensions" ):
		Cmd_ShowExtensionsHistogram( arguments );
		break;

	case Hash( "deprecated-extensions" ):
		Cmd_ShowDeprecatedExtensions( arguments );
		break;

	case Hash( "deprecated-resources" ):
		Cmd_ShowDeprecatedResources( arguments );
		break;

	case Hash( "gathered-resources" ):
		Cmd_ShowGatheredResources( arguments );
		break;

	case Hash( "top" ):
	case Hash( "top-resources" ):
		Cmd_TopResources( arguments );
		break;

	case Hash( "missing-link-resources" ):
		Cmd_MissingLinkResources( arguments );
		break;

	case Hash( "missing-resources" ):
		Cmd_MissingResources( arguments );
		break;

	case Hash( "filter" ):
	case Hash( "filter-resources" ):
		Cmd_FilterResource( arguments );
		break;

	case Hash( "find" ):
		Cmd_FindResources( arguments );
		break;

	case Hash( "list" ):
		Cmd_ListResources( arguments );
		break;

	case Hash( "list-tweak-resources" ):
		Cmd_ListTweakResources( arguments );
		break;

	case Hash( "list-script-resources" ):
		Cmd_ListScriptResources( arguments );
		break;

	case Hash( "list-code-resources" ):
		Cmd_ListCodeResources( arguments );
		break;

	case Hash( "list-directory-resources" ):
		Cmd_ListBrokenDirectoryResourcePaths( arguments );
		break;

	case Hash( "most-referenced" ):
		Cmd_ListMostReferencedResources( arguments );
		break;

	default:
		return false;
	};

	return true;
}

const char* ResourcesCommands::CommandsHelpText() const
{
	return R"text(Available commands:
(info|resource) <path> (<path>...)
Prints info about a resource, including sizes and dependencies, for the specified paths

(deps|depends|forward) <path> (<path>...)
Prints list of forward dependencies for each of the specified paths

(reverse|reverse-deps|reverse-depends) <path> (<path>...)
Prints list of reverse dependencies for each of the specified paths

resolve <path> (<path>...)
Prints list of forward dependencies with links resolved for each of the specified paths

all-deps <path> (<path>...)
Prints a list of all unique dependencies (including dependencies of dependencies) for a resource

trace <path> (<path>...)
Prints a tree of reverse dependencies until complete

deps-tree <path> (<path>...)
Prints a tree of forward dependencies for the given resources (Warning this could be huge!)

loading-order <path> (<path>...)
Prints a list of resources which would be loaded when requesting the given resources, in the order which they would be submitted to IO / loading system

(linkmap|link-map|link-mapping)
Prints out the mapping of link files to resulting resources

(top|top-resources) <count> <sorting>
Prints out top <count> resources sorted by the <sorting>, where <sorting> can be (object|buffer|size)

missing-link-resources
Prints a list of missing resources that have been referenced in link files

missing-resources (<path>...)
Prints a list of missing resources that are referenced from other resources and not link files

gathered-resources
Prints a list of gathered resources being used in the engine

(filter|filter-resources) <pattern> (<pattern>...)
Prints a list of filtered resources based on the patterns specified

extensions
Prints a histogram of found extensions in the resource depot, this includes all files and not only resources

deprecated-extensions
Prints a list of deprecated extensions that exist in the resource system

deprecated-resources
Prints a list of resources that are using deprecated extensions in the resource depot

find <path> (<path>...)
Find a path(s) by hash or list all the resources in a given directory

list <path> (<path>...)
Lists all resources found in those paths. This is mostly useful with directories (ending with \\) or glob patterns

list-tweak-resources
Lists all of the resources referenced from the final tweakdb bin file

list-script-resources
Lists all the resources referenced from the final scripts blob file

list-code-resources
Lists all the resources referenced from gathered resource references in code (hacked for now)

most-referenced
Lists the resources which are referenced very many times

)text";

}
