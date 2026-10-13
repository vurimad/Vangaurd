/**
 * Copyright c 2020 CD Projekt Red. All Rights Reserved.
 */

#include "build.h"
#include "../../backendMesh/include/meshDataView.h"
#include "../../backendMesh/include/meshPhysicsUtils.h"
#include "../../backendEngine/include/resourceUtils.h"
#include "../../backendEngine/include/commandletEngineInstance.h"
#include "../../../common/core/include/commandlet.h"
#include "../../../common/resource/include/resourceEditorDepotImpl.h"
#include "../../../common/resourceMesh/include/mesh.h"
#include "../../../common/redCore/include/commandline.h"
#include "../../../common/redFileSystem/include/fileSys.h"
#include "../../../common/redReflection/include/deferredBufferMapper.h"
#include "../../../common/redSystem/include/stopWatch.h"
#include "../../../common/world/include/worldPrefab.h"
#include "../../../common/worldEntities/include/instancedDestructibleNode.h"
#include "../../../common/worldPhysics/include/dynamicMeshNode.h"
#include "../../../common/world/include/worldPrefabNode.h"

struct MeshData
{
	red::DynArray< res::ResourcePath > m_prefabs{ red::PoolBackend() };
	red::DynArray< res::ResourcePath > m_singleInstanceErrors{ red::PoolBackend() };
	red::DynArray< res::ResourcePath > m_multipleInPrefabErrors{ red::PoolBackend() };
};

struct MeshDataStats
{
	Uint32 m_totalIDNodesProcessed = 0;
	Uint32 m_singleInstanceErrors = 0;
	Uint32 m_duplicatesInPrefabErrors = 0;
	Uint32 m_physicsDecorationResourceErrors = 0;
	Uint32 m_destructibleCount = 0;
};

class InstanceDestructibleMeshListCommandlet : public red::ICommandlet
{
	RTTI_DECLARE_TYPE( InstanceDestructibleMeshListCommandlet );

public:
	InstanceDestructibleMeshListCommandlet();
	virtual ~InstanceDestructibleMeshListCommandlet();

	virtual const char* GetOneLiner() const override final;
	virtual void PrintHelp() const override final;
	virtual Bool Execute( const red::CommandLine& options ) override;

	virtual Bool InitializeEnvironment() override;
	virtual void TeardownEnvironment() override;

private:
	void OutputStats();
	void SaveToOutputCSV();
	Bool ProcessPrefabs();	
	Bool ParseOptions( const red::CommandLine& options );
	Bool ParseResourcePaths( const red::CommandLine& options );

	res::ResourceEditorDepot* m_sourceDepot;
	commandlet::EngineInstance m_engineInstance;

	red::DynArray< res::ResourcePath > m_prefabsToProcessPath{red::PoolBackend()};
	red::HashMap< res::ResourcePath, MeshData > m_existingMeshes{ red::PoolBackend() };
	red::HashMap< res::ResourcePath, red::DynArray< res::ResourcePath > > m_potentiallyIDNMeshes{ red::PoolBackend() };
	red::AbsolutePath m_outputListingFile;
	red::AbsolutePath m_outputPotentialListingFile;
	MeshDataStats m_stats;
};

class DecosetsListCommandlet : public red::ICommandlet
{
	RTTI_DECLARE_TYPE( DecosetsListCommandlet );

public:
	DecosetsListCommandlet();
	virtual ~DecosetsListCommandlet();

	virtual const char* GetOneLiner() const override final;
	virtual void PrintHelp() const override final;
	virtual Bool Execute( const red::CommandLine& options ) override;

	virtual Bool InitializeEnvironment() override;
	virtual void TeardownEnvironment() override;

private:
	void OutputStats();
	void SaveToOutputCSV();

	Bool ProcessPrefabs();
	Bool ParseOptions( const red::CommandLine& options );
	Bool ParseResourcePaths( const red::CommandLine& options );

	res::ResourceEditorDepot* m_sourceDepot;
	commandlet::EngineInstance m_engineInstance;

	red::HashMap< Uint64, Uint32 > m_processedPrefabs{ red::PoolBackend() };
	red::DynArray< res::ResourcePath > m_prefabsToProcessPath{ red::PoolBackend() };
	red::DynArray< res::ResourcePath > m_decosetsWithIDN{ red::PoolBackend() };
	red::DynArray< res::ResourcePath > m_decosetsWithoutIDN{ red::PoolBackend() };
	red::AbsolutePath m_outputListingFile;
	Uint32 m_totalPrefabs = 0;
};


RTTI_BEGIN_TYPE( InstanceDestructibleMeshListCommandlet )
RTTI_PARENT_TYPE( red::ICommandlet )
RTTI_END_TYPE()

RTTI_BEGIN_TYPE( DecosetsListCommandlet )
RTTI_PARENT_TYPE( red::ICommandlet )
RTTI_END_TYPE()


InstanceDestructibleMeshListCommandlet::InstanceDestructibleMeshListCommandlet()
	: red::ICommandlet( RED_NAME_CONSTEXPR( "instanceDestructibleMesh" ) )
{
}

InstanceDestructibleMeshListCommandlet::~InstanceDestructibleMeshListCommandlet()
{}

const char* InstanceDestructibleMeshListCommandlet::GetOneLiner() const
{
	return "Generate list of meshes used by Instanced Destructible Node";	
}

void InstanceDestructibleMeshListCommandlet::PrintHelp() const
{
	/*
	Parameters:

	-inputList=<resource path> 
		Specify file containing list of prefabs to process

	-outputList=<resource path>
		Specify output csv file to save results into

	-outputPotentialList=<resource path>
		Specify output csv file to save results into	
	*/
}

Bool InstanceDestructibleMeshListCommandlet::Execute( const red::CommandLine& options )
{
	if( !ParseOptions( options ) )
		return false;

	if( !ProcessPrefabs( ) )
		return false;

	SaveToOutputCSV();
	OutputStats();

	return true;
}

Bool InstanceDestructibleMeshListCommandlet::InitializeEnvironment()
{
	return m_engineInstance.Initialise();
}

void InstanceDestructibleMeshListCommandlet::TeardownEnvironment()
{
	m_engineInstance.Shutdown();
}

void FillColumn( String& output, red::ArraySpan<const res::ResourcePath> paths )
{
	const String separator = ",";
	Uint32 size = paths.Size();
	for( Uint32 i : paths.Indices() )
	{
		const auto& path = paths[ i ];
		output += path.ToString();
		if( ( i + 1 ) != size )
		{
			output += separator;
		}
	}
}

void InstanceDestructibleMeshListCommandlet::OutputStats()
{
	printf( "InstanceDestructibleMeshListCommandlet finished.\n" );
	printf( "Processed %d Instanced Destructible Nodes (%d of them are actually destructible).\n", m_stats.m_totalIDNodesProcessed, m_stats.m_destructibleCount );
	printf( "Found %d unique meshes in them.\n", m_existingMeshes.Size() );
	printf( "%d nodes had only single instance.\n", m_stats.m_singleInstanceErrors );
	printf( "%d nodes were using PhysicsDecorationResource - %hs\n", m_stats.m_physicsDecorationResourceErrors, 
		m_stats.m_physicsDecorationResourceErrors ? "this should not happen, please investigate!" : "keep it that way." );
	printf( "%d prefabs had multiple Instanced Destructible Nodes using the same mesh.\n", m_stats.m_duplicatesInPrefabErrors );
	printf( "Found %d meshes that could potentially be put into an Instanced Destructible Node.\n", m_potentiallyIDNMeshes.Size() );
}

void InstanceDestructibleMeshListCommandlet::SaveToOutputCSV()
{
	{
		if( !m_outputListingFile.Empty() && !GFileManager->FileExist( m_outputListingFile ) )
		{
			red::SaveStringToFile( m_outputListingFile, "" );
		}

		String output = "Mesh;Prefabs;Single Instance;Duplicateded In Prefab;Perforce History 1;Perforce History 2;Perforce History 3\n";
		const String separator = ";";
		for( const auto& entry : m_existingMeshes )
		{
			output += entry.Key().ToString();
			output += separator;
			auto& meshData = entry.Value();
			FillColumn( output, meshData.m_prefabs );
			output += separator;
			FillColumn( output, meshData.m_singleInstanceErrors );
			output += separator;
			FillColumn( output, meshData.m_multipleInPrefabErrors );
			output += separator;
			output += "\n";
		}
		red::SaveStringToFile( m_outputListingFile, output );
	}
	{
		if( !m_outputPotentialListingFile.Empty() && !GFileManager->FileExist( m_outputPotentialListingFile ) )
		{
			red::SaveStringToFile( m_outputPotentialListingFile, "" );
		}

		String output = "Mesh;Prefabs;Perforce History 1;Perforce History 2;Perforce History 3\n";
		const String separator = ";";
		for( const auto& entry : m_potentiallyIDNMeshes )
		{
			output += entry.Key().ToString();
			output += separator;
			auto& meshData = entry.Value();
			FillColumn( output, meshData );
			output += separator;
			output += "\n";
		}
		red::SaveStringToFile( m_outputPotentialListingFile, output );
	}
}

Bool InstanceDestructibleMeshListCommandlet::ProcessPrefabs()
{
	const Uint32 totalResources = m_prefabsToProcessPath.Size();
	Uint32 currentResource = 0;
	Uint32 failedResource = 0;

	for( const auto& prefabPath : m_prefabsToProcessPath )
	{
		currentResource++;
		RED_LOG( "InstanceDestructibleMeshListCommandlet: Processing resource %hs %u/%u", prefabPath.ToDebugString(), currentResource, totalResources );

		THandle< CResource > resource;

		res::IssueLoadingRequestParameter param{ prefabPath };
		param.skipImports = false;
		param.skipPostLoad = false;
		param.postLoadFlags.skipUploadRenderData = true;

		auto token = GResourceLoader->IssueLoadingRequest( param );
		if( token->HasFailed() )
		{
			RED_LOG_ERROR( "InstanceDestructibleMeshListCommandlet: Could not load %hs for reading", prefabPath.ToDebugString() );
			continue;
		}

		resource = token->WaitUntilLoaded();
		if( !resource || token->HasFailed() )
		{
			RED_LOG_ERROR( "InstanceDestructibleMeshListCommandlet: Failed to load %hs", prefabPath.ToDebugString() );
			return false;
		}

		// Make sure all buffers are read so that the async source isn't needed anymore
		serialization::DeferredDataBufferLoader bufferLoader;
		bufferLoader.Run( resource );

		THandle<world::Prefab> prefab = Cast<world::Prefab>( resource );
		world::Prefab::TNodes nodes{ red::PoolBackend() };
		prefab->ExtractNodes( nodes );
		red::HashMap< res::ResourcePath, Uint32 > existingMeshes{ red::PoolBackend() };
		red::HashMap< res::ResourcePath, Uint32 > potentialMeshes{ red::PoolBackend() };

		existingMeshes.Reserve( nodes.Size() );
		for( auto node : nodes )
		{
			if( THandle<world::InstancedDestructibleMeshNode> idn = Cast< world::InstancedDestructibleMeshNode>( node ) )
			{
				++m_stats.m_totalIDNodesProcessed;
				const res::ResourcePath& meshPath = idn->GetUsedResourcePath();

				auto extension = red::paths::ExtractExtension( meshPath.ToStringView() );
				if( extension == "physicalscene" || extension == "redphysics" )
				{
					++m_stats.m_physicsDecorationResourceErrors;
					continue;
				}

				m_existingMeshes[ meshPath ].m_prefabs.PushBack( prefabPath );
				++existingMeshes[ meshPath ];
				
				if( idn->GetInstanceTransforms().Size() == 1 )
				{
					++m_stats.m_singleInstanceErrors;
					m_existingMeshes[ meshPath ].m_singleInstanceErrors.PushBack( prefabPath );
				}
				if( idn->m_isDestructible )
				{
					++m_stats.m_destructibleCount;
				}
			}
			else if( THandle<world::DynamicMeshNode> dn = Cast< world::DynamicMeshNode>( node ) )
			{
				const res::ResourcePath& meshPath = dn->GetUsedResourcePath();
				++potentialMeshes[ meshPath ];
			}
		}

		for( const auto& dupes : existingMeshes )
		{
			if( dupes.Value() > 1 )
			{
				m_existingMeshes[ dupes.Key() ].m_multipleInPrefabErrors.PushBack( prefabPath );
				++m_stats.m_duplicatesInPrefabErrors;
			}
		}

		for( const auto& entry : potentialMeshes )
		{
			if( entry.Value() > 1 )
			{
				m_potentiallyIDNMeshes[ entry.Key() ].PushBack( prefabPath );
			}
		}
	}
	return true;
}

Bool InstanceDestructibleMeshListCommandlet::ParseOptions( const red::CommandLine& options )
{
	if( !ParseResourcePaths( options ) )
	{
		RED_LOG_ERROR( "InstanceDestructibleMeshListCommandlet: Could not parse prefab list file." );
		return false;
	}

	String str;
	if( !options.GetFirstOption( "outputList", str ) )
	{
		RED_LOG_ERROR( "InstanceDestructibleMeshListCommandlet: Output path is invalid or not present." );
		return false;
	}

	String strPossible;
	if( !options.GetFirstOption( "outputPotentialList", strPossible ) )
	{
		RED_LOG_ERROR( "InstanceDestructibleMeshListCommandlet: Output potential meshes file path is invalid or not present." );
		return false;
	}

	if( !str.EndsWith(".csv") || !strPossible.EndsWith( ".csv" ) )
	{
		RED_LOG_ERROR( "InstanceDestructibleMeshListCommandlet: Output files must be CSV files." );
		return false;
	}
	m_outputListingFile = red::AbsolutePath::ParseFilePath( str );
	m_outputPotentialListingFile = red::AbsolutePath::ParseFilePath( strPossible );

	return true;
}

Bool InstanceDestructibleMeshListCommandlet::ParseResourcePaths( const red::CommandLine& options )
{
	String str;
	if( options.GetFirstOption( "inputList", str ) )
	{
		auto inputListFile = red::AbsolutePath::ParseFilePath( str );
		String fileString = String::EMPTY();
		red::LoadFileToString( inputListFile, fileString );
		red::DynArray<String> fileLines{ red::PoolBackend() };
		fileString.GetTokens( '\n', false, fileLines );
		static const String extension = String( ".prefab" );
		for( String path : fileLines )
		{
			path.RemoveWhiteSpaces();
			if( path.Contains( extension ) )
			{
				m_prefabsToProcessPath.PushBack( res::ResourcePath::Build( path ) );
			}
		}
		return true;
	}

	return false;
}


DecosetsListCommandlet::DecosetsListCommandlet()
	: red::ICommandlet( RED_NAME_CONSTEXPR( "decosetsListCommandlet" ) )
{

}

DecosetsListCommandlet::~DecosetsListCommandlet()
{

}

const char* DecosetsListCommandlet::GetOneLiner() const
{
	return "Generate list of decoset prefabs";
}

void DecosetsListCommandlet::PrintHelp() const
{

}

Bool DecosetsListCommandlet::Execute( const red::CommandLine& options )
{
	if( !ParseOptions( options ) )
		return false;

	if( !ProcessPrefabs() )
		return false;

	SaveToOutputCSV();
	OutputStats();

	return true;
}

Bool DecosetsListCommandlet::InitializeEnvironment()
{
	return m_engineInstance.Initialise();
}

void DecosetsListCommandlet::TeardownEnvironment()
{
	m_engineInstance.Shutdown();
}

void DecosetsListCommandlet::OutputStats()
{
	printf( "DecosetsListCommandlet finished.\n" );
	printf( "Processed %d prefabs.\n", m_totalPrefabs );
	printf( "Found %d decoset prefabs.\n", m_decosetsWithIDN.Size() + m_decosetsWithoutIDN.Size() );
	printf( "%d prefabs have instanced destructible mesh nodes.\n", m_decosetsWithIDN.Size() );
	printf( "%d prefabs do not have instanced destructible mesh nodes.\n", m_decosetsWithoutIDN.Size() );
}

void DecosetsListCommandlet::SaveToOutputCSV()
{
	if( !m_outputListingFile.Empty() && !GFileManager->FileExist( m_outputListingFile ) )
	{
		red::SaveStringToFile( m_outputListingFile, "" );
	}

	String output = "Decoset with IDN;Decosets without IDN \n";
	const String separator = ";";
	Uint32 size = std::max( m_decosetsWithIDN.Size(), m_decosetsWithoutIDN.Size() );
	for( Uint32 i = 0; i < size; ++i )
	{
		res::ResourcePath withPath = res::ResourcePath::EMPTY;
		res::ResourcePath withoutPath = res::ResourcePath::EMPTY;

		if( m_decosetsWithIDN.Size() > i )
		{
			withPath = m_decosetsWithIDN[ i ];
		}

		if( m_decosetsWithoutIDN.Size() > i )
		{
			withoutPath = m_decosetsWithoutIDN[ i ];
		}

		output += withPath.ToString();
		output += separator;
		output += withoutPath.ToString();
		output += separator;
		output += "\n";
	}
	red::SaveStringToFile( m_outputListingFile, output );
}

Bool DecosetsListCommandlet::ProcessPrefabs()
{
	while( !m_prefabsToProcessPath.Empty() )
	{
		auto prefabPath = m_prefabsToProcessPath.PopBack();
		
		++m_totalPrefabs;
		RED_LOG( "DecosetsListCommandlet: Processing resource %hs", prefabPath.ToDebugString() );

		THandle< CResource > resource;
		res::IssueLoadingRequestParameter param{ prefabPath };
		param.skipImports = false;
		param.skipPostLoad = false;
		param.postLoadFlags.skipUploadRenderData = true;

		auto token = GResourceLoader->IssueLoadingRequest( param );
		if( token->HasFailed() )
		{
			RED_LOG_ERROR( "DecosetsListCommandlet: Could not load %hs for reading", prefabPath.ToDebugString() );
			continue;
		}

		resource = token->WaitUntilLoaded();
		if( !resource || token->HasFailed() )
		{
			RED_LOG_ERROR( "DecosetsListCommandlet: Failed to load %hs", prefabPath.ToDebugString() );
			return false;
		}

		// Make sure all buffers are read so that the async source isn't needed anymore
		serialization::DeferredDataBufferLoader bufferLoader;
		bufferLoader.Run( resource );

		THandle<world::Prefab> prefab = Cast<world::Prefab>( resource );
		world::Prefab::TNodes nodes{ red::PoolBackend() };
		prefab->ExtractNodes( nodes );
		Bool hasIDN = false;
		for( auto node : nodes )
		{
			if( THandle<world::InstancedDestructibleMeshNode> idn = Cast< world::InstancedDestructibleMeshNode>( node ) )
			{
				hasIDN = true;
			}
			else if( THandle<world::PrefabNode> prefabNode = Cast< world::PrefabNode>( node ) )
			{
				const res::ResourcePath& resPath = prefabNode->GetUsedResourcePath();
				
				if( !m_processedPrefabs.KeyExist( resPath.GetHash() ) )
				{
					m_prefabsToProcessPath.PushBack( resPath );
				}
				++m_processedPrefabs[ resPath.GetHash() ];
			}
		}
		if( prefabPath.ToString().ContainsNoCase( "decoset", 0 ) )
		{
			if( hasIDN )
			{
				m_decosetsWithIDN.PushBack( prefabPath );
			}
			else
			{
				m_decosetsWithoutIDN.PushBack( prefabPath );
			}
		}
	}
	return true;
}

Bool DecosetsListCommandlet::ParseOptions( const red::CommandLine& options )
{
	if( !ParseResourcePaths( options ) )
	{
		RED_LOG_ERROR( "DecosetsListCommandlet: Could not parse prefab list file." );
		return false;
	}

	String str;
	if( !options.GetFirstOption( "outputList", str ) )
	{
		RED_LOG_ERROR( "DecosetsListCommandlet: Output path is invalid or not present." );
		return false;
	}

	if( !str.EndsWith( ".csv" ) )
	{
		RED_LOG_ERROR( "DecosetsListCommandlet: Output files must be CSV files." );
		return false;
	}
	m_outputListingFile = red::AbsolutePath::ParseFilePath( str );

	return true;
}

Bool DecosetsListCommandlet::ParseResourcePaths( const red::CommandLine& options )
{
	String str;
	if( options.GetFirstOption( "inputFile", str ) )
	{
		if( !str.EndsWith( ".prefab" ) )
		{
			RED_LOG_ERROR( "DecosetsListCommandlet: Input file must be a prefab." );
			return false;
		}
		auto inputListFile = res::ResourcePath::Build( str );

		m_prefabsToProcessPath.PushBack( inputListFile );
		return true;
	}

	return false;
}


