/*
 * Copyright (c) 2019 CD Projekt Red. All Rights Reserved.
 */

#pragma once

#include "../../../common/resource/include/backendResourceDepot.h"
#include "../../backendEngine/include/commandletUtils.h"
#include "../../backendData/include/resourceDatabase.h"
#include "../../backendData/include/fileLinkMap.h"

namespace res
{
class ResourcePath;
class GatheredResource;
}

class ResourcesCommands
{
public:
	ResourcesCommands();
	~ResourcesCommands();

	using PrintFunction = std::function<void ( red::StringView )>;
	void Initialise( commandlet::BackendDepotHolder& depotHolder, PrintFunction&& print );
	void Shutdown();

	using CommandParameters = red::ArraySpan< red::StringView >;

	bool ExecuteCommand( Uint32 commandHash, CommandParameters arguments );
	const char* CommandsHelpText() const;

	void Cmd_ForwardDependencies( CommandParameters params );
	void Cmd_ReverseDependencies( CommandParameters params );
	void Cmd_ResolvedDependencies( CommandParameters params );
	void Cmd_ResourceInfo( CommandParameters params );
	void Cmd_TopResources( CommandParameters params );
	void Cmd_LinkMapping( CommandParameters params );
	void Cmd_MissingLinkResources( CommandParameters params );
	void Cmd_MissingResources( CommandParameters params );
	void Cmd_ShowExtensionsHistogram( CommandParameters params );
	void Cmd_ShowDeprecatedExtensions( CommandParameters params );
	void Cmd_ShowDeprecatedResources( CommandParameters params );
	void Cmd_ShowGatheredResources( CommandParameters params );
	void Cmd_FilterResource( CommandParameters params );
	void Cmd_TraceReverseDependencies( CommandParameters params );
	void Cmd_TraceForwardDependencies( CommandParameters params );
	void Cmd_ListAllDependencies( CommandParameters params );
	void Cmd_ShowLoadingOrderList( CommandParameters params );
	void Cmd_FindResources( CommandParameters params );
	void Cmd_ListResources( CommandParameters params );
	void Cmd_ListScriptResources( CommandParameters params );
	void Cmd_ListTweakResources( CommandParameters params );
	void Cmd_ListCodeResources( CommandParameters params );
	void Cmd_AreResourcesReachable( CommandParameters params );
	void Cmd_ListBrokenDirectoryResourcePaths( CommandParameters params );
	void Cmd_ListMostReferencedResources( CommandParameters params );

private:
	red::DynArray< res::ResourcePath > ParseParamsToResourcePaths( CommandParameters params );
	void PrintForwardDeps( const res::ResourcePath& resourcePath );
	void PrintReverseDeps( const res::ResourcePath& resourcePath );
	void PrintResolvedDeps( const res::ResourcePath& resourcePath );
	void PrintTraceReverseDeps( const res::ResourcePath& resourcePath, const String& prefix, red::HashSet< res::ResourcePath >& visitedPaths, red::HashSet< res::ResourcePath >& allVisitedPaths );
	void PrintTraceForwardDeps( const res::ResourcePath& resourcePath, const String& prefix, red::HashSet< res::ResourcePath >& visitedPaths, red::HashSet< res::ResourcePath >& allVisitedPaths );
	void PrintLoadingOrderList( const res::ResourcePath& resourcePath );

	res::IBackendResourceDepot* m_depot;

	resdb::Database m_database;
	commandlet::FileLinkMap m_fileLinkMap;

	red::HashMap< red::String, Uint32 > m_extensions;
	red::DynArray< res::ResourcePath > m_allResourcePaths;
	red::DynArray< res::GatheredResource* > m_gatheredResources;

	PrintFunction m_printFunction;
};

namespace resources_commands_helper
{

// Convenience constexpr hash function
constexpr Uint32 Hash( const char* str )
{
	return red::CalculateHash32( str );
}

RED_INLINE Uint32 HashParam( red::StringView str )
{
	return red::CalculateHash32( str.Data(), str.Length(), RED_FNV_OFFSET_BASIS32 );
}

} // resources_commands_helper

