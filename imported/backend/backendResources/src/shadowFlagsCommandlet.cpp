/*
* Copyright (c) 2018 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"

#include "../../../common/core/include/commandlet.h"
#include "../../../common/redCore/include/commandline.h"
#include "../../../common/resource/include/resourceEditorDepotImpl.h"
#include "../../../common/redReflection/include/resourceLoader.h"
#include "../../../common/resource/include/resourceSaver.h"
#include "../../../common/redFileSystem/include/fileSys.h"

#include "../../backendEngine/include/commandletUtils.h"
#include "../../backendEngine/include/commandletEngineInstance.h"

#include "../../backendData/include/dependencyScanner.h"

#include "../../../common/resourceMesh/include/mesh.h"
#include "../../../common/world/include/worldPrefab.h"
#include "../../../common/world/include/meshNode.h"
#include "../../../common/world/include/worldNode.h"
#include "../../../common/resource/include/basicTextBuilder.h"

class ShadowFlagsCommandlet : public red::ICommandlet
{
	RTTI_DECLARE_TYPE( ShadowFlagsCommandlet );

public:
	ShadowFlagsCommandlet();
	~ShadowFlagsCommandlet();

	virtual Bool Execute( const red::CommandLine& options ) override;
	virtual const char* GetOneLiner() const override;
	virtual void PrintHelp() const override;

	virtual Bool InitializeEnvironment() override;
	virtual void TeardownEnvironment() override;

private:
	struct CommandletSettings;
	struct ShadowFlags;
	struct CommandletStats;
	struct PrefabDescriptor;

	using ResourcePathArray = red::DynArray< res::ResourcePath >;
	using ResourcePathSet = red::HashSet< res::ResourcePath >;
	using PrefabsToMeshesMap = red::Map< res::ResourcePath, red::DynArray< res::ResourcePath > >;
	using MissingMeshesMap = red::Map< res::ResourcePath, red::Map < res::ResourcePath, ResourcePathArray > >;
	using MeshesToShadowFlagsMap = red::Map< res::ResourcePath, ShadowFlags >;
	using NodePtrArray = red::DynArray< world::NodePtr >;

	bool ParseSettings( const red::CommandLine& options, CommandletSettings& settings );
	void ProcessResourceDependencies( const res::ResourcePath& currentPrefabPath, const ResourcePathArray& dependencies, PrefabDescriptor& prefabDescriptor, ResourcePathArray& pending );
	void CollectShadowFlagsForEachUniqueMesh( const res::LoadingOptions& loadingOptions, PrefabDescriptor& prefabDescriptor, MeshesToShadowFlagsMap& meshesShadowFlags );
	void CollectPrefabsShadowsFlagsAndMatchWithRawMeshesResources( const PrefabsToMeshesMap& prefabsToMeshesMap, const res::LoadingOptions& resourcesLoadingOptions, const MeshesToShadowFlagsMap& meshesShadowFlags, CommandletStats& commandletStats );
	Bool DumpStatsToFile( const CommandletSettings& settings, const CommandletStats& commandletStats, const PrefabDescriptor& prefabDescriptor );
	Bool ResavePrefabsWithShadowsFlagsOn( const PrefabDescriptor& prefabDescriptor, const res::ResourcePath& resavePath = res::ResourcePath() );

	commandlet::EngineInstance m_engineInstance;
};

RTTI_BEGIN_TYPE( ShadowFlagsCommandlet )
RTTI_PARENT_TYPE( ICommandlet )
RTTI_END_TYPE();

//------------------------------------------------------------------------------

ShadowFlagsCommandlet::ShadowFlagsCommandlet()
	: ICommandlet( RED_NAME_CONSTEXPR( "shadow-flags" ) )
{
}

ShadowFlagsCommandlet::~ShadowFlagsCommandlet()
{
}

const char* ShadowFlagsCommandlet::GetOneLiner() const
{
	return "Prefab's shadows flag info.";
}

void ShadowFlagsCommandlet::PrintHelp() const
{
	RED_LOG( "WCC: Usage:" );
	RED_LOG( "WCC:   shadow-flags -file=<path> -output=<file>" );
	RED_LOG( "WCC: Parameters:" );
	RED_LOG( "WCC:   -file=<path>                 - path of a prefab/world to be analyzed." );
	RED_LOG( "WCC:   -output=<file>               - place to store Output statistics" );
	RED_LOG( "WCC:   -resave OR -resave=<file>    - Set all shadows flags to true and either resave the file or save it to a specified path" );
}

Bool ShadowFlagsCommandlet::InitializeEnvironment()
{
	return m_engineInstance.Initialise();
}

void ShadowFlagsCommandlet::TeardownEnvironment()
{
	m_engineInstance.Shutdown();
}

res::LoadingOptions GetResourceLoadingOptions()
{
	res::LoadingOptions resourcesLoadingOptions;
	resourcesLoadingOptions.skipPostLoad = true;

	return resourcesLoadingOptions;
}

//------------------------------------------------------------------------------

struct ShadowFlagsCommandlet::CommandletSettings
{
	res::ResourcePath inputFile;
	red::AbsolutePath outputStatsFile;
	Bool resaveWithShadowFlagsOn;
	res::ResourcePath resaveAlternativePath;

	CommandletSettings()
		: resaveWithShadowFlagsOn( false )
	{}
};

struct ShadowFlagsCommandlet::ShadowFlags
{
	Bool shadows;
	Bool localShadows;

	ShadowFlags()
		: shadows( false )
		, localShadows( false )
	{}
};

struct ShadowFlagsCommandlet::CommandletStats
{
	Uint32 prefabMeshNotMatchingCastShadowsFlag;
	Uint32 prefabMeshNotMatchingCastLocalShadowsFlag;

	CommandletStats()
		: prefabMeshNotMatchingCastShadowsFlag( 0 )
		, prefabMeshNotMatchingCastLocalShadowsFlag( 0 )
	{}

	void UpdateStats( const world::MeshNode& meshNode, const ShadowFlags& comparisionShadowFlags )
	{
		const Bool castShadowsFlagMismatch      = ( meshNode.IsCastingShadows()      != comparisionShadowFlags.shadows );
		const Bool castLocalShadowsFlagMismatch = ( meshNode.IsCastingLocalShadows() != comparisionShadowFlags.localShadows );

		if ( castShadowsFlagMismatch )
		{
			++( this->prefabMeshNotMatchingCastShadowsFlag );
		}

		if ( castLocalShadowsFlagMismatch )
		{
			++( this->prefabMeshNotMatchingCastLocalShadowsFlag );
		}
	}
};

struct ShadowFlagsCommandlet::PrefabDescriptor
{
	ResourcePathSet    uniqueMeshes;
	ResourcePathSet    uniquePrefabs;
	PrefabsToMeshesMap prefabsToMeshesMap;
	MissingMeshesMap   missingMeshesMap;

	void RecordMissingMesh( const res::ResourcePath& missingMeshResourcePath )
	{
		for ( const auto& prefabToMeshes : prefabsToMeshesMap )
		{
			const auto& prefabResourcePath = prefabToMeshes.Key();
			const auto& prefabMeshes = prefabToMeshes.Value();

			for ( const auto& mesh : prefabMeshes )
			{

				if ( mesh == missingMeshResourcePath )
				{
					// At this point we know that particular prefab has mesh that has been reported as missing.
					// We need to load the prefab to memory and record all nodes that uses the mesh.
					UpdateMissingMeshesMap( prefabResourcePath, missingMeshResourcePath );
				}
			}
		}
	}

private:

	void UpdateMissingMeshesMap( const res::ResourcePath& prefabResourcePath, const res::ResourcePath& missingMeshResourcePath)
	{
		const auto prefabResource = Cast< world::Prefab >( GResourceLoader->IssueLoadingRequest( Convert( prefabResourcePath, GetResourceLoadingOptions() ) )->WaitUntilLoaded() );
		const auto& prefabNodes = prefabResource->GetRootGroup()->GetNodes();

		for (const auto& prefabNode : prefabNodes)
		{
			const auto meshNode = Cast< world::MeshNode >( prefabNode );
			if ( meshNode )
			{
				const res::ResourcePath meshResourcePath = meshNode->GetMesh().GetPath();
				if ( red::paths::GetExtension( meshResourcePath.ToStringView() ) == "mesh" && meshResourcePath == missingMeshResourcePath )
				{
					missingMeshesMap[ missingMeshResourcePath ][ prefabResourcePath ].PushBack( res::ResourcePath::Build( prefabNode->GetName().AsStringView() ) );
				}
			}
		}
	}

};

//------------------------------------------------------------------------------

Bool ShadowFlagsCommandlet::ResavePrefabsWithShadowsFlagsOn( const PrefabDescriptor& prefabDescriptor, const res::ResourcePath& resavePath )
{
	// For each prefab that has at least one mesh node, we swap all of its shadow flags to true.
	// Then, we save prefab to either a specified path or we override it.
	for ( const auto& prefabWithMeshes : prefabDescriptor.prefabsToMeshesMap )
	{
		const auto& prefabWithMeshPath = prefabWithMeshes.Key();
		const auto& prefabMeshes = prefabWithMeshes.Value();

		const auto rawPrefabResource = GResourceLoader->IssueLoadingRequest( Convert( prefabWithMeshPath, GetResourceLoadingOptions() ) )->WaitUntilLoaded();
		const auto prefabResource = Cast< world::Prefab >( rawPrefabResource );

		NodePtrArray prefabNodes;
		prefabResource->ExtractNodes( prefabNodes );

		for ( const auto& prefabNode : prefabNodes )
		{
			auto meshNode = Cast< world::MeshNode >( prefabNode );
			meshNode->SetCastShadows(true);
			meshNode->SetCastLocalShadows(true);
		}

		res::ResourceSavingOptions resourceSavingOptions;
		if ( !resavePath.IsEmpty() )
		{
			resourceSavingOptions.m_targetPath = resavePath;
		}
		if ( !res::SaveResource( prefabResource ) )
		{
			RED_LOG_ERROR( "Cannot save file." );
			return false;
		}
	}

	return true;
}

Bool ShadowFlagsCommandlet::DumpStatsToFile( const CommandletSettings& commandletSettings, const CommandletStats& commandletStats, const PrefabDescriptor& prefabDescriptor )
{
	// Header with summary.
	CBasicTextBuilder textBuilder;
	textBuilder.Write( "====================================================================\r\n" );
	textBuilder.Writef( "Root Prefab: %s\r\n", commandletSettings.inputFile.ToString().AsChar());
	textBuilder.Write( "====================================================================\r\n" );
	textBuilder.Writef( "Unique meshes processed: %d\r\n", prefabDescriptor.uniqueMeshes.Size() );
	textBuilder.Writef("Meshes missing: %d\r\n", prefabDescriptor.missingMeshesMap.Size() );
	textBuilder.Writef("Prefab meshes not matching Cast Shadows flag: %d\r\n", commandletStats.prefabMeshNotMatchingCastShadowsFlag);
	textBuilder.Writef("Prefab meshes not matching Cast Local Shadows flag: %d\r\n", commandletStats.prefabMeshNotMatchingCastLocalShadowsFlag);

	// List of missing meshes if needed.
	if (prefabDescriptor.missingMeshesMap.Capacity() != 0)
	{
		textBuilder.Write( "====================================================================\r\n" );
		textBuilder.Write( "Missing Meshes Summary\r\n");
		textBuilder.Write( "====================================================================\r\n" );
		for (const auto& missingMesh : prefabDescriptor.missingMeshesMap)
		{
			textBuilder.Writef( "[Mesh]: %s\r\n", missingMesh.Key().ToString().AsChar() );
			for (const auto& prefab : missingMesh.Value())
			{
				textBuilder.Writef( "---[Prefab]:  %s\r\n", prefab.Key().ToString().AsChar() );
				for (const auto& node : prefab.Value())
				{
					textBuilder.Writef( "------[Node]:  %s\r\n", node.ToString().AsChar() );
				}
			}
		}
	}

	{
		auto filePtr = GFileManager->CreateFileWriter( commandletSettings.outputStatsFile, FOF_AbsolutePath );
		if (!filePtr)
		{
			RED_LOG_ERROR( "Failed to open dump report output file: %hs", commandletSettings.outputStatsFile.AsChar() );
			return false;
		}
		textBuilder.SaveToFile(filePtr.Get());
	}
	
	return true;
}

void ShadowFlagsCommandlet::CollectPrefabsShadowsFlagsAndMatchWithRawMeshesResources( const PrefabsToMeshesMap& prefabsToMeshesMap, const res::LoadingOptions& resourcesLoadingOptions, const MeshesToShadowFlagsMap& meshesShadowFlags, CommandletStats& commandletStats )
{
	for (const auto& prefabWithMeshes : prefabsToMeshesMap)
	{
		const auto& prefabWithMeshPath = prefabWithMeshes.Key();
		const auto& prefabMeshes = prefabWithMeshes.Value();

		const auto prefabResource = Cast< world::Prefab >( GResourceLoader->IssueLoadingRequest( Convert( prefabWithMeshPath, resourcesLoadingOptions ) )->WaitUntilLoaded() );
		NodePtrArray prefabNodes;
		prefabResource->ExtractNodes( prefabNodes );

		for ( const auto& prefabNode : prefabNodes )
		{
			const auto meshNode = Cast< world::MeshNode >( prefabNode );
			if ( meshNode )
			{
				// Beside the extension matching, meshResourcePath must be in meshes shadow flags mapping. 
				// It may happen that during processing of the meshes some are missing from the disk, 
				// then meshShadowFlags will not contain it, while it still exists in basic prefabs-to-meshes mapping.
				const res::ResourcePath meshResourcePath = meshNode->GetMesh().GetPath();
				const red::StringView meshResourceExt = red::paths::GetExtension( meshResourcePath.ToStringView() );
				const Bool meshResourcePathMatchesMeshExt = ( meshResourceExt == CMesh::GetFileExtension() || meshResourceExt == CMesh::GetDeprecatedFileExtension() );
				if ( meshResourcePathMatchesMeshExt && meshesShadowFlags.KeyExist( meshResourcePath ) )
				{
					commandletStats.UpdateStats( *meshNode, meshesShadowFlags[ meshResourcePath ] );
				}
			}
		}
	}
}

void ShadowFlagsCommandlet::CollectShadowFlagsForEachUniqueMesh( const res::LoadingOptions& loadingOptions, PrefabDescriptor& prefabDescriptor, MeshesToShadowFlagsMap& meshesShadowFlags )
{
	for ( const auto& meshResourcePath : prefabDescriptor.uniqueMeshes )
	{
		const auto meshResource = Cast< CMesh >( GResourceLoader->IssueLoadingRequest( Convert( meshResourcePath, loadingOptions ) )->WaitUntilLoaded() );

		if ( !meshResource )
		{	
			RED_LOG_WARNING("Resource mesh does not exists. Skipping.");
			prefabDescriptor.RecordMissingMesh( meshResourcePath );
			continue;
		}

		ShadowFlags shadowFlags;
		shadowFlags.shadows = meshResource->GetCastGlobalShadows();
		shadowFlags.localShadows = meshResource->GetCastLocalShadows();

		meshesShadowFlags[ meshResourcePath ] = shadowFlags;
	}
}

void ShadowFlagsCommandlet::ProcessResourceDependencies( const res::ResourcePath& currentPrefabPath, const ResourcePathArray& dependencies, PrefabDescriptor& prefabDescriptor, ResourcePathArray& pending )
{
	for ( const res::ResourcePath& dependencyPath : dependencies )
	{
		const red::StringView ext = red::paths::GetExtension( dependencyPath.ToStringView() );

		// Although mesh resource is not treated as a dependency that need to be recursively checked, 
		// we keep it for further analysis.
		if ( ext == CMesh::GetFileExtension() || ext == CMesh::GetDeprecatedFileExtension())
		{
			prefabDescriptor.prefabsToMeshesMap[ currentPrefabPath ].PushBack( dependencyPath );
			prefabDescriptor.uniqueMeshes.Insert( dependencyPath );
		}
		// We save prefabs that were not '_generated' for the recursive pass.
		else
		{
			const red::StringView filename = red::paths::GetFileStem( dependencyPath.ToStringView() );
			const Bool extensionMatchesPrefabFile = ( ext == world::Prefab::GetFileExtension() || ext == world::Prefab::GetDeprecatedFileExtension() );
			if ( extensionMatchesPrefabFile && filename != "_generated" )
			{
				if ( prefabDescriptor.uniquePrefabs.Insert( dependencyPath ).IsSuccessful() )
				{
					pending.PushBack( dependencyPath );
				}
			}
		}
	}
}

bool ShadowFlagsCommandlet::ParseSettings( const red::CommandLine& options, CommandletSettings& commandletSettings )
{
	String filePath;
	if ( !options.GetFirstOption( "file", filePath ) )
	{
		RED_LOG_ERROR( "Input prefab/world not specified." );
		return false;
	}

	String outputStatsFile;
	if ( !options.GetFirstOption( "output", outputStatsFile ) )
	{
		RED_LOG_ERROR( "Output file not specified." );
		return false;
	}

	String resaveOutput;
	options.GetFirstOption( "resave", resaveOutput );
	
	commandletSettings.inputFile = res::ResourcePath::Build( filePath );
	commandletSettings.outputStatsFile = red::AbsolutePath::ParseFilePath( outputStatsFile );
	commandletSettings.resaveWithShadowFlagsOn = options.HasOption( "resave" );
	commandletSettings.resaveAlternativePath = res::ResourcePath::Build( resaveOutput );

	return true;
}

//------------------------------------------------------------------------------

Bool ShadowFlagsCommandlet::Execute( const red::CommandLine& options )
{
	CommandletSettings commandletSettings;
	if ( !ParseSettings( options, commandletSettings ) )
	{
		return false;
	}

	ResourcePathArray dependencies;
	ResourcePathArray pending;
	PrefabDescriptor  prefabDescriptor;

	const auto depot = commandlet::GetEditorDepot();

	// Recursively traverse the input prefab tree and fill in respective data structures.
	// We save mappings from each prefab to an array of  its meshes.
	// Additionally, list of all unique meshes that we stumbled upon is kept.
	pending.PushBack( commandletSettings.inputFile );
	while ( !pending.Empty() )
	{
		dependencies.Clear();

		const auto currentPath = pending.PopBack();
		dependency::ScanResource( depot, currentPath, dependencies );

		ProcessResourceDependencies( currentPath, dependencies, prefabDescriptor, pending );
	}
	
	CommandletStats commandletStats;
	
	// Collect shadow flags information for each mesh.
	MeshesToShadowFlagsMap meshesShadowFlags;
	CollectShadowFlagsForEachUniqueMesh( GetResourceLoadingOptions(), prefabDescriptor, meshesShadowFlags );
	
	// Collect shadow flags information for each prefab (including sub-prefabs) that contains at least one mesh.
	// Prefab's nodes will be checked and matched with already collected shadow flags settings for raw meshes resources.
	CollectPrefabsShadowsFlagsAndMatchWithRawMeshesResources( prefabDescriptor.prefabsToMeshesMap, GetResourceLoadingOptions(), meshesShadowFlags, commandletStats );

	if ( !DumpStatsToFile( commandletSettings, commandletStats, prefabDescriptor ) )
	{
		RED_LOG_ERROR( "Dumping data failed." );
		return false;
	}

	if ( commandletSettings.resaveWithShadowFlagsOn )
	{
		if ( !ResavePrefabsWithShadowsFlagsOn(prefabDescriptor, commandletSettings.resaveAlternativePath) )
		{
			RED_LOG_ERROR( " Turning on all shadows flags failed." );
			return false;
		}
	}

	return true;
}
