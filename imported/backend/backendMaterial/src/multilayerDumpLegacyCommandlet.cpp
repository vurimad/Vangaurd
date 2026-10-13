/**
* Copyright (c) 2018 CD Projekt Red. All Rights Reserved.
*/
#include "build.h"
#include "../../../common/core/include/commandlet.h"
#include "../../../common/redCore/include/commandline.h"
#include "../../../common/redReflection/include/resource.h"
#include "../../../common/redReflection/include/resourceLoader.h"
#include "../../../common/redReflection/include/serializationLoader.h"
#include "../../../common/redReflection/include/serializationFileTables.h"
#include "../../../common/redReflection/include/serializationBinaryRuntimeTables.h"
#include "../../../common/redFileSystem/include/fileSys.h"
#include "../../../common/resource/include/basicTextBuilder.h"
#include "../../../common/resource/include/resourceEditorDepotImpl.h"
#include "../../../common/resourceMesh/include/mesh.h"
#include "../../../common/resourceMesh/include/meshAppearance.h"
#include "../../backendEngine/include/commandletUtils.h"
#include "../../../common/redJobs2/include/jobBuilder.h"


struct FoundMesh
{
	res::ResourcePath meshPath;
	red::DynArray<CName> appearances;
};

static Bool operator <( const FoundMesh& a, const FoundMesh& b )
{
	return a.meshPath.ToStringView() < b.meshPath.ToStringView();
}


class MultilayerDumpLegacyCommandlet : public red::ICommandlet
{
	RTTI_DECLARE_TYPE(MultilayerDumpLegacyCommandlet);

public:
	MultilayerDumpLegacyCommandlet();
	~MultilayerDumpLegacyCommandlet();

	virtual Bool Execute(const red::CommandLine& options) override;
	virtual const char* GetOneLiner() const override;
	virtual void PrintHelp() const override;
};

RTTI_BEGIN_TYPE(MultilayerDumpLegacyCommandlet);
RTTI_PARENT_TYPE(ICommandlet);
RTTI_END_TYPE();


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


MultilayerDumpLegacyCommandlet::MultilayerDumpLegacyCommandlet()
	: ICommandlet( RED_NAME_CONSTEXPR("multilayerDumpLegacy") )
{
}

MultilayerDumpLegacyCommandlet::~MultilayerDumpLegacyCommandlet()
{
}

const char* MultilayerDumpLegacyCommandlet::GetOneLiner() const
{
	return "Dump a list of all mesh resources that are using the old multilayer materials.";
}

void MultilayerDumpLegacyCommandlet::PrintHelp() const
{
	RED_LOG("Usage:");
	RED_LOG("  multilayerDumpLegacy [-path=<inputPath>] [-all] -output=<outFile>");
	RED_LOG("");
	RED_LOG("Parameters:");
	RED_LOG("  -path=<inputPath>        - A resource path to a folder in the file system (e.g. engine\\meshes\\editor) to scan");
	RED_LOG("  -all                     - Scan the entire depot");
	RED_LOG("  -output=<outFile>        - Absolute path where resulting list should be saved");
	RED_LOG("");
	RED_LOG("Description:");
	RED_LOG("  Finds all meshes in the given depot path using the old multilayer materials, and writes the list to the");
	RED_LOG("  specified output file. One of 'path' or 'all' must be provided.");
}


Bool MultilayerDumpLegacyCommandlet::Execute(const red::CommandLine& options)
{
	String inputPath;

	if ( !options.HasOption("all") )
	{
		if ( !options.GetFirstParam("path", inputPath) )
		{
			RED_LOG_ERROR("Missing input path");
			RED_LOG("");
			PrintHelp();
			return false;
		}
	}

	red::AbsolutePath outputFilePath;
	{
		String outputFile;
		if ( options.GetFirstParam("output", outputFile) )
		{
			outputFilePath = red::AbsolutePath::ParseFilePath( outputFile );
		}
		else
		{
			RED_LOG_ERROR("Missing output file");
			RED_LOG("");
			PrintHelp();
			return false;
		}
	}

	auto* depot = commandlet::GetEditorDepot();

	const red::DynArray< String > patterns{ "*.mesh", "*.w2mesh" };
	red::DynArray< res::ResourcePath > allMeshResources = commandlet::ScanResourceDepotDirectories( *depot, inputPath, patterns );

	RED_LOG("Found %u mesh resources", allMeshResources.Size());


	red::Mutex foundMeshesMutex;
	red::DynArray<FoundMesh> foundMeshes;
	foundMeshes.Reserve(allMeshResources.Size());

	job::Builder builder;
	{
		for ( const res::ResourcePath& meshPath : allMeshResources )
		{
			res::LoadingOptions options;
			options.skipPostLoad = true;
			res::ResourceTokenHandle token = GResourceLoader->IssueLoadingRequest( Convert( meshPath, options ) );

			builder.DispatchJobAfterWait_NoFence(
				token->GetWaitCounter(), "CheckMaterials",
			[&foundMeshesMutex, &foundMeshes, token, meshPath]( const job::RunContext& )
			{
				if ( !token->HasFailed() )
				{
					THandle<CMesh> mesh = Cast<CMesh>(token->GetResource());
					if ( mesh )
					{
						FoundMesh foundMesh;

						red::ArraySpan<const THandle<mesh::MeshAppearance>> appearances = mesh->GetAllAppearances();
						for ( const THandle<mesh::MeshAppearance>& appearance : appearances )
						{
							const Uint32 numChunks = appearance->GetNumChunks();
							Bool foundMLChunk = false;

							for ( Uint32 i = 0; i < numChunks; ++i )
							{
								const Uint32 numMaterials = appearance->GetChunkMaterialsCount( i );
								if ( numMaterials > 1 )
								{
									foundMLChunk = true;
									break;
								}
							}

							if ( foundMLChunk )
							{
								foundMesh.appearances.PushBack( appearance->GetName() );
							}
						}

						if ( !foundMesh.appearances.Empty() )
						{
							foundMesh.meshPath = meshPath;

							{
								red::ScopedLock<red::Mutex> lock( foundMeshesMutex );
								foundMeshes.PushBack( std::move(foundMesh) );
							}
						}
					}
				}
			} );
		}
		builder.DispatchFenceExplicitly();
	}

	job::FlushCounter( builder.ExtractWaitCounter() );

	RED_LOG("Found %u mesh resources with legacy multilayer", foundMeshes.Size());

	std::sort( foundMeshes.Begin(), foundMeshes.End() );

	CBasicTextBuilder textBuilder;
	for ( const FoundMesh& mesh : foundMeshes )
	{
		red::StringView meshPath = mesh.meshPath.ToStringView();
		textBuilder.Write( meshPath.Data(), meshPath.Length() );
		textBuilder.Write( "\n" );
	}
	textBuilder.SaveToFile( outputFilePath, false );


	return true;
}
