/*
* Copyright (c) 2014 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"

#include "../../../common/core/include/commandlet.h"
#include "../../../common/redCore/include/commandline.h"
#include "../../../common/resource/include/resourceEditorDepotImpl.h"
#include "../../../common/resource/include/resourceDepotUtils.h"
#include "../../../common/redReflection/include/resourceLoader.h"
#include "../../../common/redFileSystem/include/fileSys.h"

#include "../../backendEngine/include/commandletUtils.h"
#include "../../backendEngine/include/commandletEngineInstance.h"
#include "../../backendEngine/include/commandletRenderInstance.h"

#include "../../../common/resourceMesh/include/mesh.h"
#include "../../backendMesh/include/meshDataView.h"
#include "../../backendMesh/include/meshRenderBlobUtils.h"
#include "../../../common/resource/include/resourceSaver.h"
#include "../../../common/redReflection/include/util.h"
#include "../../../common/redReflection/include/resourceLoaderTypes.h"

class RemoveUnusedBonesCommandlet : public red::ICommandlet
{
	RTTI_DECLARE_TYPE( RemoveUnusedBonesCommandlet );

public:
	RemoveUnusedBonesCommandlet();
	~RemoveUnusedBonesCommandlet() = default;

	// interface
	virtual Bool Execute( const red::CommandLine& options ) override;
	virtual const char* GetOneLiner() const override;
	virtual void PrintHelp() const override {}

	virtual Bool InitializeEnvironment() override;
	virtual void TeardownEnvironment() override;

private:
	commandlet::EngineInstance m_engineInstance;
	commandlet::RenderInstance m_renderInstance;
};

RTTI_BEGIN_TYPE( RemoveUnusedBonesCommandlet )
RTTI_PARENT_TYPE( ICommandlet )
RTTI_END_TYPE();

RemoveUnusedBonesCommandlet::RemoveUnusedBonesCommandlet()
	: ICommandlet( RED_NAME_CONSTEXPR( "removeUnusedBones" ) )
{
}

const char* RemoveUnusedBonesCommandlet::GetOneLiner() const
{
	return "Remove Unused Bones";
}

Bool RemoveUnusedBonesCommandlet::InitializeEnvironment()
{
	const Bool engineInit = m_engineInstance.Initialise();
	const Bool renderInit = m_renderInstance.Initialise();
	return engineInit && renderInit;
}

void RemoveUnusedBonesCommandlet::TeardownEnvironment()
{
	m_renderInstance.Shutdown();
	m_engineInstance.Shutdown();
}

Bool RemoveUnusedBonesCommandlet::Execute( const red::CommandLine& options )
{
	res::ResourceEditorDepot* sourceDepot = commandlet::GetEditorDepot();
	red::DynArray< res::ResourcePath > resources = commandlet::ParseResourcePathsFromCommandLine( *sourceDepot, options );
	if( resources.Empty() )
	{
		return false;
	}

	auto endIt = std::remove_if( resources.Begin(), resources.End(), []( const res::ResourcePath& path )
	{
		const red::StringView pathView = path.ToStringView();
		if( pathView.EndsWith( ".mesh" ) || pathView.EndsWith( ".w2mesh" ) )
			return false;

		return true;
	});

	resources.Remove( endIt, resources.End() );

	//
	// Run it!
	//

	bool result = true;
	Uint32 index = 0;
	const Uint32 total = resources.Size();
	for( const res::ResourcePath& path : resources )
	{
		Bool resourceStatus = false;
		Bool blobStatus = false;
		Bool saveStatus = false;
		res::ResourceTokenHandle token = GResourceLoader->IssueLoadingRequest( path );
		THandle<CMesh> mesh = Cast<CMesh>( token->WaitUntilLoaded() );
		if ( mesh )
		{
			mesh::DataView meshView( *mesh.Get() );
			if ( resourceStatus = meshView.RemoveUnusedBones() )
			{
				blobStatus = tools::CreateMeshRenderResourceBlob( *mesh.Get(), PLATFORM_PC, false );
			}

			if ( resourceStatus && blobStatus )
			{
				res::ResourceSavingOptions savingOptions;
				savingOptions.m_depot = sourceDepot;
				savingOptions.m_targetPath = path;
				saveStatus = res::SaveResource( mesh, savingOptions );
			}
		}

		RED_LOG_INFO( "%u/%u: %hs (%s|%s|%s|%s)", index, total, path.ToDebugString(),
			mesh ?
			"MESH LOAD OK" :
			"MESH FAILED TO LOAD",

			resourceStatus ? 
			"REMOVED" : 
			"NOTHING TO REMOVE",
			
			blobStatus ? 
			"BLOB UPDATED" : 
			"BLOB FAIL",
			
			saveStatus ? 
			"SAVED" : 
			"CAN'T SAVE (READ-ONLY?)" );

		++index;
	}

	return result;
}


