#include "build.h"
#include "importMeshREX_Materials.h"

#include "../../../common/resourceMesh/include/mesh.h"
#include "../../../common/resourceMesh/include/meshAppearance.h"
#include "../../../common/redReflection/include/gatheredResource.h"
#include "../../../common/resource/include/resourceEditorDepotImpl.h"

#include "../../../backend/backendMesh/include/meshDataView.h"
#include "../../../backend/backendREA/include/reFileHelpers.h"
#include "../../../backend/backendREA/include/reMaterialHelpers.h"


using red::DynArray;
using red::String;


res::TStaticResource< IMaterial > resMaterialDefault( RED_CONST_RESOURCEPATH( "engine\\materials\\metal_base.remt" ), GRF_Backend | GRF_Startup );


static void GatherMaterialsDirectories( red::DynArray< String >& outDirs )
{
	outDirs.PushBack( "engine\\materials\\" );
	outDirs.PushBack( "base\\materials\\" );

	for( Uint32 i = 0; i < outDirs.Size(); ++i )
	{
		const String outDir = outDirs[ i ];

		red::DynArray< String > subDirs;
		res::GetEditorDepot()->FindDepotSubDirectories( outDir, subDirs );

		for( const auto& subDir : subDirs )
		{
			outDirs.PushBack( outDir + subDir + "\\" );
		}
	}
}


static THandle< IMaterial > FindMaterial( const red::DynArray< String >& searchDirs, const String& name )
{
	for( const auto& dir : searchDirs )
	{
		const String pathString = dir + name;
		res::ResourcePath path = res::ResourcePath::Build( pathString );
		if( res::GetEditorDepot()->ResourceExists( path ) )
		{
			res::IssueLoadingRequestParameter params( path );
			params.skipPostLoad = true;
			params.skipBuffers = true;

			if( THandle< CResource > loadedMat = GResourceLoader->TryAcquiringLoadedResource( path ) )
			{
				return Cast< IMaterial >( loadedMat );
			}
			else if( const THandle< CResource > matRes = GResourceLoader->IssueLoadingRequest( params )->WaitUntilLoaded() )
			{
				return Cast< IMaterial >( matRes );
			}
		}
	}

	return nullptr;
}


void ImportMaterials( THandle< CMesh >& parentMesh, const std::vector< ReFileMeshPtr >& meshes, mesh::DataView& buildData, Bool replaceExistingMaterials )
{
	MESH_IMPORT_LOG_INFO( "   Importing materials..." );
	DynArray< String > materials;


	for( Uint32 m = 0; m < (Uint32)meshes.size(); m++ )
	{
		auto mesh = meshes[ m ];
		Int32 meshChunkCount = mesh->getNumChunks();

		red::DynArray< Uint32 > materialHashes;

		if( parentMesh->GetAllAppearances().Empty() )
		{
			THandle< mesh::MeshAppearance > defaultAppearance( CreateHandle< mesh::MeshAppearance >( parentMesh.Get() ) );
			buildData.ReplaceDefaultAppearance( defaultAppearance );
		}

		for( Int32 i = 0; i < (Int32)mesh->getNumChunks(); i++ )
		{
			ReFileMeshChunkPtr meshChunk = mesh->getMeshChunk( i );
			String matName;
			ReFileHelpers::GetMaterialName( *meshChunk.get(), matName );

			int index = -1;

			MESH_IMPORT_LOG_INFO( "      Processing mesh %u chunk %u,   material name '%hs'", m, i, matName.AsChar() );

			if( matName.Empty() )
				matName = "default"; // consistent with chunk import

			CName materialName = RED_NAME( matName );

			// --- This is a slightly different method of importing material instance from Re mesh chunk.
			if( ReFileHelpers::JSONMaterialInstanceSerializer::HasMaterialInstanceData( meshChunk ) )
			{
				std::string materialNameBlob;
				std::string materialDataBlob;
				meshChunk->getTextBlob( "originalMaterialName", materialNameBlob );
				meshChunk->getTextBlob( "originalMaterialData", materialDataBlob );
				materialName = RED_NAME( materialNameBlob.c_str() );
				THandle< CMaterialInstance > matInstance = ReFileHelpers::JSONMaterialInstanceSerializer::Deserialize( parentMesh.Get(), materialDataBlob, materialName );
				buildData.AddMaterial( matInstance, materialName, -1, replaceExistingMaterials );
				continue;
			}

			CName materialTemplateName;
			// by default try to assign standard metal_base template
			if( !ReFileHelpers::GetMaterialTemplateName( *meshChunk.get(), materialTemplateName ) )
			{
				materialTemplateName = RED_NAME_CONSTEXPR( "metal_base" );
			}

			const String materialInstanceFileName = String::Printf( "%hs.mi", materialTemplateName.AsChar() );
			// TODO: remove support for .remt once all materials are resaved and converted to .mt
			const String materialFileName = String::Printf( "%hs.remt", materialTemplateName.AsChar() );
			const String materialFileName_new = String::Printf( "%hs.mt", materialTemplateName.AsChar() );

			red::DynArray< String > directories;
			GatherMaterialsDirectories( directories );

			THandle< IMaterial > material = FindMaterial( directories, materialInstanceFileName );
			if( !material )
				material = FindMaterial( directories, materialFileName );
			if( !material )
				material = FindMaterial( directories, materialFileName_new );
			// parameters

			if( material )
			{
				MESH_IMPORT_LOG_INFO( "         Creating instance of '%hs'", material->GetPath().ToString().AsChar() );
				/* Create material instance */
				THandle< CMaterialInstance > matInstance = CreateHandle< CMaterialInstance >( parentMesh.Get(), material );
				if( !matInstance )
				{
					RED_LOG_ERROR( "Core: Creating material instance for mesh failed: %ls.", matName.AsChar() );
					return;
				}

				if( !ReFileHelpers::ImportMaterialParameters( *meshChunk.get(), matInstance.Get() ) )
				{
					RED_LOG_ERROR( "Core: Creating material instance parameters for mesh failed: %ls.", matName.AsChar() );
					return;
				}

				Bool materialAdded = buildData.AddMaterial( matInstance, materialName, -1, replaceExistingMaterials );

				MESH_IMPORT_LOG_INFO( "      Added material: %hs to chunk: %u      ", matName.AsChar(), i );
			}
			else
			{
				MESH_IMPORT_LOG_INFO( "         No material found, using default material '%hs'", resMaterialDefault.GetPath().ToDebugString() );
				material = resMaterialDefault.Get();
				Bool materialAdded = buildData.AddMaterial( material, materialName, -1, replaceExistingMaterials );
			}
		}
	}

	MESH_IMPORT_LOG_INFO( "   Importing materials ended." );
}
