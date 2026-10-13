/**
* Copyright (c) 2008-16 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"

#include "re_archive/reFileArchive.h"

#include "importMeshREX_Common.h"
#include "importMeshREX_Chunks.h"
#include "importMeshREX_Cloth.h"
#include "importMeshREX_Materials.h"
#include "importMeshREX_Physics.h"
#include "importMeshREX_Skinning.h"
#include "importMeshREX_Utils.h"

#include "../../../common/renderData/include/visibilityWorldCommon.h"

#include "../../../common/redReflection/include/handle.h"

#include "../../../common/resourceMesh/include/meshAppearance.h"

#include "../../../backend/backendEngine/include/resourceMetadata.h"

#include "../../../backend/backendMesh/include/meshDataView.h"
#include "../../../backend/backendMesh/include/meshDataBuilder.h"

#include "../../../backend/backendREA/include/reFileHelpers.h"
#include "../../../common/resourceMesh/include/meshParamDestructionStepsData.h"


using red::DynArray;
using red::String;


//------------------------------------------------------------------------------------------------
class CREXMeshImporter : public IImporter
{
	RTTI_DECLARE_TYPE( CREXMeshImporter );

public:
	CREXMeshImporter();
	virtual ImportResult DoImport( THandle< CResource > inputResource, const THandle< ResourceImportOptions >& options ) override;
	virtual THandle< ResourceImportOptions > CreateOptions( const red::AbsolutePath& sourcePath ) override;
};


RTTI_BEGIN_TYPE( CREXMeshImporter )
	RTTI_PARENT_TYPE( IImporter )
RTTI_END_TYPE()


CREXMeshImporter::CREXMeshImporter()
{
	m_resourceClass = ClassID< CMesh >();
	m_formats.PushBack( CFileFormat( "re", "Red Engine File" ) );
}


THandle< ResourceImportOptions > CREXMeshImporter::CreateOptions( const red::AbsolutePath& sourcePath )
{
	THandle< CREXMeshResourceImportOptions > options = CreateHandle< CREXMeshResourceImportOptions >();
	options->ParseRenderFlagsFromReFile( sourcePath );
	return options;
}


ImportResult CREXMeshImporter::DoImport( THandle< CResource > inputResource, const THandle< ResourceImportOptions >& importOptions )
{
	String importMessages;
	// get mesh to import into
	THandle< CMesh > meshData = Cast< CMesh >( inputResource );
	if( !meshData )
	{
		return ImportResult::Error( "Core: Unable to import mesh into something that is not a mesh" );
	}

	mesh::DataView view( *meshData );

	THandle< CREXMeshResourceImportOptions > options = Cast< CREXMeshResourceImportOptions >( importOptions );
	RED_FATAL_ASSERT( options != nullptr );

	const red::AbsolutePath sourceFilePath = importOptions->GetSourcePath();

	std::unique_ptr< ReReadStream > reFile( ReReadStream::Create( sourceFilePath.AsChar() ) );
	if( reFile == nullptr )
	{
		return ImportResult::Error( "Failed to read the RE file." );
	}

	const ReFileHeader2 header = reFile->GetHeader();

	if( header.getReFileVersion() != RE_FILE_SYSTEM_CURRENT_VERSION )
	{
		red::StringBuilder< String > builder;
		builder.Append( "EXPORT REQUIRED! Re file format version is outdated and it has to be re-exported.\n" );
		builder.Append( " File format version is " );
		builder.Append( header.getReFileVersion() );
		builder.Append( ", current Re version is: " );
		builder.Append( RE_FILE_SYSTEM_CURRENT_VERSION );
		return ImportResult::Error( builder.ToString() );
	}

	// New import metadata
	THandle< res::ResourceImportMetadata > importMetadata = CreateHandle< res::ResourceImportMetadata >();
	ReFileHelpers::GetImportMetadata( &header, importMetadata );
	importMetadata.Get()->SetIntermediatePath( sourceFilePath );

	THandle< res::ResourceMetadata > metadata = res::ResourceMetadata::Get( meshData );

	if( !metadata )
		metadata = CreateHandle< res::ResourceMetadata >();

	metadata->SetImportData( importMetadata );
	metadata->ClearThumbnail();
	metadata->GetTags().Remove( RED_NAME_CONSTEXPR( "lod0Removed" ) );

	res::ResourceMetadata::Set( meshData, metadata );

	std::vector< ReFileMeshPtr > meshes;

	std::vector< ReFileOcclusionMeshPtr > occlusions;
	std::vector< ReFileSkeletonPtr > skeleton;

	ReCollisionsImportContext collContext;

	// Legacy support - collisions are now moved to ReFileMesh
	reFile->Read( meshes );
	reFile->Read( collContext.convexCollisions );
	reFile->Read( collContext.boxes );
	reFile->Read( collContext.spheres );
	reFile->Read( collContext.capsules );
	reFile->Read( collContext.triMeshes );
	reFile->Read( occlusions );
	reFile->Read( skeleton );

	Bool importDestructionData = ( (Uint32)skeleton.size() > 0 ) && ( skeleton[ 0 ]->getDynamicLinks().size() > 0 );
	if( importDestructionData )
	{
		// this is probably not needed - only physical destruction should use dynamic links for bonds, but I am paranoid at this point
		if( !sourceFilePath.AsString().Contains( "_dst" ) )
		{
			importDestructionData = false;
			importMessages += "Asset with destruction data in.re file but no _dst in name?\n";
		}
	}

	if( meshes.size() == 0 )
	{
		return ImportResult::Error( "Found no meshes in the file!" );
	}

	Uint32 totalNumChunks = 0;

	for( const auto m : meshes )
	{
		totalNumChunks += m->getNumChunks();
	}

	if( totalNumChunks == 0 )
	{
		return ImportResult::Error( "Found no chunks in the meshes!" );
	}

	ReadCollisionsFromMeshes( meshes, collContext );

	collContext.reMesh = meshes[ 0 ];

	ImportMaterials( meshData, meshes, view, options->m_importMaterials );
	view.Apply(); // creates the actual materials

	// import geometry
	mesh::DataBuilder builder;
	// builder is extracted into this scope because it holds information about skinning.
	// This information is used later to determine if physics must be created as separate actors
	// or a single actor
	{
		// import bones
		DynArray< DynArray< Int32 > > rigMapping;
		if( options->m_importRigs )
		{
			ImportRigMatrices( meshes, builder, rigMapping );
		}
		else
		{
			rigMapping.Resize( (Uint32)meshes.size() );
		}

		// import geometry
		mesh::DataViewRO meshView( *meshData );

		Bool prepareMeshTopology = ( options->m_importAsGraphicalClothMesh || options->m_prepareMeshTopology );

		ImportGarmentSupportData( meshData, meshes );
		ImportResult result = ImportChunks( meshData.Get(), meshes, rigMapping, meshView, options->m_renderFlags, options->m_importUV2AsMorphVertexData, prepareMeshTopology, options, builder, importDestructionData );
		if( !result.IsSuccessful() )
		{
			return result;
		}

		if( result.HasMessage() )
		{
			importMessages += result.GetSuccessMessage();
		}
		// look for a bbox ( re_bbox data ) if exist override it;
		// this is because meshes with animation in vertex shader may disappear when some part of mesh is still in camera frustum,
		// but bbox was calculated on mesh before vertex shader was evaluated. If we know exact size of bbox including change in position by vertex shader we can override it
		// default bbox is recalculated in builder.Flush(), so if we have our external bbox data, don't do that
		Bool rebuildBBox = true;
		ImportBBoxData( meshes[ 0 ], meshData, rebuildBBox );

		// import mesh parameters
		ImportMeshDataParameters( meshes, meshData );

		// import the animated float track names. They are always stored on the first mesh
		if( meshes.size() > 0 )
			ImportFloatTrackNames( meshes[ 0 ], builder );

		{
			// mesh::DataView view( *meshData );

			MESH_IMPORT_LOG( "   Flushing the mesh, appearance correcting happens here:\r\n"
							 "      Old number of chunks: %u\r\n"
							 "      New number of chunks: %u\r\n",
							 view.GetNumChunks(), builder.m_chunks.Size() );
		}

		// if we have baked destruction we need to cleanup fake bones used to import multiple actors (if there are any)
		if( auto paramBDRD = meshData->GetParameter< mesh::MeshParamBakedDestructionData >() )
		{
			red::DynArray< CName > newBonesNames = { red::PoolBackend() };
			red::DynArray< Matrix > newBoneRigMatrices = { red::PoolBackend() };
			red::DynArray< Float > newBoneVertexEpsilons = { red::PoolBackend() };	
			view.ResetBones( newBonesNames, newBoneRigMatrices, newBoneVertexEpsilons );
			view.Apply();
			builder.m_bones.Clear();
		}

		// create geometry in mesh
		mesh::DataBuilder::FlushOptions flushOptions;
		flushOptions.m_verbose = true;
		flushOptions.m_rebuildMaterials = options->m_importMaterials;
		flushOptions.m_rebuildLODDistances = options->m_resetLODDistances;
		flushOptions.m_rebuildBBox = rebuildBBox;
		flushOptions.m_extrudeBBox = options->m_extrudeBoundingBox;
		flushOptions.m_boundingBoxExtrusionFactor = options->m_boundingBoxExtrusionFactor;
		flushOptions.m_matchByOrder = options->m_matchChunksByOrder;
		builder.Flush( *meshData, flushOptions );
	}

	// remove unused materials
	if( options->m_removeUnusedMaterials )
	{
		// mesh::DataView view( *meshData );
		view.RemoveUnusedMaterials();
		view.Apply(); // creates the actual materials
	}

	// Make sure the appearances are correct.
	{
		Uint32 numMeshChunks = view.GetNumChunks();
		Bool rebuildNeeded = false;

		for( Uint32 appearanceIndex = 0; appearanceIndex < view.GetNumAppearances(); ++appearanceIndex )
		{
			THandle< mesh::MeshAppearance > appearance = view.GetAppearanceData( appearanceIndex );
			Uint32 numAppearanceChunks = appearance->GetNumChunks();

			if( numAppearanceChunks != numMeshChunks )
			{
				mesh::DataBuilder::CorrectNumberOfChunksInAppearance( *meshData, appearance );
				rebuildNeeded = true;
			}
		}

		if( rebuildNeeded )
			view.Apply();
	}

	if( options->m_importCollision )
	{
		ImportResult result = ImportCollisionData( meshData.Get(), options, collContext, builder );
		if( result.HasMessage() )
		{
			importMessages += result.GetSuccessMessage();
		}
	}

    if( options->m_createSkinningLOD && !importDestructionData )
    {
        if( view.CreateSkinningLODs() )
        {
            view.Apply();
        }
    }

	if( options->m_importAsPhysicalClothMesh )
	{
		ImportPhysicalClothData( meshData, meshes );
	}

	if( options->m_importAsGraphicalClothMesh )
	{
		ImportGraphicalClothData( meshData, meshes );
	}

	if( options->m_importSnapPoints )
	{
		ImportSnapFeatures( meshes, meshData );
	}

	if( options->m_importOcclusion )
	{
		ImportResult result = ImportOcclusionData( occlusions, sourceFilePath, options, meshData );
		if( result.HasFailed() )
			return result;
	}

	{
		if( options->m_importWorkspotOffsets )
		{
			ImportWorkspotOffsets( meshes, meshData );
		}
	}

	{
		meshData->SetRenderObjectType( options->m_objectType );
	}

	if( importDestructionData )
	{
		ClearDestructionData( meshData.Get() );
		ImportDestructionHierarchy( meshData.Get(), skeleton[ 0 ], builder );
	}
	else
	{
		ImportResult result = ClearDestructionData( meshData.Get() );
		if( result.HasMessage() )
		{
			importMessages += result.GetSuccessMessage();
		}
	}

	{
		CDateTime importDateTime;
		red::Clock::GetInstance().GetLocalTime( importDateTime );
		view.ForceSetSaveDate( importDateTime );
		view.ForceRecalculateGeometryHash();
		view.Apply();
	}


	return ImportResult::Success( meshData, importMessages );
}
