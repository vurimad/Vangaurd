/**
 * Copyright (c) 2019 CD Projekt Red. All Rights Reserved.
 */

#include "build.h"

#include "../../../common/core/include/commandlet.h"

#include "../../../common/resource/include/resourceEditorDepotImpl.h"
#include "../../../common/redCore/include/commandline.h"
#include "../../../common/redFileSystem/include/fileSys.h"
#include "../../backendEngine/include/commandletEngineInstance.h"
#include "../../backendData/include/resourceDatabase.h"
#include "../../backendData/include/dataTypes.h"
#include "../../backendEngine/include/commandletUtils.h"
#include "../../backendEngine/include/resourceUtils.h"
#include "../../../common/world/include/worldPrefab.h"
#include "../../../common/world/include/worldPrefabNode.h"
#include "../../../common/worldStatic/include/staticMeshNode.h"
#include "../../../common/worldStatic/include/bendedMeshNode.h"

class RoadResourcesCommandlet : public red::ICommandlet
{
	RTTI_DECLARE_TYPE( RoadResourcesCommandlet );

public:
	RoadResourcesCommandlet();
	virtual ~RoadResourcesCommandlet() = default;

	// ICommandlet interface
	virtual Bool InitializeEnvironment() override;
	virtual void TeardownEnvironment() override;
	virtual void PrintHelp() const override;
	virtual const char* GetOneLiner() const override;
	virtual Bool Execute( const red::CommandLine& options ) override;

private:
	Bool ParseSettings( const red::CommandLine& options );
	void Handle( THandle< world::PrefabNode > node );
	void Handle( THandle< world::BendedMeshNode > node );
	void Handle( THandle< world::MeshNode > node, Bool root = false );
	void Handle( THandle< world::NodesGroup> group );

	struct Settings
	{
		res::ResourcePath startPrefab;
		red::AbsolutePath outputPath;
	} m_settings{};

	struct Output
	{
		red::HashSet< res::ResourcePath > proxiex;
		red::HashSet< res::ResourcePath > meshes;
		red::HashSet< res::ResourcePath > checkerMeshes;
		red::HashSet< res::ResourcePath > bentMeshes;
		red::HashSet< res::ResourcePath > prefabs;
	} m_output{};

	commandlet::EngineInstance m_engineInstance;
	red::UniquePtr< resdb::Database > m_ownedSourceDepotDatabase;
	res::ResourceEditorDepot* m_sourceDepot{};
	resdb::Database* m_sourceDepotDatabase{};
};

RTTI_BEGIN_TYPE( RoadResourcesCommandlet )
	RTTI_PARENT_TYPE( red::ICommandlet )
RTTI_END_TYPE()

RoadResourcesCommandlet::RoadResourcesCommandlet()
	: ICommandlet( RED_NAME_CONSTEXPR( "road-resources" ) )
{
}

Bool RoadResourcesCommandlet::InitializeEnvironment()
{
	if ( !m_engineInstance.Initialise() )
	{
		return false;
	}

	m_ownedSourceDepotDatabase = red::CreateUniquePtr< resdb::Database >();

	if ( !m_ownedSourceDepotDatabase )
	{
		return false;
	}

	m_sourceDepotDatabase = m_ownedSourceDepotDatabase.Get();
	m_sourceDepot = commandlet::GetEditorDepot();

	if ( !m_sourceDepot )
	{
		return false;
	}

	const red::AbsolutePath& sourceCachePath = GFileManager->GetCacheDirectory();
	const red::AbsolutePath sourceDatabasePath = sourceCachePath.AddFilePath( resdb::Database::GetDefaultFilename() );

	m_ownedSourceDepotDatabase->Initialise( *m_sourceDepot, sourceDatabasePath );

	return true;
}

void RoadResourcesCommandlet::TeardownEnvironment()
{
	m_engineInstance.Shutdown();
}

void RoadResourcesCommandlet::PrintHelp() const
{
	RED_LOG( "Prints to file resources used in compiled roads prefab (proxies, meshes, prefabs, bent meshes)\n\n" );
	RED_LOG( "Usage:" );
	RED_LOG( "  road-resources [-prefab=<file>] [-extension=<extension>]" );
	RED_LOG( "Parameters:" );
	RED_LOG( "  -prefab=<file>                    - resource path of start prefab... roads.prefab" );
	RED_LOG( "  -output=<file>                    - where to save report (defaults to: c:\\road_resources_used.txt )" );
}

Bool RoadResourcesCommandlet::Execute( const red::CommandLine& options )
{
	if ( !ParseSettings( options ) )
	{
		RED_LOG( "Failed to parse settings from commandline.." );

		PrintHelp();
		RED_LOG_FLUSH_AND_WAIT();
		return false;
	}
	
	auto prefabToken = GResourceLoader->IssueLoadingRequest( m_settings.startPrefab );
	prefabToken->WaitUntilLoaded();
	if (prefabToken->HasFailed())
	{
		RED_LOG( "Failed loading star prefab..." );
		RED_LOG_FLUSH_AND_WAIT();
		return false;
	}

	const THandle< world::Prefab > prefab = Cast<world::Prefab>( prefabToken->GetResource() );
	THandle< world::NodesGroup > rootGroup;
	rootGroup = prefab->GetRootGroup();
	if (!rootGroup)
	{
		return false;
	}

	for (const world::NodePtr node : rootGroup->GetAllNodes())
	{
		if (THandle< world::PrefabNode > prefabNode = Cast< world::PrefabNode >( node ))
		{
			Handle( prefabNode );
		}
		else if (auto meshNode = Cast< world::MeshNode >( node ))
		{
			Handle( meshNode, true );
		}
		else if (auto meshNode = Cast< world::BendedMeshNode >( node ))
		{
			Handle( meshNode );
		}
	}


	if (!m_settings.outputPath.Empty())
	{
		GFileManager->CreatePath( m_settings.outputPath );
		FILE *outFile = fopen( m_settings.outputPath.AsChar(), "w" );

		for (auto const &path : m_output.prefabs)
		{
			const auto absolutePath = m_sourceDepot->TranslateResourcePath( path );
			const String str = absolutePath.AsString();
			fwrite( str.AsChar(), str.Length(), 1, outFile );	fputs( "\n", outFile );
		}
		for (auto const &path : m_output.proxiex)
		{
			const auto absolutePath = m_sourceDepot->TranslateResourcePath( path );
			const String str = absolutePath.AsString();
			fwrite( str.AsChar(), str.Length(), 1, outFile );	fputs( "\n", outFile );
		}
		for (auto const &path : m_output.meshes)
		{
			const auto absolutePath = m_sourceDepot->TranslateResourcePath( path );
			const String str = absolutePath.AsString();
			fwrite( str.AsChar(), str.Length(), 1, outFile );	fputs( "\n", outFile );
		}
		for (auto const &path : m_output.bentMeshes)
		{
			const auto absolutePath = m_sourceDepot->TranslateResourcePath( path );
			const String str = absolutePath.AsString();
			fwrite( str.AsChar(), str.Length(), 1, outFile );	fputs( "\n", outFile );
		}
		for (auto const &path : m_output.checkerMeshes)
		{
			const auto absolutePath = m_sourceDepot->TranslateResourcePath( path );
			const String str = absolutePath.AsString();
			fwrite( str.AsChar(), str.Length(), 1, outFile );	fputs( "\n", outFile );
		}
		fclose( outFile );
	}

	return true;
}

void RoadResourcesCommandlet::Handle( THandle< world::PrefabNode > srcPrefab )
{
	res::ResourcePath inputPrefabPath = srcPrefab->GetPrefab().GetPath();
	m_output.prefabs.Insert( inputPrefabPath );

	auto token = srcPrefab->GetPrefab().IssueLoadingRequest();
	auto resource = token->WaitUntilLoaded();
	auto prefab = ( world::Prefab* )resource.Get();

	if(!prefab->GetProxyMesh().GetPath().IsEmpty()) m_output.proxiex.Insert( prefab->GetProxyMesh().GetPath() );

	THandle< world::NodesGroup > rootGroup;
	rootGroup = prefab->GetRootGroup();

	if (!rootGroup) return;
	for (const world::NodePtr node : rootGroup->GetAllNodes())
	{
		if (THandle< world::PrefabNode > prefabNode = Cast< world::PrefabNode >( node ))
		{
			Handle( prefabNode );
		}
		else if (auto meshNode = Cast< world::MeshNode >( node ))
		{
			Handle( meshNode );
		}
		else if (auto meshNode = Cast< world::BendedMeshNode >( node ))
		{
			Handle( meshNode );
		}
	}

	for (auto& nodesGroup : rootGroup->GetSubGroups())
	{
		Handle( nodesGroup );
	}
}





void RoadResourcesCommandlet::Handle( THandle< world::BendedMeshNode > node )
{
	m_output.bentMeshes.Insert( node->GetMesh().GetPath() );
}

void RoadResourcesCommandlet::Handle( THandle< world::MeshNode > node, Bool root )
{
	if (root)
	{
		m_output.checkerMeshes.Insert( node->GetMesh().GetPath() );
	}
	else
	{
		m_output.meshes.Insert( node->GetMesh().GetPath() );
	}
}

void RoadResourcesCommandlet::Handle( THandle< world::NodesGroup> group )
{
	if(!group->GetProxyMesh().GetPath().IsEmpty()) m_output.proxiex.Insert( group->GetProxyMesh().GetPath());
}

Bool RoadResourcesCommandlet::ParseSettings( const red::CommandLine& options )
{
	String param;
	Bool requiredPresent = false;
	if ( options.GetFirstOption( "prefab", param ) )
	{
		m_settings.startPrefab = res::ResourcePath::Build( param );
		requiredPresent = true;
	}

	if (options.GetFirstOption( "output", param ))
	{
		m_settings.outputPath = red::AbsolutePath::ParseFilePath( param );
	}
	else 		
	{
		m_settings.outputPath = red::AbsolutePath::ParseFilePath( "c:\\road_resources_used.txt");
	}

	return requiredPresent;
}

const char* RoadResourcesCommandlet::GetOneLiner() const
{
	return "List resources used in compiled roads";
}