/**
* Copyright (c) 2019 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"
#include "../../../common/core/include/commandlet.h"
#include "../../../common/redCore/include/commandline.h"
#include "../../../common/redReflection/include/resourceDepot.h"
#include "../../../common/renderData/include/textureGroup.h"
#include "../../../common/renderData/include/visibilityOccluderMeshResource.h"
#include "../../../common/resource/include/basicTextBuilder.h"
#include "../../../common/resourceMaterial/include/material.h"
#include "../../../common/resourceMaterial/include/texture.h"
#include "../../../common/resourceMesh/include/mesh.h"
#include "../../../common/resourceMesh/include/meshDataViewRO.h"
#include "meshDataView.h"
#include "../../../common/resourceMesh/include/meshParamOccluderData.h"
#include "../../../common/resourceMesh/include/meshParamPhysics.h"
#include "../../../common/physics/include/physicsSystemResource.h"
#include "../../../common/physics/include/physicsSystemBody.h"
#include "../../backendData/include/resourceDatabase.h"
#include "../../backendEngine/include/commandletEngineInstance.h"
#include "../../backendEngine/include/commandletUtils.h"
#include "../../backendEngine/include/resourceImportMetadata.h"
#include "../../backendEngine/include/resourceMetadata.h"
#include "gatherResourceDataCommandletUtils.h"
#include "../../../common/resourceMaterial/include/materialTemplate.h"
#include "../../../common/redReflection/include/handle.h"
#include "../../../common/renderBackend/include/materialLibrary.h"
#include "../../../common/resourceMaterial/include/materialInstance.h"
#include "../../../common/redFileSystem/include/fileSys.h"

#include "../../../common/resource/include/resourceSaver.h"

namespace
{
	const Uint32 c_maxNumberOfLODS = 4u;
}


namespace tools
{
	struct MeshRowData;
	struct TextureRowData;

	class GatherMeshDataCommandlet: public red::ICommandlet
	{
		RTTI_DECLARE_TYPE( GatherMeshDataCommandlet );

	public:
		GatherMeshDataCommandlet();
		~GatherMeshDataCommandlet();

		virtual Bool Execute( const red::CommandLine& options ) override;
		virtual const char* GetOneLiner() const override;
		virtual void PrintHelp() const override;

		virtual Bool InitializeEnvironment() override;
		virtual void TeardownEnvironment() override;

	private:
		struct Settings;
		Bool ParseSettings( const red::CommandLine& options, Settings& settingsFilePath );

		void InitialiseDatabase( commandlet::BackendDepotHolder& depotHolder );

		void GatherSupportedExtensions( red::HashSet< String >& outAllowedExtenstions ) const;
		void ProcessResource( const res::ResourcePath& resourcePath, red::DynArray< MeshRowData >& outMeshData, red::DynArray< TextureRowData >& outTextureData, Uint32& maxNumberOfLODs ) const;

		Uint32 GetLODNumberOfBones( const mesh::DataViewRO& meshView, mesh::LODIndex i ) const;
		Uint32 GetLODTrianglesCount( const mesh::DataViewRO& meshView, mesh::LODIndex i ) const;
		Uint32 GetNumberOfMLMasks( const mesh::DataViewRO& meshView ) const;
		Float GetBoundsDiag( const mesh::DataViewRO& meshView ) const;
		Float GetSurfaceArea( const mesh::DataViewRO& meshView ) const;
		Uint32 GetOccludersVertexCount( const THandle< CMesh>& mesh ) const;
		Bool HasMeshFlag( const mesh::DataViewRO& meshView, EMeshStreamType flag ) const;
		String GetAssetPath( const THandle<CResource>& resource ) const;
		Uint32 GetNumberOfReferences( const res::ResourcePath& path ) const;
		String GetCompressionName( const ETextureCompression& compression ) const;
		Uint32 GetNumberOfCollisionShapes(const mesh::DataViewRO& meshView) const;
		Uint32 GetCollisionMeshMemory(const mesh::DataViewRO& meshView) const;

		Bool SaveMeshesAsCSV( const red::AbsolutePath& outFilePath, const red::DynArray< MeshRowData >& objects, Uint32 numberOfElementsInArrays ) const;
		Bool SaveTexturesAsCSV( const red::AbsolutePath& outFilePath, const red::DynArray< TextureRowData >& objects ) const;

		commandlet::EngineInstance m_engineInstance;
		res::IBackendResourceDepot* m_depot;
		resdb::Database m_database;
	};

	struct GatherMeshDataCommandlet::Settings
	{
		res::ResourcePath m_directoryPath;
		red::AbsolutePath m_outputFile;
	};

	// Serializable out data

	struct MeshRowData
	{
		res::ResourcePath m_mesh;
		red::DynArray< Uint32 > m_numberOfTrianglesLOD;
		red::DynArray< Uint32 > m_numberOfChunksLOD;
		red::DynArray< Uint32 > m_numberOfBonesLOD;
		Uint32 m_numberOfMaterialAppearances;
		Uint32 m_numberOfUsedMaterials;
		Uint32 m_numberOfMLMasks;
		Uint32 m_numberOfBones;
		Float m_autohideDistance;
		Bool m_garmentFlags;
		Bool m_garmentMorph;
		String m_assetPath;
		Float m_boundsDiag;
		Float m_surfaceArea;
		Uint32 m_occludersVertexCount;
		Uint32 m_numberOfCollisionShapes;
		Uint32 m_collisionMeshMemory;
		ERenderObjectType m_objectType;
		Float m_memoryUsageGPU;
	};

	struct TextureRowData
	{
		res::ResourcePath m_texture;
		Uint32 m_resolution;
		String m_compression;
		Uint32 m_mipmapBiasPC;
		Uint32 m_mipmapBiasConsole;
		Uint32 m_referenced;
		Float m_memoryUsageGPU;
	};

	// IInteractiveCommandlet Interface

	GatherMeshDataCommandlet::GatherMeshDataCommandlet()
		: ICommandlet( RED_NAME_CONSTEXPR( "gatherMeshData" ) )
	{
	}

	GatherMeshDataCommandlet::~GatherMeshDataCommandlet()
	{
	}

	Bool GatherMeshDataCommandlet::Execute( const red::CommandLine& options )
	{
		Settings settings;
		if ( !ParseSettings( options, settings ) )
		{
			RED_LOG_ERROR( "Invalid settings" );
			return false;
		}

		commandlet::BackendDepotHolder depotHolder;
		InitialiseDatabase( depotHolder );
		
		red::DynArray< res::ResourcePath > resourcePaths{ red::PoolBackend() };
		red::HashSet< String > supportedExtensions{ red::PoolBackend() };
		GatherSupportedExtensions( supportedExtensions );
		FindResourcesByExtensionRecursive( settings.m_directoryPath.ToString(), supportedExtensions, resourcePaths );

		red::DynArray< MeshRowData > meshData{ red::PoolBackend() };
		red::DynArray< TextureRowData > textureData{ red::PoolBackend() };

		Uint32 maxNumberOfLODs = c_maxNumberOfLODS;
		meshData.Reserve( resourcePaths.Size() );
		textureData.Reserve( resourcePaths.Size() );
		
		for ( const auto& resource : resourcePaths )
		{
			ProcessResource( resource, meshData, textureData, maxNumberOfLODs );
		}

		String outDirectory = red::paths::ExtractParentPath( settings.m_outputFile );
		String outFileName = red::paths::ExtractFileStem( settings.m_outputFile );

		Bool result = true;
		result &= SaveMeshesAsCSV( red::AbsolutePath::CreateFilePath( outDirectory + outFileName + "_mesh.csv" ), meshData, maxNumberOfLODs );
		result &= SaveTexturesAsCSV( red::AbsolutePath::CreateFilePath( outDirectory + outFileName + "_texture.csv" ), textureData );
		return result;
	}

	const char* GatherMeshDataCommandlet::GetOneLiner() const
	{
		return "Gather informations about meshes and textures from a directory";
	}

	void GatherMeshDataCommandlet::PrintHelp() const
	{
		RED_LOG( "Usage:" );
		RED_LOG( " gatherMeshData -dir=<resource_path_to_scanned_directory> -out=<absolute_path_to_csv_file>" );
	}

	Bool GatherMeshDataCommandlet::InitializeEnvironment()
	{
		return m_engineInstance.Initialise();
	}

	void GatherMeshDataCommandlet::TeardownEnvironment()
	{
		m_engineInstance.Shutdown();
	}

	// The work stuff

	Bool GatherMeshDataCommandlet::ParseSettings( const red::CommandLine& options, Settings& settings )
	{
		String path;
		if ( options.GetFirstOption( "dir", path ) )
		{
			settings.m_directoryPath = res::ResourcePath::Build( path );
		}

		if ( options.GetFirstOption( "out", path ) )
		{
			settings.m_outputFile = red::AbsolutePath::CreateFilePath( red::StringView( path ) );
		}

		if ( !settings.m_directoryPath.IsValid() )
			return false;

		if ( !settings.m_outputFile.IsFilePath() )
			return false;

		return true;
	}

	void GatherMeshDataCommandlet::InitialiseDatabase( commandlet::BackendDepotHolder& depotHolder )
	{
		m_depot = depotHolder.GetDepot();

		job::Counter counter;
		red::AbsolutePath resdbCachePath = depotHolder.GetCachePath().AddFilePath( resdb::Database::GetDefaultFilename() );
		m_database.Initialise_NoUpdate( *m_depot, resdbCachePath, counter );
		job::FlushCounter( counter );
	}

	void GatherMeshDataCommandlet::GatherSupportedExtensions( red::HashSet< String >& outAllowedExtenstions ) const
	{
		tools::GatherSupportedExtensions( CMesh::GetStaticClass(), outAllowedExtenstions );
		tools::GatherSupportedExtensions( ITexture::GetStaticClass(), outAllowedExtenstions );
	}

	void GatherMeshDataCommandlet::ProcessResource( const res::ResourcePath& resourcePath, red::DynArray< MeshRowData >& outMeshData, red::DynArray< TextureRowData >& outTextureData, Uint32& maxNumberOfLODs ) const
	{
		res::IssueLoadingRequestParameter loadingRequest( resourcePath );
		loadingRequest.postLoadFlags.skipUploadRenderData = true;
		loadingRequest.skipImports = false;
		loadingRequest.skipPostLoad = false;

		THandle< CResource > resource = GResourceLoader->IssueLoadingRequest( loadingRequest )->WaitUntilLoaded();
		if ( !resource )
		{
			RED_LOG_ERROR( "Failed loading resource %hs", resourcePath.ToDebugString() );
			return;
		}

		if ( auto mesh = Cast< CMesh >( resource ) )
		{
			mesh::DataViewRO meshView( *mesh );
			Uint32 numOfLODs = meshView.GetNumLODs();
			maxNumberOfLODs = Max( maxNumberOfLODs, numOfLODs );

			MeshRowData& meshData = outMeshData.EmplaceBack();
			meshData.m_numberOfTrianglesLOD.Reserve( numOfLODs );
			meshData.m_numberOfChunksLOD.Reserve( numOfLODs );
			meshData.m_numberOfBonesLOD.Reserve( numOfLODs );

			meshData.m_mesh = mesh->GetPath();
			for ( mesh::LODIndex i = 0u; i < numOfLODs; ++i )
			{
				meshData.m_numberOfTrianglesLOD.PushBack( GetLODTrianglesCount( meshView, i ) );
				meshData.m_numberOfChunksLOD.PushBack( meshView.GetLODChunkCount( i ) );
				meshData.m_numberOfBonesLOD.PushBack( GetLODNumberOfBones( meshView, i ) );
			}
			meshData.m_numberOfMaterialAppearances = meshView.GetNumAppearances();
			meshData.m_numberOfUsedMaterials = meshView.GetNumMaterials();
			meshData.m_numberOfMLMasks = GetNumberOfMLMasks( meshView );
			meshData.m_numberOfBones = meshView.GetNumBones();
			meshData.m_autohideDistance = meshView.GetAutoHideDistance();
			meshData.m_garmentFlags = HasMeshFlag( meshView, MST_GarmentFlags_U32 );
			meshData.m_garmentMorph = HasMeshFlag( meshView, MST_MorphOffset_3F );
			meshData.m_assetPath = GetAssetPath( mesh );
			meshData.m_boundsDiag = GetBoundsDiag( meshView );
			meshData.m_surfaceArea = GetSurfaceArea( meshView );
			meshData.m_occludersVertexCount = GetOccludersVertexCount( mesh );
			meshData.m_numberOfCollisionShapes = GetNumberOfCollisionShapes( meshView );
			meshData.m_collisionMeshMemory = GetCollisionMeshMemory( meshView );
			meshData.m_objectType = mesh->GetRenderObjectType();
			meshData.m_memoryUsageGPU = mesh->GetGPUSize() / ( 1024.0f * 1024.0f );
		} 
		else if ( auto texture = Cast< ITexture >( resource ) )
		{
			const STextureGroupSetup& textureSetup = texture->GetTextureSetup();

			TextureRowData& textureData = outTextureData.EmplaceBack();

			textureData.m_texture = texture->GetPath();
			textureData.m_resolution = texture->GetSizeDesc().width; // TODO[PB]: Verify
			textureData.m_compression = GetCompressionName( textureSetup.m_compression );
			textureData.m_mipmapBiasPC = textureSetup.m_platformMipBiasPC;
			textureData.m_mipmapBiasConsole = textureSetup.m_platformMipBiasConsole;
			textureData.m_referenced = GetNumberOfReferences( texture->GetPath() );
			textureData.m_memoryUsageGPU = texture->GetGPUSize() / ( 1024.0f * 1024.0f );
		}
		else
		{
			RED_FATAL_ASSERT( 0 != 0, "Unknow file type" );
		}
	}

	// Count all the bones that affect any of the chunks that belong to this LOD (as per my understanding)
	Uint32 GatherMeshDataCommandlet::GetLODNumberOfBones( const mesh::DataViewRO& meshView, mesh::LODIndex i ) const
	{
		if( meshView.GetNumBones() == 0 )
			return 0u;

		red::DynArray< Uint32 > boneMap{ meshView.GetNumBones(), red::PoolBackend() };
		
		for( Uint16 chunkIndex: meshView.GetLODChunkIndices( i ) )
		{
			const mesh::StreamViewRO< Color > dataBoneIndices( meshView.GetChunkStreamData( chunkIndex, MST_SkinningIndices_4U8 ) );
			const mesh::StreamViewRO< Vector4 > dataBoneWeights( meshView.GetChunkStreamData( chunkIndex, MST_SkinningWeights_4F ) );
			if( dataBoneWeights.IsValid() && dataBoneIndices.IsValid() )
			{
				for( Uint32 j = 0; j < dataBoneIndices.Size(); ++j )
				{
					auto indices = dataBoneIndices[ j ];
					auto weights = dataBoneWeights[ j ];

					for( Uint32 i = 0; i < 4; ++i )
					{
						if( weights[ i ] > 0.f )
						{
							boneMap[ indices.RGBA[ i ] ]++;
						}
					}
				}
			}

			const mesh::StreamViewRO< Color > dataBoneIndicesExt( meshView.GetChunkStreamData( chunkIndex, MST_SkinningIndicesExt_4U8 ) );
			const mesh::StreamViewRO< Vector4 > dataBoneWeightsExt( meshView.GetChunkStreamData( chunkIndex, MST_SkinningWeightsExt_4F ) );
			if( dataBoneWeightsExt.IsValid() && dataBoneIndicesExt.IsValid() )
			{
				for( Uint32 j = 0; j < dataBoneIndicesExt.Size(); ++j )
				{
					auto indices = dataBoneIndicesExt[ j ];
					auto weights = dataBoneWeightsExt[ j ];

					for( Uint32 i = 0; i < 4; ++i )
					{
						if( weights[ i ] > 0.f )
						{
							boneMap[ indices.RGBA[ i ] ]++;
						}
					}
				}
			}
		}

		Uint32 numberOfBones = 0u;
		for( Uint32 i = 0; i < boneMap.Size(); ++i )
		{
			if( boneMap[ i ] > 0 )
			{
				++numberOfBones;
			}
		}
		return numberOfBones;
	}

	Uint32 GatherMeshDataCommandlet::GetLODTrianglesCount( const mesh::DataViewRO& meshView, mesh::LODIndex i ) const
	{
		Uint32 numTriangles = 0u;
		for ( Uint32 j = 0; j < meshView.GetLODChunkIndices( i ).Size(); j++ )
		{
			Uint16 chunkIndex = meshView.GetLODChunkIndices( i )[ j ];
			numTriangles += meshView.GetChunkTriangleCount( chunkIndex );
		}
		return numTriangles;
	}

	Uint32 GatherMeshDataCommandlet::GetNumberOfMLMasks( const mesh::DataViewRO& meshView ) const
	{
		red::Set< res::ResourcePath > mlMaskFilePaths{ red::PoolBackend() };
		for ( const THandle< IMaterial >& material : meshView.GetMaterials() )
		{
			TResRef< Multilayer_Mask > mask;
			if ( material->ReadParameter( RED_NAME_CONSTEXPR( "MultilayerMask" ), mask ) )
			{
				if ( GetResourceDepot()->ResourceExists( mask.GetPath() ) )
				{
					mlMaskFilePaths.Insert( mask.GetPath() );
				}
				else
				{
					RED_LOG_ERROR( "Mesh %hs points to invalid multilayer mask resource %hs. ", meshView.GetMesh()->GetPath().ToDebugString(), mask.GetPath().ToDebugString() );
				}
			}
		}
		return mlMaskFilePaths.Size();
	}

	Float GatherMeshDataCommandlet::GetBoundsDiag( const mesh::DataViewRO& meshView ) const
	{
		const Box bounds = meshView.GetMesh()->GetBoundingBox();
		const Vector3 boundsMin = bounds.Min.AsVector3();
		const Vector3 boundsMax = bounds.Max.AsVector3();
		const float boundsDiag = ( boundsMax - boundsMin ).Mag();
		return boundsDiag;
	}

	Float GatherMeshDataCommandlet::GetSurfaceArea( const mesh::DataViewRO& meshView ) const
	{
		Double surfaceArea = 0.0;
		for( Uint32 chunkIndex : meshView.GetLODChunkIndices( 0 ) )
		{
			surfaceArea += meshView.GetChunkSurfaceArea( chunkIndex );
		}
		return static_cast< Float >( surfaceArea );
	}

	Uint32 GatherMeshDataCommandlet::GetOccludersVertexCount( const THandle< CMesh >& mesh ) const
	{
		Uint32 vertexCount = 0u;

		if ( THandle< mesh::MeshParamOccluderData > occParam = mesh->GetParameter< mesh::MeshParamOccluderData >() )
		{
			if (THandle< vis::IOccluderResource > occluder = occParam->GetOccluderResource() )
			{
				if ( THandle< vis::OccluderMeshResource > meshOccluder = Cast< vis::OccluderMeshResource >( occluder ) )
				{
					vertexCount = meshOccluder->GetVertices().Size();
				}
			}
		}

		return vertexCount;
	}

	Bool GatherMeshDataCommandlet::HasMeshFlag( const mesh::DataViewRO& meshView, EMeshStreamType flag ) const
	{
		for ( Uint32 i = 0; i < meshView.GetNumChunks(); ++i )
		{
			if ( !meshView.GetChunkStreamData( i, flag ).Empty() )
			{
				return true;
			}
		}
		return false;
	}

	String GatherMeshDataCommandlet::GetAssetPath( const THandle<CResource>& resource ) const
	{
		if( THandle<res::ResourceMetadata> meta = res::ResourceMetadata::Get( resource ) )
		{
			if( const THandle<res::ResourceImportMetadata>& importMeta = meta->GetImportData() )
			{
				return importMeta->GetAssetPath();
			}
		}
		return String::EMPTY();
	}

	Uint32 GatherMeshDataCommandlet::GetNumberOfReferences( const res::ResourcePath& path ) const
	{
		return m_database.GetBackwardLinks( path ).Size();
	}

	String GatherMeshDataCommandlet::GetCompressionName( const ETextureCompression& compression ) const
	{
		String compressionName;
		const rtti::EnumType* targetEnum = GetRttiSystem().FindEnum( ::GetTypeName<ETextureCompression>() );
		targetEnum->ToString( &compression, compressionName );
		return compressionName;
	}

	Uint32 GatherMeshDataCommandlet::GetNumberOfCollisionShapes( const mesh::DataViewRO& meshView ) const
	{
		Uint32 result = 0;

		if( auto param = meshView.GetParameter<mesh::MeshParamPhysics>() )
		{
			if( auto system_resource = param->GetPhysicsData() )
			{
				const auto& bodies = system_resource->GetBodies();
				result = std::accumulate( begin( bodies ), end( bodies ), 0, []( Uint32 i, const auto& body ){ return i + body->m_collisionShapes.Size(); } );
			}
		}

		return result;
	}

	Uint32 GatherMeshDataCommandlet::GetCollisionMeshMemory( const mesh::DataViewRO& meshView ) const
	{
		Uint32 result = 0;

		if( auto param = meshView.GetParameter<mesh::MeshParamPhysics>() )
		{
			if( auto system_resource = param->GetPhysicsData() )
			{
				const auto& bodies = system_resource->GetBodies();
				for( auto& body : bodies )
				{
					for( auto& shape : body->m_collisionShapes )
					{
						if( auto collider = Cast< physics::ICollider >( shape ) )
						{
							result += static_cast< Uint32 >( collider->DebugMemoryFootprint() );
						}
					}
				}
			}
		}

		return result;
	}

	// If there is an array, serialize it as `numberOfElementsInArrays` separate columns
	Bool GatherMeshDataCommandlet::SaveMeshesAsCSV( const red::AbsolutePath& outFilePath, const red::DynArray< MeshRowData >& objects, Uint32 numberOfElementsInArrays ) const
	{
		// Header
		// Before making changes to csv file structure please verify with bartosz.gregorek

		CBasicTextBuilder textBuilder;
		textBuilder.Write( "Mesh;" );
		for ( Uint32 i = 0; i < numberOfElementsInArrays; ++i )
		{
			textBuilder.Writef( "LOD%d;", i );
		}

		for ( Uint32 i = 0; i < numberOfElementsInArrays; ++i )
		{
			textBuilder.Writef( "Chunks LOD%d;", i );
		}

		textBuilder.Write( "Materials Appearances;Used Materials;MLMask Number;Bones;Autohide Distance;GarmentFlags;Garment Morph;" );

		for( Uint32 i = 0; i < numberOfElementsInArrays; ++i )
		{
			textBuilder.Writef( "Bones LOD%d;", i );
		}

		textBuilder.Write( "Bounds Diagonal;Surface Area;Occluders Vertices;Collision Shapes Number;Collision Mesh Memory;Asset Path;Object Type;GPU Memory Usage\n" );

		// Data

		String row;
		row.Reserve( textBuilder.GetDataSize() );
		for ( const MeshRowData& object : objects )
		{
			row.Clear();
			row += object.m_mesh.ToString() + ";";

			Uint32 i = 0u;
			for ( ; i < object.m_numberOfTrianglesLOD.Size(); ++i )
			{
				row += String::Printf( "%d;", object.m_numberOfTrianglesLOD[ i ]  );
			}
			for ( ; i < numberOfElementsInArrays; ++i )
			{
				row += ";";
			}

			i = 0u;
			for ( ; i < object.m_numberOfChunksLOD.Size(); ++i )
			{
				row += String::Printf( "%d;", object.m_numberOfChunksLOD[ i ] );
			}
			for ( ; i < numberOfElementsInArrays; ++i )
			{
				row += ";";
			}

			row += String::Printf( "%d;%d;%d;%d;", object.m_numberOfMaterialAppearances, object.m_numberOfUsedMaterials, object.m_numberOfMLMasks, object.m_numberOfBones );
			row += String::Printf( "%f;%d;%d;", object.m_autohideDistance, object.m_garmentFlags, object.m_garmentMorph );

			i = 0u;
			for( ; i < object.m_numberOfBonesLOD.Size(); ++i )
			{
				row += String::Printf( "%d;", object.m_numberOfBonesLOD[ i ] );
			}
			for( ; i < numberOfElementsInArrays; ++i )
			{
				row += ";";
			}

			row += String::Printf( "%f;%f;", object.m_boundsDiag, object.m_surfaceArea );
			row += String::Printf( "%d;", object.m_occludersVertexCount );
			row += String::Printf( "%d;", object.m_numberOfCollisionShapes );
			row += String::Printf( "%d;", object.m_collisionMeshMemory );
			row += String::Printf( "%hs;", object.m_assetPath.AsChar() );

			const rtti::EnumType* enumType = GetRttiSystem().FindEnum( RED_NAME_CONSTEXPR_NOREG( "ERenderObjectType" ) );
			CName objectType;
			enumType->FindName( object.m_objectType, objectType );
			row += String::Printf( "%hs;", objectType.AsChar() );
			row += String::Printf( "%f\n", object.m_memoryUsageGPU );

			textBuilder.Write( row.AsChar() );
		}
		return textBuilder.SaveToFile( outFilePath );
	}

	Bool GatherMeshDataCommandlet::SaveTexturesAsCSV( const red::AbsolutePath& outFilePath, const red::DynArray< TextureRowData >& objects ) const
	{
		// Header
		// Before making changes to csv file structure please verify with bartosz.gregorek

		CBasicTextBuilder textBuilder;
		textBuilder.Write( "Texture;Resolution;Compression;Mipmap Bias PC;Mipmap Bias Console;empty;Referenced;GPU Memory Usage\n" );

		// Data

		for ( const TextureRowData& object : objects )
		{
			textBuilder.Writef( "%hs;%d;%hs;%d;", object.m_texture.ToString().AsChar(), object.m_resolution, object.m_compression.AsChar(), object.m_mipmapBiasPC );
			textBuilder.Writef( "%d;;%d;%f\n", object.m_mipmapBiasConsole, object.m_referenced, object.m_memoryUsageGPU );
		}
		return textBuilder.SaveToFile( outFilePath );
	}

	//////////////////////////////////////////////////////////////////////////
	//////////////////////////////////////////////////////////////////////////
	//////////////////////////////////////////////////////////////////////////

	class Mesh_RemapFrostedGlassCommandlet : public red::ICommandlet
	{
		RTTI_DECLARE_TYPE(Mesh_RemapFrostedGlassCommandlet);

	public:
		Mesh_RemapFrostedGlassCommandlet();

		virtual Bool InitializeEnvironment() override;
		virtual void TeardownEnvironment() override;
		virtual const char* GetOneLiner() const override;
		virtual void PrintHelp() const override;
		virtual Bool Execute(const red::CommandLine& options) override;

	private:
		Bool ParseParams(const red::CommandLine& options);
		Bool ProcessMaterialInstance(const THandle< CMaterialInstance >& matInstance, const THandle< IMaterial >& baseMaterial);

		Bool HandleMaterialInstance(const THandle< CMaterialInstance >& resource, const THandle< IMaterial >& baseMaterial);
		Bool HandleMesh(const THandle< CMesh >& resource, const THandle< IMaterial >& baseMaterial);
		Bool HandleResource(const res::ResourcePath& resourcePath, const THandle< IMaterial >& baseMaterial);

		void RemapScalar(const red::HashMap< CName, Float >& scalarParams, const CName paramName, const THandle< CMaterialInstance >& matInstance, const THandle< IMaterial >& baseMaterial);
		void RemapColor(const red::HashMap< CName, Color >& params, const CName paramName, const THandle< CMaterialInstance >& matInstance, const THandle< IMaterial >& baseMaterial);
		void RemapVector(const red::HashMap< CName, Vector4 >& params, const CName paramName, const THandle< CMaterialInstance >& matInstance, const THandle< IMaterial >& baseMaterial);
		void RemapTexture(const red::HashMap< CName, TResRef< ITexture > >& params, const CName paramName, const THandle< CMaterialInstance >& matInstance, const THandle< IMaterial >& baseMaterial);

		resdb::Database m_database;
		commandlet::EngineInstance m_engineInstance;

		res::ResourcePath m_glassMaterialPath;
		res::ResourcePath m_frostedGlassMaterialPath;

		THandle< CMaterialTemplate > m_glassMaterial;
		THandle< CMaterialTemplate > m_frostedGlassMaterial;

		Bool m_hasErrors = false;

		red::HashSet< res::ResourcePath > m_processedResources;

		res::ResourceEditorDepot* m_editorDepot;
	};

	Mesh_RemapFrostedGlassCommandlet::Mesh_RemapFrostedGlassCommandlet()
		: red::ICommandlet(RED_NAME("mesh_remapfrostedglass"))
		, m_editorDepot(commandlet::GetEditorDepot())
	{}

	const char* Mesh_RemapFrostedGlassCommandlet::GetOneLiner() const
	{
		return "Reimport all the materials in the depot";
	}

	void Mesh_RemapFrostedGlassCommandlet::PrintHelp() const
	{
		RED_LOG("Usage:");
		RED_LOG("  mesh_remapfrostedglass");
	}

	Bool Mesh_RemapFrostedGlassCommandlet::InitializeEnvironment()
	{
		commandlet::BackendDepotHolder depotHolder;

		red::AbsolutePath resdbCachePath = depotHolder.GetCachePath().AddFilePath(resdb::Database::GetDefaultFilename());
		m_database.Initialise(*depotHolder.GetDepot(), resdbCachePath);

		m_frostedGlassMaterialPath = res::ResourcePath::Build("base\\materials\\frosted_glass.mt");
		res::IssueLoadingRequestParameter loadingRequestParameter;
		loadingRequestParameter.path = m_frostedGlassMaterialPath;
		loadingRequestParameter.skipBuffers = true;
		loadingRequestParameter.skipPostLoad = true;
		loadingRequestParameter.postLoadFlags.skipUploadRenderData = true;

		m_frostedGlassMaterial = Cast< CMaterialTemplate >(GResourceLoader->IssueLoadingRequest(loadingRequestParameter)->WaitUntilLoaded());
		if (!m_frostedGlassMaterial)
		{
			RED_LOG_ERROR("FrostedGlass Remapper: Failed to load '%hs'", m_frostedGlassMaterialPath.ToDebugString());
			return false;
		}

		m_glassMaterialPath = res::ResourcePath::Build("base\\materials\\glass.mt");
		loadingRequestParameter.path = m_glassMaterialPath;

		m_glassMaterial = Cast< CMaterialTemplate >(GResourceLoader->IssueLoadingRequest(loadingRequestParameter)->WaitUntilLoaded());
		if (!m_glassMaterial)
		{
			RED_LOG_ERROR("FrostedGlass Remapper: Failed to load '%hs'", m_glassMaterialPath.ToDebugString());
			return false;
		}

		return m_engineInstance.Initialise() && InitializeMaterialLibrary();
	}

	void Mesh_RemapFrostedGlassCommandlet::TeardownEnvironment()
	{
		ShutdownMaterialLibrary();
		m_engineInstance.Shutdown();
	}

	Bool Mesh_RemapFrostedGlassCommandlet::ParseParams(const red::CommandLine& options)
	{
		return true;
	}

	Float Mesh_RemapTo01(Float val, Float rangeMin, Float rangeMax)
	{
		return math::Clamp(((val - rangeMin) / (rangeMax - rangeMin)), 0.f, 1.f);
	}

	template < typename T >
	Bool GetParam(const THandle< IMaterial >& material, const THandle< IMaterial >& baseTemplate, const CName paramName, T& outValue)
	{
		IMaterial* pMaterial = material.Get();
		while (pMaterial && pMaterial->IsA< CMaterialInstance >())
		{
			if (pMaterial->ReadParameter_NoBase(paramName, outValue))
			{
				return true;
			}
			pMaterial = pMaterial->GetBaseMaterial();
		}

		// did not find anything in overriden instances, take it from the base
		return baseTemplate->ReadParameter(paramName, outValue);
	}

	void Mesh_RemapFrostedGlassCommandlet::RemapScalar(const red::HashMap< CName, Float >& params, const CName paramName, const THandle< CMaterialInstance >& matInstance, const THandle< IMaterial >& baseMaterial)
	{
		Float scalarValue;
		RED_VERIFY(GetParam(baseMaterial, m_glassMaterial, paramName, scalarValue));

		if (paramName == RED_NAME_CONSTEXPR_NOREG("GlassRoughnessBias")) // GlassRoughnessBias = clamp( RoughnessBase, -1.f, 1.f );
		{
			if (params.KeyExist(RED_NAME_CONSTEXPR_NOREG("RoughnessBase")))
			{
				Float unmapped = params[RED_NAME_CONSTEXPR("RoughnessBase")];
				const Float f = Clamp(unmapped, -1.f, 1.f);
				if (f != scalarValue)
				{
					RED_LOG_INFO("FrostedGlass Remapper:     '%hs' RoughnessBase[%1.3f] -> GlassRoughnessBias[%1.3f] (original: %1.3f)", matInstance->GetPath().ToDebugString(), unmapped, f, scalarValue);
					matInstance->WriteParameter_NoDefinitionCheck(paramName, f, false);
				}
			}
		}
		else if (paramName == RED_NAME_CONSTEXPR_NOREG("MaskRoughnessBias")) // MaskRoughnessBias = lerp(-1.f, 0.f, RoughnessAttenuation);
		{
			if (params.KeyExist(RED_NAME_CONSTEXPR_NOREG("RoughnessAttenuation")))
			{
				const Float unmapped = params[RED_NAME_CONSTEXPR("RoughnessAttenuation")];
				const Float f = math::Lerp(unmapped, -1.f, 0.f);
				if (f != scalarValue)
				{
					RED_LOG_INFO("FrostedGlass Remapper:     '%hs' RoughnessAttenuation[%1.3f] -> MaskRoughnessBias[%1.3f] (original: %1.3f)", matInstance->GetPath().ToDebugString(), unmapped, f, scalarValue);
					matInstance->WriteParameter_NoDefinitionCheck(paramName, f, false);
				}
			}
		}
		else if (paramName == RED_NAME_CONSTEXPR_NOREG("BlurRadius")) // BlurRadius = RemapTo01(BlurRadius, 0.f, 5.f);
		{
			if (params.KeyExist(RED_NAME_CONSTEXPR_NOREG("BlurRadius")))
			{
				const Float unmapped = params[RED_NAME_CONSTEXPR("BlurRadius")];
				const Float f = Mesh_RemapTo01(unmapped, 0.f, 0.5f);
				if (f != scalarValue)
				{
					RED_LOG_INFO("FrostedGlass Remapper:     '%hs' BlurRadius[%1.3f] -> BlurRadius[%1.3f] (original: %1.3f)", matInstance->GetPath().ToDebugString(), unmapped, f, scalarValue);
					matInstance->WriteParameter_NoDefinitionCheck(paramName, f, false);
				}
			}
		}
		else if (paramName == RED_NAME_CONSTEXPR_NOREG("BlurByRoughness")) // BlurByRoughness = RemapTo01(BlurRoughness, 0.f, 4.f);
		{
			if (params.KeyExist(RED_NAME_CONSTEXPR_NOREG("BlurRoughness")))
			{
				const Float unmapped = params[RED_NAME_CONSTEXPR("BlurRoughness")];
				const Float f = Mesh_RemapTo01(unmapped, 0.f, 4.f);
				if (f != scalarValue)
				{
					RED_LOG_INFO("FrostedGlass Remapper:     '%hs' BlurRoughness[%1.3f] -> BlurByRoughness[%1.3f] (original: %1.3f)", matInstance->GetPath().ToDebugString(), unmapped, f, scalarValue);
					matInstance->WriteParameter_NoDefinitionCheck(paramName, f, false);
				}
			}
		}
		else if (paramName == RED_NAME_CONSTEXPR_NOREG("OpacityBackFace")) // OpacityBackFace = RenderBackFaces;
		{
			if (params.KeyExist(RED_NAME_CONSTEXPR_NOREG("RenderBackFaces")))
			{
				const Float unmapped = params[RED_NAME_CONSTEXPR("RenderBackFaces")];
				const Float f = unmapped;
				if (f != scalarValue)
				{
					RED_LOG_INFO("FrostedGlass Remapper:     '%hs' RenderBackFaces[%1.3f] -> OpacityBackFace[%1.3f] (original: %1.3f)", matInstance->GetPath().ToDebugString(), unmapped, f, scalarValue);
					matInstance->WriteParameter_NoDefinitionCheck(paramName, f, false);
				}
			}
		}
		else if (paramName == RED_NAME_CONSTEXPR_NOREG("TintFromVertexPaint")) // TintFromVertexPaint = 0.f;
		{
			if (scalarValue != 0.f)
			{
				Float unmapped = 0.f;
				Float f = 0.f;
				RED_LOG_INFO("FrostedGlass Remapper:     '%hs' TintFromVertexPaint[%1.3f] -> TintFromVertexPaint[%1.3f] (original: %1.3f)", matInstance->GetPath().ToDebugString(), unmapped, f, scalarValue);
				matInstance->WriteParameter_NoDefinitionCheck(paramName, 0.f, false);
			}
		}
		else if (paramName == RED_NAME_CONSTEXPR_NOREG("MaskOpacity")) // MaskOpacity = saturate( MaskOpacity + SurfaceOpacity );
		{
			Float oldMaskOpacity = 0.f;
			Float oldSurfaceOpacity = 0.f;
			Float f = 0.f;
			if (params.KeyExist(RED_NAME_CONSTEXPR_NOREG("MaskOpacity")))
			{
				oldMaskOpacity = params[RED_NAME_CONSTEXPR("MaskOpacity")];
				f += oldMaskOpacity;
			}
			if (params.KeyExist(RED_NAME_CONSTEXPR_NOREG("SurfaceOpacity")))
			{
				oldSurfaceOpacity = params[RED_NAME_CONSTEXPR("SurfaceOpacity")];
				f += oldSurfaceOpacity;
			}

			// saturate
			f = math::Clamp(f, 0.f, 1.f);
			if (f != scalarValue)
			{
				RED_LOG_INFO("FrostedGlass Remapper:     '%hs' (MaskOpacity[%1.3f] + SurfaceOpacity[%1.3f]) -> MaskOpacity[%1.3f] (original: %1.3f)", matInstance->GetPath().ToDebugString(), oldMaskOpacity, oldSurfaceOpacity, f, scalarValue);
				matInstance->WriteParameter_NoDefinitionCheck(paramName, f, false);
			}
		}
		else if (paramName == RED_NAME_CONSTEXPR_NOREG("Opacity")) // Opacity = 1.f;
		{
			if (scalarValue != 1.f)
			{
				Float unmapped = 1.f;
				Float f = 1.f;
				RED_LOG_INFO("FrostedGlass Remapper:     '%hs' Opacity[%1.3f] -> Opacity[%1.3f] (original: %1.3f)", matInstance->GetPath().ToDebugString(), unmapped, f, scalarValue);
				matInstance->WriteParameter_NoDefinitionCheck(paramName, f, false);
			}
		}
		else
		{
			if (params.KeyExist(paramName))
			{
				const Float unmapped = params[paramName];
				const Float f = params[paramName];
				if (scalarValue != f)
				{
					RED_LOG_INFO("FrostedGlass Remapper:     '%hs' %hs[%1.3f] -> %hs[%1.3f] (original: %1.3f)", matInstance->GetPath().ToDebugString(), paramName.AsChar(), unmapped, paramName.AsChar(), f, scalarValue);
					matInstance->WriteParameter_NoDefinitionCheck(paramName, f, false);
				}
			}
		}
	}

	void Mesh_RemapFrostedGlassCommandlet::RemapColor(const red::HashMap< CName, Color >& params, const CName paramName, const THandle< CMaterialInstance >& matInstance, const THandle< IMaterial >& baseMaterial)
	{
		Color colorValue;
		RED_VERIFY(GetParam(baseMaterial, m_glassMaterial, paramName, colorValue));
		if (paramName == RED_NAME_CONSTEXPR_NOREG("TintColor"))
		{
			if (params.KeyExist(RED_NAME_CONSTEXPR_NOREG("ColorMultiplier")))
			{
				const Color& c = params[RED_NAME_CONSTEXPR("ColorMultiplier")];
				if (colorValue != c)
				{
					String s;
					ToString(s, c);
					String originalS;
					ToString(originalS, colorValue);
					RED_LOG_INFO("FrostedGlass Remapper:     '%hs' %hs[%hs] -> %hs[%hs] (original: %hs)", matInstance->GetPath().ToDebugString(), paramName.AsChar(), s.AsChar(), paramName.AsChar(), s.AsChar(), originalS.AsChar());
					matInstance->WriteParameter_NoDefinitionCheck(paramName, c, false);
				}
			}
		}
		else
		{
			if (params.KeyExist(paramName))
			{
				const Color& c = params[paramName];
				if (colorValue != c)
				{
					String s;
					ToString(s, c);
					String originalS;
					ToString(originalS, colorValue);
					RED_LOG_INFO("FrostedGlass Remapper:     '%hs' %hs[%hs] -> %hs[%hs] (original: %hs)", matInstance->GetPath().ToDebugString(), paramName.AsChar(), s.AsChar(), paramName.AsChar(), s.AsChar(), originalS.AsChar());
					matInstance->WriteParameter_NoDefinitionCheck(paramName, c, false);
				}
			}
		}
	}

	void Mesh_RemapFrostedGlassCommandlet::RemapVector(const red::HashMap< CName, Vector4 >& params, const CName paramName, const THandle< CMaterialInstance >& matInstance, const THandle< IMaterial >& baseMaterial)
	{
		Vector4 vectorValue;
		RED_VERIFY(GetParam(baseMaterial, m_glassMaterial, paramName, vectorValue));
		if (params.KeyExist(paramName))
		{
			const Vector4& v = params[paramName];
			if (vectorValue != v)
			{
				String s;
				ToString(s, v);
				String originalS;
				ToString(originalS, vectorValue);
				RED_LOG_INFO("FrostedGlass Remapper:     '%hs' %hs[%hs] -> %hs[%hs] (original: %hs)", matInstance->GetPath().ToDebugString(), paramName.AsChar(), s.AsChar(), paramName.AsChar(), s.AsChar(), originalS.AsChar());
				matInstance->WriteParameter_NoDefinitionCheck(paramName, v, false);
			}
		}
	}

	void Mesh_RemapFrostedGlassCommandlet::RemapTexture(const red::HashMap< CName, TResRef< ITexture > >& params, const CName paramName, const THandle< CMaterialInstance >& matInstance, const THandle< IMaterial >& baseMaterial)
	{
		TResRef< ITexture > textureValue;
		RED_VERIFY(GetParam(baseMaterial, m_glassMaterial, paramName, textureValue));
		if (paramName == RED_NAME_CONSTEXPR_NOREG("MaskTexture"))
		{
			if (params.KeyExist(RED_NAME_CONSTEXPR_NOREG("Diffuse")))
			{
				const TResRef< ITexture >& t = params[RED_NAME_CONSTEXPR("Diffuse")];
				if (textureValue.GetPath() != t.GetPath())
				{
					RED_LOG_INFO("FrostedGlass Remapper:     '%hs' Diffuse[%hs] -> MaskTexture[%hs] (original: %hs)", matInstance->GetPath().ToDebugString(), t.GetPath().ToDebugString(), t.GetPath().ToDebugString(), textureValue.GetPath().ToDebugString());
					matInstance->WriteParameter_NoDefinitionCheck(paramName, t, false);
				}
			}
		}
		else
		{
			if (params.KeyExist(paramName))
			{
				const TResRef< ITexture >& t = params[paramName];
				if (textureValue.GetPath() != t.GetPath())
				{
					RED_LOG_INFO("FrostedGlass Remapper:     '%hs' Diffuse[%hs] -> MaskTexture[%hs] (original: %hs)", matInstance->GetPath().ToDebugString(), t.GetPath().ToDebugString(), t.GetPath().ToDebugString(), textureValue.GetPath().ToDebugString());
					matInstance->WriteParameter_NoDefinitionCheck(paramName, t, false);
				}
			}
		}
	}

	Bool Mesh_RemapFrostedGlassCommandlet::ProcessMaterialInstance(const THandle< CMaterialInstance >& matInstance, const THandle< IMaterial >& baseMaterial)
	{
		red::HashMap< CName, Float > scalarParams;
		red::HashMap< CName, Vector4 > vectorParams;
		red::HashMap< CName, Color > colorParams;
		red::HashMap< CName, TResRef< ITexture > > texParams;

		const auto& fg_parameterDefinitions = m_frostedGlassMaterial->GetParameterDefinitions(MSH_PixelShader);
		Float scalarValue;
		TResRef< ITexture > textureValue;
		Color colorValue;
		Vector4 vectorValue;
		for (const IMaterialDataProvider::ParameterDefinition& paramDef : fg_parameterDefinitions)
		{
			switch (paramDef.m_type)
			{
			case IMaterialDataProviderDesc::PT_Scalar:	if (GetParam(matInstance, m_frostedGlassMaterial, paramDef.m_name, scalarValue))	scalarParams.Insert(paramDef.m_name, scalarValue);	break;
			case IMaterialDataProviderDesc::PT_Texture:	if (GetParam(matInstance, m_frostedGlassMaterial, paramDef.m_name, textureValue))	texParams.Insert(paramDef.m_name, textureValue);		break;
			case IMaterialDataProviderDesc::PT_Color:	if (GetParam(matInstance, m_frostedGlassMaterial, paramDef.m_name, colorValue))		colorParams.Insert(paramDef.m_name, colorValue);		break;
			case IMaterialDataProviderDesc::PT_Vector:	if (GetParam(matInstance, m_frostedGlassMaterial, paramDef.m_name, vectorValue))	vectorParams.Insert(paramDef.m_name, vectorValue);	break;
			}
		}

		matInstance->ClearInstanceParameters();

		const auto& parameterDefinitions = m_glassMaterial->GetParameterDefinitions(MSH_PixelShader);
		for (const IMaterialDataProvider::ParameterDefinition& paramDef : parameterDefinitions)
		{
			switch (paramDef.m_type)
			{
			case IMaterialDataProviderDesc::PT_Scalar:	RemapScalar(scalarParams, paramDef.m_name, matInstance, baseMaterial);	break;
			case IMaterialDataProviderDesc::PT_Texture:	RemapTexture(texParams, paramDef.m_name, matInstance, baseMaterial);		break;
			case IMaterialDataProviderDesc::PT_Color:	RemapColor(colorParams, paramDef.m_name, matInstance, baseMaterial);		break;
			case IMaterialDataProviderDesc::PT_Vector:	RemapVector(vectorParams, paramDef.m_name, matInstance, baseMaterial);	break;
			}
		}

		return true;
	}

	Bool Mesh_RemapFrostedGlassCommandlet::HandleMaterialInstance(const THandle< CMaterialInstance >& resource, const THandle< IMaterial >& baseMaterial)
	{
		ProcessMaterialInstance(resource, baseMaterial );
		HandleResource(resource->GetPath(), baseMaterial );
		resource->SetBaseMaterial( baseMaterial );
		return true;
	}

	Bool Mesh_RemapFrostedGlassCommandlet::HandleMesh(const THandle< CMesh >& resource, const THandle< IMaterial >& baseMaterial)
	{
		RED_LOG_INFO("FrostedGlass Remapper: Processing mesh '%hs'", resource->GetPath().ToDebugString() );

		IMaterialDefinition* frostedGlassDefinition = m_frostedGlassMaterial->GetMaterialDefinition();

		mesh::DataView view(*resource.Get());

		mesh::MaterialIndex numMaterials = view.GetNumMaterials();
		for (Uint32 i = 0; i < numMaterials;++i)
		{
			THandle< IMaterial > material = view.GetMaterialData(i);
			// process only those materials that are based on frosted_glass
			if (material->GetMaterialDefinition() != frostedGlassDefinition)
			{
				continue;
			}

			THandle< CMaterialInstance > materialInstance = nullptr;
			if ( material->IsA< CMaterialInstance >() )
			{
				materialInstance = Cast< CMaterialInstance >( material );
			}
			else if ( material->IsA< CMaterialTemplate >() )
			{
				// mesh uses frosted_glass.mt directly as material for one of its chunks
				RED_LOG_INFO( "FrostedGlass Remapper: mesh '%hs' is using frosted_glass.mt directly in chunk%d, creating local instance of glass.mt", resource->GetPath().ToDebugString(), i );
				materialInstance = CreateHandle< CMaterialInstance >( resource.Get(), m_frostedGlassMaterial );
				view.SetMaterialData( i, materialInstance );
				materialInstance->AutoAssignTextures();
				view.Apply();
			}

			RED_LOG_INFO("FrostedGlass Remapper: Processing material '%hs' '%hs' with base '%hs'", view.GetMaterialName(i).AsChar(), materialInstance->GetPath().ToDebugString(), materialInstance->GetBaseMaterialPath().ToDebugString());
			ProcessMaterialInstance( materialInstance, baseMaterial );

			if ( !view.IsMaterialLocalInstance(i) )
			{
				// this is not local instance, so it references a file, handle it
				HandleResource( material->GetPath(), material );
			}

			materialInstance->SetBaseMaterial(baseMaterial);
		}
		return true;
	}

	Bool Mesh_RemapFrostedGlassCommandlet::HandleResource(const res::ResourcePath& resourcePath, const THandle< IMaterial >& baseMaterial)
	{
		if (!resourcePath.IsValid() || resourcePath.IsEmpty())
		{
			RED_LOG_ERROR("FrostedGlass remapper: Invalid path: '%hs'", resourcePath.ToDebugString());
			m_hasErrors = true;
			return false;
		}

		if ( m_processedResources.Exist(resourcePath))
		{
			RED_LOG_INFO("FrostedGlass remapper: Resource already processed '%hs'", resourcePath.ToDebugString() );
			return true;
		}

		m_processedResources.Insert( resourcePath );

		resdb::Links backwardLinks = m_database.GetBackwardLinks(resourcePath);
		for (const resdb::Link& link : backwardLinks)
		{
			res::IssueLoadingRequestParameter loadingRequestParameter;
			loadingRequestParameter.path = link.from;
			loadingRequestParameter.skipBuffers = true;
			loadingRequestParameter.skipPostLoad = false;
			loadingRequestParameter.postLoadFlags.skipUploadRenderData = true;

			const THandle< CResource > resource = GResourceLoader->IssueLoadingRequest(loadingRequestParameter)->WaitUntilLoaded();
			if (!resource)
			{
				RED_LOG_ERROR("FrostedGlass remapper: Failed to load '%hs'", link.from.ToDebugString());
				m_hasErrors = true;
				continue;
			}

			resource->MarkModified();

			Bool shouldSave = false;

			const red::StringView parentFileExtension = red::paths::GetExtension(link.from.ToStringView());
			if (parentFileExtension == CMaterialInstance::GetFileExtension() || parentFileExtension == CMaterialInstance::GetDeprecatedFileExtension())
			{
				const THandle< CMaterialInstance > matInstance = Cast< CMaterialInstance >(resource);
				if (!matInstance)
				{
					RED_LOG_ERROR("FrostedGlass remapper: Failed to load '%hs'", link.from.ToDebugString());
					m_hasErrors = true;
					return false;
				}
				HandleMaterialInstance(matInstance, baseMaterial);
				shouldSave = true;
			}
			else if (parentFileExtension == CMesh::GetFileExtension())
			{
				const THandle< CMesh > mesh = Cast< CMesh >(resource);
				if (!mesh)
				{
					RED_LOG_ERROR("FrostedGlass remapper: Failed to load '%hs'", link.from.ToDebugString());
					m_hasErrors = true;
					return false;
				}
				HandleMesh(mesh, baseMaterial);
				shouldSave = true;
			}

			if (shouldSave)
			{
				red::AbsolutePath resourceAbsolutePath = m_editorDepot->TranslateResourcePath(resource->GetPath());
				GFileManager->SetFileReadOnly(resourceAbsolutePath, false);

				res::ResourceSavingOptions options;
				options.m_depot = m_editorDepot;
				if (res::SaveResource(resource, options))
				{
					RED_LOG_INFO("FrostedGlass remapper: Saved '%hs'", resource->GetPath().ToDebugString());
				}
				else
				{
					RED_LOG_ERROR("FrostedGlass remapper: Could not save '%hs'", resource->GetPath().ToDebugString());
					m_hasErrors = true;
				}
			}			
		}

		return true;
	}

	Bool Mesh_RemapFrostedGlassCommandlet::Execute(const red::CommandLine& options)
	{
		if (!ParseParams(options))
		{
			return false;
		}

		HandleResource(m_frostedGlassMaterialPath, m_glassMaterial);

		return !m_hasErrors;
	}
}

RTTI_BEGIN_TYPE_IN_NAMESPACE( GatherMeshDataCommandlet, tools )
RTTI_PARENT_TYPE( red::ICommandlet );
RTTI_END_TYPE()

RTTI_BEGIN_TYPE_IN_NAMESPACE( Mesh_RemapFrostedGlassCommandlet, tools )
RTTI_PARENT_TYPE(red::ICommandlet)
RTTI_END_TYPE()