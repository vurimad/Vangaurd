/**
* Copyright (c) 2017 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"
#include "meshReport.h"

#include "../../backendEngine/include/backendEnginePublic.h"
#include "../../backendEngine/include/resourceMetadata.h"
#include "../../backendEngine/include/resourceImportMetadata.h"

#include "../../../common/core/include/message.h"
#include "../../../common/redFileSystem/include/fileSys.h"
#include "../../../common/redReflection/include/gatheredResource.h"
#include "../../../common/redReflection/include/resourceUtils.h"
#include "../../../common/redContainers/include/string/stringBuilder.h"
#include "../../../common/resource/include/jsonSerializer.h"
#include "../../../common/resourceMesh/include/mesh.h"
#include "../../../common/resourceMesh/include/meshDataViewRO.h"
#include "../../../common/resourceMesh/include/meshAppearance.h"
#include "../../../common/resourceMesh/include/meshParamPhysics.h"
#include "../../../common/resourceMesh/include/meshUtils.h"
#include "../../../common/resourceMaterial/include/material.h"
#include "../../../common/resourceMaterial/include/materialDefinition.h"
#include "../../../common/resourceMaterial/include/texture.h"
#include "../../../common/resourceMaterial/include/bitmapTexture.h"

#include "../../../common/renderData/include/renderResource.h"

#include "../../../common/physics/include/physicsSystemResource.h"
#include "../../../common/physics/include/physicsSystemResourceValidator.h"
#include "../../../common/physics/include/physicsSystemBody.h"

#include "../../../common/generalEditorBackend/include/meshEditorInterop.h"

using red::DynArray;
using RapidJsonDocument = rapidjson::GenericDocument< rapidjson::UTF8< red::AnsiChar >, rapidjson::MemoryPoolAllocator< res::json::JSONAllocator >, res::json::JSONAllocator >;
using RapidJsonValue = rapidjson::GenericValue< rapidjson::UTF8< red::AnsiChar >, rapidjson::MemoryPoolAllocator< res::json::JSONAllocator > >;

// These are values from W3. We probably need adjustments for CP2077 here further down the development road...
#define MAX_REASONABLE_MESH_TRIANGLES			45000
#define MAX_REASONABLE_SKINNED_MESH_TRIANGLES	20000
#define MAX_ALLOWED_TRIANGLES_WITHOUT_LOD		1000
#define MAX_TEXTURES_DATA_ALLOWED				20*1024*1024
#define MAX_BOUNDING_BOX_EDGE_LENGHT			50.0f

namespace
{
	/////////////////////////////////////////////////////////////////////////////////////////////////
	String MeshVertexTypeToName( EMeshVertexType vt )
	{
		switch ( vt )
		{
			case MVT_StaticMesh:						return "MVT_StaticMesh";
			case MVT_DestructibleMesh:					return "MVT_DestructibleMesh";
			case MVT_DestructibleMeshSkinned:			return "MVT_DestructibleMeshSkinned";
			case MVT_ProceduralMesh:					return "MVT_ProceduralMesh";
			case MVT_SkinnedMesh:						return "MVT_SkinnedMesh";
			case MVT_ExtSkinnedMesh:					return "MVT_ExtSkinnedMesh";
			case MVT_StaticMeshVehicle:					return "MVT_StaticMeshVehicle";
			case MVT_SkinnedMeshVehicle:				return "MVT_SkinnedMeshVehicle";
			case MVT_GarmentSkinnedMesh:				return "MVT_GarmentSkinnedMesh";
			case MVT_ExtGarmentSkinnedMesh:				return "MVT_ExtGarmentSkinnedMesh";
			case MVT_SpeedTreeMesh:						return "MVT_SpeedTreeMesh";
			case MVT_SkinnedMeshLightBlocker:			return "MVT_SkinnedMeshLightBlocker";
			case MVT_ExtSkinnedMeshLightBlocker:		return "MVT_ExtSkinnedMeshLightBlocker";
			case MVT_GarmentSkinnedMeshLightBlocker:	return "MVT_GarmentSkinnedMeshLightBlocker";
			case MVT_ExtGarmentSkinnedMeshLightBlocker:	return "MVT_ExtGarmentSkinnedMeshLightBlocker";
			case MVT_SkinnedMeshSingleBone:				return "MVT_SkinnedMeshSingleBone";
		}
		return "Unknown";
	}

	//--------------------------------------------------------------------------------------------
	CName GetTextureGroupName( const ITexture* texture )
	{
		const STextureGroupSetup& groupSetup = texture->GetTextureSetup();
		const AnsiChar* groupName = GpuWrapApi::GetTextureGroupName( groupSetup.m_group );
		return RED_NAME( groupName );
	}

	//--------------------------------------------------------------------------------------------
	String ExtractTextureThumbnail( ITexture* texture )
	{
		if ( !texture || !texture->HasValidPath() )
		{
			return String::EMPTY();
		}

		res::ResourcePath path = texture->GetPath();
		red::AbsolutePath thumbnailPath = res::GetThumbnailPath( path, CName::NONE() );
		return thumbnailPath.AsString();
	}

	/////////////////////////////////////////////////////////////////////////////////////////////////
	// Tag helper for HTML string printing.
	class CHTMLTag
	{
	public:
		String& m_string;
		String m_tag;

	public:
		CHTMLTag( String& str, const String& tag )
			: m_string( str )
			, m_tag( tag )
		{
			m_string += "<";
			m_string += m_tag;
			m_string += ">";
		}

		~CHTMLTag()
		{
			m_string += "</";
			m_string += m_tag;
			m_string += ">";
		}
	};
}

namespace tools
{
	static const char* s_meshScoreConfigPath = "base\\mesh_score_limits.json";

	//////////////////////////////////////////////////////////////////////////////////////////////
	struct MeshReport::ProblemList
	{
		String m_problemsCode;
		String m_warningCode;

		void EmitProblem( const AnsiChar* format, ... )
		{
			// Format
			va_list arglist;
			va_start( arglist, format );
			AnsiChar formattedBuf[4096];
			red::VSNPrintF( formattedBuf, RED_ARRAY_COUNT( formattedBuf ), format, arglist );

			// Emit
			m_problemsCode += "<font color=\"#C00000\"><b>";
			m_problemsCode += formattedBuf;
			m_problemsCode += "</b></font><br>";
		}

		void EmitWarning( const AnsiChar* format, ... )
		{
			// Format
			va_list arglist;
			va_start( arglist, format );
			AnsiChar formattedBuf[4096];
			red::VSNPrintF( formattedBuf, RED_ARRAY_COUNT( formattedBuf ), format, arglist );

			// Emit
			m_warningCode += "<font color=\"#C0C000\"><b>";
			m_warningCode += formattedBuf;
			m_warningCode += "</b></font><br>";
		}
	};

	//////////////////////////////////////////////////////////////////////////////////////////////
	struct MeshReport::MeshTextureInfo
	{
		ITexture*				m_texture;
		String					m_thumbnailFile;
		Uint32					m_dataSize;
		DynArray< Uint32 >		m_usedByMaterials;
		DynArray< Uint32 >		m_isUsedByMeshes;
		Uint32					m_refCount;
		Uint32					m_anchorCounter;
		Uint32 m_width;
		Uint32 m_height;
		Uint32 m_mipMapCount;

		MeshTextureInfo( ITexture* texture )
			: m_texture( texture )
			, m_dataSize( MeshReport::CalcTextureDataSize( texture ) )
			, m_usedByMaterials( red::PoolBackend() )
			, m_isUsedByMeshes( red::PoolBackend() )
			, m_refCount( 1 )
		{
			if ( m_texture )
			{
				auto sizeDesc = m_texture->GetSizeDesc();

				m_width			= sizeDesc.width;
				m_height		= sizeDesc.height;
				m_mipMapCount	= sizeDesc.numMips;
			}
			else
			{
				m_width = m_height = m_mipMapCount = 0;
			}
		}

		static Bool CmpFuncByDataSize( const MeshTextureInfo* elem0, const MeshTextureInfo* elem1 )
		{
			return elem0->m_dataSize < elem1->m_dataSize;
		}
	};

	//////////////////////////////////////////////////////////////////////////////////////////////
	struct MeshReport::MeshTextureGroupInfo
	{
		CName	m_name;
		Uint32	m_dataSize;
		Uint32	m_numTextures;

		MeshTextureGroupInfo( const CName& name ) : m_name( name ), m_dataSize( 0 ), m_numTextures( 0 ) {}

		static Bool CmpFumcByDataSize( const MeshTextureGroupInfo& elem0, const MeshTextureGroupInfo& elem1 )
		{
			return elem0.m_dataSize < elem1.m_dataSize;
		}
	};

	//////////////////////////////////////////////////////////////////////////////////////////////
	struct MeshReport::MeshScoreRange
	{
		// 0: light, 1: median, 2: heavy, 3: extreme
		Float	m_values[ 4 ];
	};

	//////////////////////////////////////////////////////////////////////////////////////////////
	MeshReport::MeshReport( const THandle< CMesh >& mesh, Bool updateStats )
		: m_importPath( String::EMPTY() )
	{
		m_reportInfo = red::CreateUniquePtr< MeshEditorReportInfo >();

		if( mesh )
			SetMesh( mesh, updateStats );
	}

	MeshReport::~MeshReport() = default;

	void MeshReport::SetMesh( const THandle< CMesh >& newMesh, Bool updateStats )
	{
		m_mesh = newMesh;
		if ( updateStats )
		{
			UpdateStats();
		}
		UpdateReportInfo();
	}

	//--------------------------------------------------------------------------------------------
	bool MeshReport::LoadMeshScoreConfig( MeshScoreRange& scoreRangeBones, MeshScoreRange& scoreRangeChunks, MeshScoreRange& scoreRangeTrisToAreaLog, MeshScoreRange& scoreRangeColliderCount, MeshScoreRange& scoreRangeColliderMemory, const res::ResourcePath meshPath )
	{
		auto configPath = GFileManager->GetGameRoot();
		configPath.AppendDirPath( "depot" );
		configPath.AppendFilePath( s_meshScoreConfigPath );

		const res::ResourcePath errorLocation = meshPath;
		const CName configPathAsName = RED_NAME( configPath.ToDebugString() );
		const red::StringView meshFolder = red::paths::GetParentPath( meshPath.ToStringView() );

		// Fallback values
		scoreRangeBones = { { 0.0f, 8.0f, 64.0f, 128.0f } }; // Number of bones in LOD0
		scoreRangeChunks = { { 1.0f, 2.0f, 5.0f, 10.0f } }; // Number of chunks in LOD0
		scoreRangeTrisToAreaLog = { { 0.0f, 1.7f, 4.2f, 6.0f } }; // Log10 of: triangle count divided by the surface area (m2), in LOD0
		scoreRangeColliderCount = { { 0.0f, 1.0f, 2.0f, 3.0f } }; // Number of collision shapes
		scoreRangeColliderMemory = { { 0.0f, 0.0f, 0.0f, 0.0f } }; // Total memory used by the collision meshes, in bytes

		red::String jsonContent;
		RapidJsonDocument doc;

		Bool foundBestMatch = false;
		Bool fileLoaded = red::LoadFileToString( configPath, jsonContent );

		if ( !fileLoaded )
		{
			return false;
		}
		
		doc.Parse( jsonContent.AsChar(), jsonContent.Length() );
		if ( doc.HasParseError() )
		{
			return false;
		}

		String folderPathSoFar = "";

		auto& folderInfo = doc.GetArray()[ 0 ]; // Root folder in the config file, which is a parent to "base" and "user"

		while ( !foundBestMatch )
		{
			Bool isValid = (
				folderInfo.HasMember( "numBones" ) &&
				folderInfo.HasMember( "numChunks" ) &&
				folderInfo.HasMember( "numTrisToAreaLog" ) &&
				folderInfo.HasMember( "numCollisionShapes" ) &&
				folderInfo.HasMember( "collisionMeshMemory" ) &&
				folderInfo.HasMember( "limits" )
			);
			if ( !isValid )
			{
				RED_REPORT_ERROR( RED_LOCATION( errorLocation ), "Mesh Report: Failed to parse \"%\". Missing properties in \"%\"", configPathAsName, RED_NAME( folderPathSoFar ) );
				return false;
			}

			auto& arrNumBones = folderInfo[ "numBones" ].GetArray();
			auto& arrNumChunks = folderInfo[ "numChunks" ].GetArray();
			auto& arrNumTris = folderInfo[ "numTrisToAreaLog" ].GetArray();
			auto& arrColliderCount = folderInfo[ "numCollisionShapes" ].GetArray();
			auto& arrColliderMemory = folderInfo[ "collisionMeshMemory" ].GetArray();

			isValid &= (
				arrNumBones.Size() == 4 &&
				arrNumChunks.Size() == 4 &&
				arrNumTris.Size() == 4 &&
				arrColliderCount.Size() == 4 &&
				arrColliderMemory.Size() == 4
			);
			if ( !isValid )
			{
				RED_REPORT_ERROR( RED_LOCATION( errorLocation ), "Mesh Report: Failed to parse \"%\". Incorrect array length in \"%\"", configPathAsName, RED_NAME( folderPathSoFar ) );
				return false;
			}

			for ( Uint8 i = 0u; i < 4u; ++i )
			{
				scoreRangeBones.m_values[ i ] = arrNumBones[ i ].GetFloat();
				scoreRangeChunks.m_values[ i ] = arrNumChunks[ i ].GetFloat();
				scoreRangeTrisToAreaLog.m_values[ i ] = arrNumTris[ i ].GetFloat();
				scoreRangeColliderCount.m_values[ i ] = arrColliderCount[ i ].GetFloat();
				scoreRangeColliderMemory.m_values[ i ] = arrColliderMemory[ i ].GetFloat();
			}

			if ( folderInfo.Size() == 0 )
			{
				// The folder doesn't have more sub-folders in the config file
				foundBestMatch = true;
				break;
			}

			String subfolderName = "";
			for each ( auto& subFolderInfo in folderInfo[ "limits" ].GetArray() )
			{
				const String folderName = String( subFolderInfo[ "folder" ].GetString() );
				String currentPath;
				if ( folderPathSoFar != "" )
				{
					currentPath = folderPathSoFar + "\\" + folderName;
				}
				else
				{
					currentPath = folderName;
				}

				if ( meshFolder.StartsWithIgnoreCase( currentPath ) )
				{
					// Found a matching sub-folder. Continue searching through its sub-folders
					subfolderName = folderName;
					folderPathSoFar = currentPath;
					folderInfo = subFolderInfo;
					break;
				}
			}

			if ( subfolderName == "" )
			{
				// No sub-folders matched the mesh's path
				foundBestMatch = true;
				break;
			}
		}

		if ( !foundBestMatch )
		{
			return false;
		}

		return true;  // Success
	}

	//--------------------------------------------------------------------------------------------
	void MeshReport::GatherTexturesUsedByMaterial( IMaterial* material, Uint32 materialIndex, DynArray< MeshTextureInfo* >& textures )
	{
		if ( material )
		{
			// Get definition for this material 
			IMaterialDefinition* def = material->GetMaterialDefinition();
			if ( def )
			{
				// Scan for texture parameters
				const IMaterialDefinition::TParameterDefinitionArray& params = def->GetParameterDefinitions( MSH_PixelShader );
				for ( Uint32 i = 0; i < params.Size(); i++ )
				{
					const IMaterialDefinition::ParameterDefinition& param = params[i];
					if ( param.m_type == IMaterialDataProviderDesc::PT_Texture )
					{
						// Get value ( the bounded texture )
						TResRef< ITexture > texture;
						material->ReadParameter( param.m_name, texture );

						// Add to list
						if ( texture.IsValid() )
						{
							// Find existing slot
							Bool added = false;
							for ( Uint32 j = 0; j < textures.Size(); j++ )
							{
								MeshTextureInfo* texInfo = textures[j];
								if ( texInfo->m_texture == texture.Get().Get() )
								{
									texInfo->m_usedByMaterials.PushBack( materialIndex );
									added = true;
									break;
								}
							}

							// Add new info struct
							if ( !added )
							{
								MeshTextureInfo* info = new MeshTextureInfo( texture.Get().Get() );
								info->m_usedByMaterials.PushBack( materialIndex );
								textures.PushBack( info );
							}
						}
					}
				}
			}
		}
	}

	void MeshReport::GatherTexturesUsedByMesh( const CMesh* mesh, DynArray< MeshTextureInfo* >& textures )
	{
		mesh::DataViewRO meshData( *mesh );
		const red::DynArray< THandle< IMaterial > >& materials = meshData.GetMaterials();
		for ( Uint32 i = 0; i < materials.Size(); i++ )
		{
			GatherTexturesUsedByMaterial( materials[i].Get(), i, textures );
		}
	}

	//--------------------------------------------------------------------------------------------
	void MeshReport::GatherMaterialsUsedByChunk( const mesh::DataViewRO& meshData, Uint32 chunkIndex, red::DynArray< Uint32 >& usedMaterials )
	{
		for ( Uint32 appearanceIndex = 0; appearanceIndex < meshData.GetNumAppearances(); appearanceIndex++ )
		{
			THandle< mesh::MeshAppearance > appearance = meshData.GetAppearanceData( appearanceIndex );
			if ( chunkIndex < appearance->GetNumChunks() )
			{
				const CName& materialName = appearance->GetChunkMaterialName( chunkIndex );
				Int32 materialIndex = meshData.GetMaterialIndex( materialName );
				if ( materialIndex >= 0 )
				{
					red::alg::PushBackUnique( usedMaterials, materialIndex );
				}
			}
		}
	}

	//--------------------------------------------------------------------------------------------
	Double MeshReport::CalcChunkArea( const CMesh* mesh, Uint32 chunkIndex )
	{
		Double area = 0.0;

		mesh::DataViewRO meshData( *mesh );
#ifdef MESH_TODO
		const auto chunks = data.GetChunks();

		const SMeshChunk& chunk = chunks[chunkIndex];
		for ( Uint32 i = 0; i < chunk.m_indices.Size(); i += 3 )
		{
			// Get the vertices
			const Vector v0 = chunk.m_vertices[chunk.m_indices[i + 0]].m_position;
			const Vector v1 = chunk.m_vertices[chunk.m_indices[i + 1]].m_position;
			const Vector v2 = chunk.m_vertices[chunk.m_indices[i + 2]].m_position;

			// Calculate edges
			const Double abx = v1.X - v0.X;
			const Double aby = v1.Y - v0.Y;
			const Double abz = v1.Z - v0.Z;
			const Double acx = v2.X - v0.X;
			const Double acy = v2.Y - v0.Y;
			const Double acz = v2.Z - v0.Z;

			// Calculate area
			Double abab = abx*abx + aby*aby + abz*abz;
			Double acac = acx*acx + acy*acy + acz*acz;
			Double abac = abx*acx + aby*acy + abz*acz;

			// Calculate area
			Double triArea = sqrt( abab * acac - abac * abac ) / 2.0;
			area += triArea;
		}
#endif

		// Return calculated area
		return area;
	}

	Double MeshReport::CalcChunkAverageTriangleArea( const CMesh* mesh, Uint32 chunkIndex )
	{
		Double area = 0.0;

		area = CalcChunkArea( mesh, chunkIndex );

#ifdef MESH_TODO
		const CMeshData data( mesh );
		const auto chunks = data.GetChunks();

		const SMeshChunk& chunk = chunks[chunkIndex];
		area /= chunk.m_indices.Size() / 3;
#else
		area = 0.1;
#endif

		// Return calculated area
		return area;
	}

	//--------------------------------------------------------------------------------------------
	String MeshReport::MemSizeToText( Uint32 memSize )
	{
		return String::Printf( "%1.4f", memSize / (1024.0f*1024.0f) );
	}

	String MeshReport::RawCountToText( Uint32 count )
	{
		// Get the number
		String txt = String::Printf( "%i", count );

		// Format string (NOT VERY OPTIMAL :P)
		String txtNew;
		for ( Int32 i = txt.Length() - 1; i >= 0; i-- )
		{
			// Extra spaces
			if ( txtNew.Length() == 3 ) txtNew = " " + txtNew;
			if ( txtNew.Length() == 7 ) txtNew = " " + txtNew;
			if ( txtNew.Length() == 11 ) txtNew = " " + txtNew;

			// Add char
			AnsiChar str[2] = { txt[i], 0 };
			txtNew = str + txtNew;
		}

		return txtNew;
	}

	//--------------------------------------------------------------------------------------------
	Bool MeshReport::IsMaterialUsedByChunk( const mesh::DataViewRO& meshData, Uint32 materialIndex, Uint32 chunkIndex )
	{
		/// @todo DP: what about current appearance?
		CName materialName = meshData.GetMaterialName( materialIndex );

		for ( Uint32 appearanceIndex = 0; appearanceIndex < meshData.GetNumAppearances(); appearanceIndex++ )
		{
			THandle< mesh::MeshAppearance > appearance = meshData.GetAppearanceData( appearanceIndex );
			if ( chunkIndex < appearance->GetNumChunks() )
			{
				const CName& chunkMaterialName = appearance->GetChunkMaterialName( chunkIndex );
				if ( chunkMaterialName == materialName )
				{
					return true;
				}
			}
		}

		return false;
	}

	//--------------------------------------------------------------------------------------------
	Uint32 MeshReport::CalcMeshRenderDataSize( const mesh::DataViewRO& meshData )
	{
		Uint32 rawDataSize = 0;
		return rawDataSize;															/// @todo DP: add proper code here
	}

	//--------------------------------------------------------------------------------------------
	Uint32 MeshReport::CalcMeshCollisionDataSize( const mesh::DataViewRO& meshData )
	{
		Uint32 rawDataSize = 0;
		return rawDataSize;															/// @todo DP: add proper code here
	}

	//--------------------------------------------------------------------------------------------
	Uint32 MeshReport::CalcMeshRawDataSize( const mesh::DataViewRO& meshData )
	{
		Uint32 rawDataSize = 0;

		// Mesh size
		rawDataSize += sizeof( CMesh );

		// Chunks size
#ifdef MESH_TODO
		const auto& chunks = mesh->GetChunks();

		rawDataSize += chunks.DataSize();

		// Size of materials
		const TDynArray< String >& materialNames = mesh->GetMaterialNames();
		rawDataSize += materialNames.DataSize();
		for ( Uint32 i = 0; i < materialNames.Size(); i++ )
		{
			rawDataSize += materialNames[i].DataSize();
		}

		// Materials
		const CMeshTypeResource::TMaterials& materials = mesh->GetMaterials();
		rawDataSize += materials.DataSize();
		for ( Uint32 i = 0; i < materials.Size(); i++ )
		{
			CMaterialInstance* matInstance = Cast< CMaterialInstance >( materials[i].Get() );
			if ( matInstance )
			{
				rawDataSize += sizeof( CMaterialInstance );
				const TMaterialParameters& params = matInstance->GetParameters();
				rawDataSize += params.DataSize();
				for ( Uint32 j = 0; j < params.Size(); j++ )
				{
					const MaterialParameterInstance& param = params[j];
					rawDataSize += sizeof( MaterialParameterInstance );
					rawDataSize += param.GetType()->GetSize();
				}
			}
		}

		// Bones
		rawDataSize += mesh->GetBoneNamesDataSize();
		rawDataSize += mesh->GetBoneRigMatricesDataSize();
		rawDataSize += mesh->GetBoneVertexEpsilonsDataSize();

		// LODs
		const CMesh::TLODLevelArray& lods = mesh->GetMeshLODLevels();
		rawDataSize += lods.DataSize();
		for ( Uint32 i = 0; i < lods.Size(); i++ )
		{
			const CMesh::LODLevel& lod = lods[i];
			rawDataSize += lod.m_chunks.DataSize();
		}
#endif
		// Return total size
		return rawDataSize;
	}

	//--------------------------------------------------------------------------------------------
	Uint32 MeshReport::CalcTextureDataSize( const ITexture* texture )
	{
		Uint32 rawData = 0;

		if ( texture->IsA< CBitmapTexture >() )
		{
			const CBitmapTexture* bitmapTexture = Cast<CBitmapTexture>( texture );

			return (bitmapTexture->GetRenderResource() != nullptr) ? bitmapTexture->GetRenderResource()->GetUsedVideoMemory() : 0;

#ifdef MESH_TODO
			const CBitmapTexture::MipArray& mips = texture->GetMips();
			for ( Uint32 i = 0; i < mips.Size(); i++ )
			{
				rawData += mips[i].m_data.GetSize();
			}
#endif
		}

		return rawData;
	}

	//--------------------------------------------------------------------------------------------
	Uint32 MeshReport::CalcMeshLodRenderDataSize( const mesh::DataViewRO& meshData, Uint32 lodIndex )
	{
		Uint32 rawDataSize = 0;

		// Chunks size
#ifdef MESH_TODO
		const auto& chunks = m_mesh->GetChunks();

		const auto& lod = mesh->GetMeshLODLevels()[lodIndex];
		for ( Uint32 i = 0; i < lod.m_chunks.Size(); i++ )
		{
			const Uint32 chunkIndex = lod.m_chunks[i];
			if ( chunkIndex < chunks.Size() )
			{
				rawDataSize += chunks[chunkIndex].m_numIndices * sizeof( Uint16 );
				rawDataSize += chunks[chunkIndex].m_numVertices * sizeof( SMeshVertex );
			}
		}
#endif

		return rawDataSize;
	}

	//--------------------------------------------------------------------------------------------
	void MeshReport::CalcChunkAreas( const mesh::DataViewRO& meshData, Double& totalArea, red::DynArray< Double >& chunkAreas, ProblemList& problemList )
	{
		// Calculate area of triangles in each chunk
		totalArea = 0.0;
		const Uint32 numChunks = meshData.GetNumChunks();
		for ( Uint32 i = 0; i < numChunks; i++ )
		{
			// Calculate area
			const Double area = CalcChunkArea( meshData.GetMesh(), i );

			Double averageArea = CalcChunkAverageTriangleArea( meshData.GetMesh(), i );
			if ( averageArea < 0.001 )
			{
				problemList.EmitProblem( "Too small triangles in chunk %u", i );
			}

			chunkAreas.PushBack( area );
			totalArea += area;
		}
	}

	//--------------------------------------------------------------------------------------------
	void MeshReport::AnalyzeTextures( const red::DynArray< MeshTextureInfo* >& usedTextures, Uint32& textureDataSize, ProblemList& problemList )
	{
		textureDataSize = 0;
		for ( Uint32 i = 0; i < usedTextures.Size(); i++ )
		{
			// Accumulate shit
			MeshTextureInfo* texInfo = usedTextures[i];
			textureDataSize += texInfo->m_dataSize;

			// Extract thumbnail
			texInfo->m_thumbnailFile = ExtractTextureThumbnail( texInfo->m_texture );
		}

		// To many textures
		if ( textureDataSize > MAX_TEXTURES_DATA_ALLOWED )
		{
			problemList.EmitProblem( "Too much texture memory used by mesh! Reduce textures!" );
		}
	}
	
	Uint32 MeshReport::GetNumberOfCollisionShapes( const mesh::DataViewRO& meshView )
	{
		Uint32 result = 0;

		if( auto param = meshView.GetParameter< mesh::MeshParamPhysics >() )
		{
			if( auto system_resource = param->GetPhysicsData() )
			{
				const auto& bodies = system_resource->GetBodies();
				result = std::accumulate( begin( bodies ), end( bodies ), 0, [ ]( Uint32 i, const auto& body ){ return i + body->m_collisionShapes.Size(); } );
			}
		}

		return result;
	}
	
	Uint32 MeshReport::GetCollisionMeshMemory( const mesh::DataViewRO& meshView )
	{
		Uint32 result = 0;

		if( auto param = meshView.GetParameter< mesh::MeshParamPhysics >() )
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
							result += static_cast<Uint32>( collider->DebugMemoryFootprint() );
						}
					}
				}
			}
		}

		return result;
	}

	//--------------------------------------------------------------------------------------------
	void MeshReport::UpdateStats()
	{
		mesh::DataViewRO meshData( *m_mesh );

		// Format source code
		String sourceCode;

		// Problem reporting
		ProblemList problemList;
		Bool foundNonOptimalMaterials = false;

		// Calculate area of triangles in each chunk
		Double totalArea = 0.0;
		DynArray< Double > chunkAreas{ red::PoolBackend() };
		CalcChunkAreas( meshData, totalArea, chunkAreas, problemList );

		// Gather used textures
		DynArray< MeshTextureInfo* > usedTextures{ red::PoolBackend() };
		GatherTexturesUsedByMesh( m_mesh.Get(), usedTextures );

		// Analyze textures
		Uint32 textureDataSize = 0;
		AnalyzeTextures( usedTextures, textureDataSize, problemList );

		// Not used materials
		UpdateStatsNotUsedMaterials( sourceCode, meshData, problemList );

		// Used bones map
		DynArray< Uint32 > allUsedBones( meshData.GetNumBones(), red::PoolBackend() );
		red::Memset( allUsedBones.TypedData(), 0, allUsedBones.DataSize() );

		// Per LOD info
		DynArray< Uint32 > lodTriangles{ red::PoolBackend() };
		DynArray< Double > lodArea{ red::PoolBackend() };
		Bool isAnythingSkinned = false;

		for ( Uint32 lodIndex = 0; lodIndex < meshData.GetNumLODs(); lodIndex++ )
		{
			DynArray<Uint16> lodChunkIndices = meshData.GetLODChunkIndices( lodIndex );

			sourceCode += "<hr>";
			CHTMLTag tagP( sourceCode, "p" );
			sourceCode += String::Printf( "<h1>LOD %i</h1>", lodIndex );

			Bool isLODSkinned = false;

			UpdateStatsLODHeader( sourceCode, meshData, lodIndex, lodChunkIndices, lodTriangles, isAnythingSkinned, isLODSkinned, problemList );

			// Used materials
			UpdateStatsLODMaterials( sourceCode, meshData, usedTextures, lodIndex, lodChunkIndices, chunkAreas,
			                         lodTriangles, lodArea, foundNonOptimalMaterials, problemList );

			// Find duplicated chunks ( chunks with the same shader and texture(s) )
			FindDuplicateChunks( m_mesh.Get(), problemList );

			// Chunk info
			UpdateStatsLODChunkInfo( sourceCode, meshData, isLODSkinned, lodIndex, lodChunkIndices, allUsedBones, problemList );
		}

		// Skinning issues
		if ( isAnythingSkinned )
		{
			UpdateStatsSkinning( sourceCode, meshData, allUsedBones, problemList );
		}

		// Texture groups
		UpdateStatsTextureGroups( sourceCode, meshData, usedTextures, problemList );

		// Textures
		UpdateStatsTextures( sourceCode, meshData, usedTextures );

		// Collision
		UpdateStatsCollision( sourceCode, meshData, problemList );

		// Skeleton
		UpdateStatsSkeleton( sourceCode, meshData, allUsedBones, problemList );

		// Create header
		String finalCode;
		{
			CHTMLTag tagHTML( finalCode, "html" );
			CHTMLTag tagBody( finalCode, "body" );

			// Import metadata.
			UpdateStatsImportInfo( finalCode, m_importPath, *m_reportInfo, m_mesh );
			finalCode += "<hr>";

			// Totals
			UpdateStatsTotals( finalCode, meshData, usedTextures, textureDataSize, problemList );

			// Sub stats
			finalCode += sourceCode;
		}

		// Update content
		m_report = finalCode;

	}
	
	void MeshReport::UpdateReportInfo()
	{
		mesh::DataViewRO meshData( *m_mesh );
		Uint32 textureDataSize = 0;
		ProblemList problemList;
		String dummyString, dummyStringB;
		DynArray< MeshTextureInfo* > usedTextures{ red::PoolBackend() };

		GatherTexturesUsedByMesh( m_mesh.Get(), usedTextures );
		AnalyzeTextures( usedTextures, textureDataSize, problemList );
		UpdateStatsImportInfo( dummyString, dummyStringB, *m_reportInfo, m_mesh );
		UpdateReportInfo( meshData, m_mesh.Get()->GetPath(), textureDataSize, *m_reportInfo );
	}


	//--------------------------------------------------------------------------------------------
	void MeshReport::UpdateStatsLODHeader( String& sourceCode, const mesh::DataViewRO& meshData,
	                                       Uint32 lodIndex, const DynArray<Uint16>& lodChunkIndices, const DynArray< Uint32 >& lodTriangles,
	                                       Bool& isAnythingSkinned, Bool& isLODSkinned, ProblemList& problemList )
	{
		// Chunk count
		sourceCode += "Chunks: <b>";
		sourceCode += RawCountToText( meshData.GetLODChunkCount( lodIndex ) );
		sourceCode += "</b><br>";

		// Count chunk data
		Uint32 numTriangles = 0;
		Uint32 numVertices = 0;
		isLODSkinned = false;

		for ( Uint32 j = 0; j < lodChunkIndices.Size(); j++ )
		{
			Uint16 chunkIndex = lodChunkIndices[j];
			numTriangles += meshData.GetChunkTriangleCount( chunkIndex );
			numVertices += meshData.GetChunkVertexCount( chunkIndex );

			EMeshVertexType chunkVertexType = meshData.GetChunkVertexType( chunkIndex );
			if ( IsSkinnedMeshVertexType( chunkVertexType ) )
			{
				isAnythingSkinned = true;
				isLODSkinned = true;
			}
		}

		// Check mesh size
		if ( lodIndex == 0 )
		{
			const Uint32 limit = isLODSkinned ? MAX_REASONABLE_SKINNED_MESH_TRIANGLES : MAX_REASONABLE_MESH_TRIANGLES;
			if ( numTriangles > limit )
			{
				problemList.EmitProblem( "Base mesh has to many triangles. Limit is %i.", limit );
			}

			if ( numTriangles > MAX_ALLOWED_TRIANGLES_WITHOUT_LOD && meshData.GetNumLODs() == 1 )
			{
				problemList.EmitProblem( "LODs are required for this mesh." );
			}
		}
		else
		{
			const Uint32 prevLodTriangleCount = lodTriangles[lodIndex - 1];
			if ( numTriangles > (prevLodTriangleCount / 2) )
			{
				problemList.EmitWarning( "LOD%i is not optimal. It should have at most 50%% triangles of LOD%i.", lodIndex, lodIndex - 1u );
			}

			if ( isAnythingSkinned && !isLODSkinned )
			{
				problemList.EmitProblem( "LOD%i is not skinned while the base mesh is. This wont work!", lodIndex );
			}
		}

		// Vertex count
		sourceCode += "Num vertices: <b>";
		sourceCode += RawCountToText( numVertices );
		sourceCode += "</b><br>";

		// Triangles count
		sourceCode += "Num triangles: <b>";
		sourceCode += RawCountToText( numTriangles );
		sourceCode += "</b><br>";

		// Render data
		sourceCode += "Render data: <b>";
		sourceCode += MemSizeToText( CalcMeshLodRenderDataSize( meshData, lodIndex ) );
		sourceCode += "</b><br>";
	}

	//--------------------------------------------------------------------------------------------
	void MeshReport::UpdateStatsLODMaterials( String& sourceCode, const mesh::DataViewRO& meshData, const DynArray< MeshTextureInfo* >& usedTextures,
		Uint32 lodIndex, const DynArray<Uint16>& lodChunkIndices, const DynArray< Double >& chunkAreas,
		DynArray< Uint32 >& lodTriangles, DynArray< Double >& lodArea, Bool& foundNonOptimalMaterials, ProblemList& problemList )
	{
		//*
		sourceCode += "<h3>Used materials:</h3><br>";
		sourceCode += "<table border=1>";

		// Gather used materials, calculate totals
		Uint32 totalLODTriangles = 0;
		Double totalLODArea = 0.0;
		DynArray< Uint32 > usedMaterials{ red::PoolBackend() };

		for ( Uint32 j = 0; j < lodChunkIndices.Size(); j++ )
		{
			const Uint32 chunkIndex = lodChunkIndices[j];
			GatherMaterialsUsedByChunk( meshData, chunkIndex, usedMaterials );

			totalLODTriangles += meshData.GetChunkTriangleCount( chunkIndex );
			totalLODArea += chunkAreas[chunkIndex];
		}

		// Save for future :)
		lodTriangles.PushBack( totalLODTriangles );
		lodArea.PushBack( totalLODArea );
		// Header
		sourceCode += "<tr><th width=100 align=center><i>ID</i></th>";
		sourceCode += "<th align=center><i>Material Name</i></th>";
		sourceCode += "<th align=center><i>Triangles</i></th>";
		sourceCode += "<th align=center><i>% Triangles</i></th>";
		sourceCode += "<th align=center><i>% Area</i></th></tr>";

		// Add row for each material
		for ( Uint32 i = 0; i < usedMaterials.Size(); i++ )
		{
			const Uint32 materialIndex = usedMaterials[i];

			// Calculate number of triangles with this material
			Double materialArea = 0.0;
			Uint32 materialTriangles = 0;
			for ( Uint32 j = 0; j < lodChunkIndices.Size(); j++ )
			{
				// Get chunk
				const Uint32 chunkIndex = lodChunkIndices[j];
				if ( IsMaterialUsedByChunk( meshData, materialIndex, chunkIndex ) )
				{
					materialTriangles += meshData.GetChunkTriangleCount( chunkIndex );
					materialArea += chunkAreas[chunkIndex];
				}
			}

			// Usage %
			const Float prcTriangles = (Float)materialTriangles / (Float)totalLODTriangles;
#ifdef MESH_TODO
			const Float prcArea = (Float)(materialArea / totalLODArea);
#else
			const Float prcArea = 0.1f;
#endif
			const CName materialName = meshData.GetMaterialName( materialIndex );

			// To many triangles in to small area
			if ( prcArea < 0.15f )
			{
				if ( prcArea < 0.01f )
				{
					// Very non optimal materials
					problemList.EmitProblem( "Material <a href=\"mat:%s\">%s</a> is almost not used by LOD%i!", materialName.AsChar(), materialName.AsChar(), lodIndex );

					// Color!
					sourceCode += "<tr bgcolor=\"#FF6060\">";
				}
				else if ( prcTriangles > 0.05f || prcArea < 0.05f )
				{
					sourceCode += "<tr bgcolor=\"#FFC0C0\">";
				}
				else
				{
					sourceCode += "<tr bgcolor=\"#FFFFC0\">";
				}

				// Non optimal materials
				if ( !foundNonOptimalMaterials )
				{
					foundNonOptimalMaterials = true;
					problemList.EmitWarning( "Non optimal material usage." );
				}
			}
			else
			{
				sourceCode += "<tr>";
			}

			// Print stuff
			sourceCode += String::Printf( "<td>%i</td>", materialIndex );
			sourceCode += String::Printf( "<td><a href=\"mat:%s\">%s</a></td>", materialName.AsChar(), materialName.AsChar() );
			sourceCode += String::Printf( "<td>%i</td>", materialTriangles );
			sourceCode += String::Printf( "<td>%1.2f</td>", prcTriangles * 100.0f );
			sourceCode += String::Printf( "<td>%1.2f</td>", prcArea * 100.0f );
			sourceCode += "</tr>";
		}

		// End of table
		sourceCode += "</table><br>";
		//*/
	}

	//--------------------------------------------------------------------------------------------
	void MeshReport::UpdateStatsLODChunkInfo( String& sourceCode, const mesh::DataViewRO& meshData, Bool isSkinned,
		Uint32 lodIndex, const DynArray<Uint16>& lodChunkIndices,
		DynArray< Uint32 >& allUsedBones, ProblemList& problemList )
	{
		sourceCode += "<h3>Chunks in the LOD</h3><br>";
		sourceCode += "<table border=1>";

		// Header
		sourceCode += "<tr><th width=50 align=center><i>ID</i></th>";
		sourceCode += "<th align=center><i>Vertex Type</i></th>";
		sourceCode += "<th align=center><i>Material</i></th>";
		sourceCode += "<th align=center><i>BPV</i></th>";
		sourceCode += "<th align=center><i>Vertices</i></th>";
		sourceCode += "<th align=center><i>Indices</i></th>";
		sourceCode += "<th align=center><i>Vert/tri ratio</i></th>";

		// Skinning histogram
		if ( isSkinned )
		{
			sourceCode += "<th align=center><i>S1</i></th>";
			sourceCode += "<th align=center><i>S2</i></th>";
			sourceCode += "<th align=center><i>S3</i></th>";
			sourceCode += "<th align=center><i>S4</i></th>";
		}

		sourceCode += "</tr>";

		// Add row for each chunk
		for ( Uint32 j = 0; j < lodChunkIndices.Size(); j++ )
		{
			const Uint32 chunkIndex = lodChunkIndices[j];

			sourceCode += "<tr>";
			sourceCode += String::Printf( "<td>%i</td>", j );

			sourceCode += "<td>";
			sourceCode += MeshVertexTypeToName( meshData.GetChunkVertexType( chunkIndex ) );
			sourceCode += "</td>";

			sourceCode += "<td>";
#ifdef MESH_TODO
			const String materialName = m_mesh->GetMaterialNames()[chunk.m_materialID];
			sourceCode += String::Printf( "<a href=\"mat:%s\">%s</a>", materialName.AsChar(), materialName.AsChar() );
#else
			sourceCode += "TODO material names here";
#endif
			sourceCode += "</td>";

#ifdef MESH_TODO
			sourceCode += String::Printf( "<td>%i</td>", chunk.m_numBonesPerVertex );
#else
			sourceCode += String::Printf( "<td>%i</td>", 999 );
#endif

			sourceCode += String::Printf( "<td>%i</td>", meshData.GetChunkVertexCount( chunkIndex ) );

			sourceCode += String::Printf( "<td>%i</td>", meshData.GetChunkIndexCount( chunkIndex ) );

			Uint32 numTriangles = meshData.GetChunkTriangleCount( chunkIndex );
			Float ratio = (Float)meshData.GetChunkVertexCount( chunkIndex ) / numTriangles;
			sourceCode += String::Printf( "<td>%1.2f</td>", ratio );

			if ( ratio > 2.0f )
			{
				problemList.EmitWarning( "Vert / tri ratio (%.2f) is very high for chunk %d in lod %d", ratio, j, lodIndex );
			}

			if ( isSkinned )
			{
				// Calculate skinning histogram
				Uint32 s1 = 0, s2 = 0, s3 = 0, s4 = 0;
				CalcChunkSkinningHistogram( meshData, chunkIndex, s1, s2, s3, s4 );

				// Extract used bones indices
				if ( !allUsedBones.Empty() )
				{
					CalcChunkSkinningUsedBones( meshData, chunkIndex, allUsedBones );
				}

				// Skinning data
				sourceCode += String::Printf( "<td>%i</td>", s1 );
				sourceCode += String::Printf( "<td>%i</td>", s2 );
				sourceCode += String::Printf( "<td>%i</td>", s3 );
				sourceCode += String::Printf( "<td>%i</td>", s4 );
			}

			sourceCode += "</tr>";
		}

		// End of table
		sourceCode += "</table><br>";
	}

	//--------------------------------------------------------------------------------------------
	Float MeshReport::CalcMeshScore( const Float value, const MeshScoreRange range )
	{
		// MeshScoreRange contains brackets: lowest, median, heavy and extreme value.
		// This function returns a number in [0,1] range. It means which brackets were closest to the value.
		// The result has a fractional part, if the value falls between two brackets.

		constexpr Int32 numValues = 4;  // number of elements in the MeshScoreRange's array
		Int32 index = -1;

		for ( Uint32 i = 0; i < numValues; ++i )
		{
			if ( value > range.m_values[ i ] )
			{
				index = i;
			}
		}

		constexpr Int32 indexLast = numValues - 1;
		Int32 indexNext = ::Min( index + 1, indexLast );
		index = ::Max( index, 0 );
		
		if ( index == indexNext )
		{
			return Float( index ) / Float( indexLast );
		}
		else
		{
			Float indexFrac = ( value - range.m_values[ index ] ) / ( range.m_values[ indexNext ] - range.m_values[ index ] );
			return ( Float( index ) + indexFrac ) / Float( indexLast );
		}
	}

	//--------------------------------------------------------------------------------------------
	void MeshReport::UpdateReportInfo( const mesh::DataViewRO& meshData, const res::ResourcePath meshPath, Uint32 texturesDataSize, MeshEditorReportInfo& outReportInfo )
	{
		constexpr Uint8 lod0 = 0u;
		Uint32 numLODs = meshData.GetNumLODs();

		DynArray<Uint16> lodChunkIndices = meshData.GetLODChunkIndices( lod0 );
		if ( numLODs == 0 )
		{
			numLODs = 1;
			for ( Uint32 i = 0; i < meshData.GetNumChunks(); ++i )
				lodChunkIndices.PushBack( i );
		}

		outReportInfo.m_numLODs = numLODs;
		outReportInfo.m_numChunks = meshData.GetLODChunkCount( lod0 );

		outReportInfo.m_numAppearances = meshData.GetNumAppearances();
		outReportInfo.m_numBones = meshData.GetNumBones();

		Double surfaceArea = 0.0f;
		Uint32 numTrians = 0;
		Uint32 numVerts = 0;
		for ( Uint32 j = 0; j < lodChunkIndices.Size(); j++ )
		{
			const Uint32 chunkIndex = lodChunkIndices[j];
			surfaceArea += meshData.GetChunkSurfaceArea( chunkIndex );
			numVerts += meshData.GetChunkVertexCount( chunkIndex );
			numTrians += meshData.GetChunkTriangleCount( chunkIndex );
		}
		
		Vector4 boundingBoxSize = meshData.GetMesh()->GetBoundingBox().CalcSize();
		float longestBoundingBoxEdge = Max<float>(boundingBoxSize.X, boundingBoxSize.Y, boundingBoxSize.Z);
		outReportInfo.m_boundingBoxEdgeLimit = longestBoundingBoxEdge;

		Uint32 gpuMemSize = meshData.GetMesh()->GetGPUSize();

		outReportInfo.m_numVerts = numVerts;
		outReportInfo.m_numTrians = numTrians;

		outReportInfo.m_gpuMemSize = gpuMemSize;               // CalcMeshLodRenderDataSize( meshData, lodIndex );

		outReportInfo.m_dependencyDataSize = texturesDataSize;																/// @todo DP: finish this
		outReportInfo.m_numOfSmallTrians = 0;
		outReportInfo.m_numUnusedMaterials = 0;

		outReportInfo.m_numStreams = 0;
		outReportInfo.m_streamsDataSize = 0;

		outReportInfo.m_colliderCount = GetNumberOfCollisionShapes( meshData );
		outReportInfo.m_colliderMemory = GetCollisionMeshMemory( meshData );

		// =================================

		const red::StringView meshFolder = red::paths::GetParentPath( meshPath.ToStringView() );
		DynArray< red::StringView > folders = { meshFolder };
		Uint8 depth = 0;
		while ( depth < 255u )
		{
			const red::StringView parentFolder = red::paths::GetParentPath( folders.Back() );
			if ( parentFolder == "" )
			{
				break;
			}
			else
			{
				folders.EmplaceBack( parentFolder );
				depth++;
			}
		}

		// =================================

		MeshScoreRange scoreRangeBones;
		MeshScoreRange scoreRangeChunks;
		MeshScoreRange scoreRangeTrisToAreaLog;
		MeshScoreRange scoreRangeColliderCount;
		MeshScoreRange scoreRangeColliderMemory;
		if (MeshReport::LoadMeshScoreConfig( scoreRangeBones, scoreRangeChunks, scoreRangeTrisToAreaLog, scoreRangeColliderCount, scoreRangeColliderMemory, meshPath ) )
		{
			RED_REPORT_INFO( RED_LOCATION( meshPath ), "Mesh Report: Loaded mesh score config." );
		}
		else
		{
			RED_REPORT_ERROR( RED_LOCATION( meshPath ), "Mesh Report: Failed to load mesh score config file." );
		}
		
		// =================================

		Float trisToArea = static_cast<Float>(Double(outReportInfo.m_numTrians) / surfaceArea);
		Float trisToAreaLog = MLog10(trisToArea + 1.0f); // adding 1.0 for better logarithmic distribution (starting at 0)

		outReportInfo.m_scoreBones = CalcMeshScore( Float( outReportInfo.m_numBones ), scoreRangeBones );
		outReportInfo.m_scoreChunks = CalcMeshScore( Float( outReportInfo.m_numChunks ), scoreRangeChunks );
		outReportInfo.m_scoreTrisToArea = CalcMeshScore( trisToAreaLog, scoreRangeTrisToAreaLog );
		outReportInfo.m_scoreColliderCount = CalcMeshScore( Float( outReportInfo.m_colliderCount ), scoreRangeColliderCount );
		outReportInfo.m_scoreColliderMemory = CalcMeshScore( Float( outReportInfo.m_colliderMemory ), scoreRangeColliderMemory );
	}

	//--------------------------------------------------------------------------------------------
	void MeshReport::CalcChunkSkinningHistogram( const mesh::DataViewRO& meshData, Uint32 chunkIndex, Uint32& s1, Uint32& s2, Uint32& s3, Uint32& s4 )
	{
		red::StaticArray<Uint32, 9> hist;
		meshData.CalcChunkSkinningHistogram( chunkIndex, hist );

		s1 = (hist.Size() >= 2) ? hist[1] : 0;								/// @todo DP: we need 8 weights here
		s2 = (hist.Size() >= 3) ? hist[2] : 0;
		s3 = (hist.Size() >= 4) ? hist[3] : 0;
		s4 = (hist.Size() >= 5) ? hist[4] : 0;
	}

	//--------------------------------------------------------------------------------------------
	void MeshReport::CalcChunkSkinningUsedBones( const mesh::DataViewRO& meshData, Uint32 chunkIndex, red::DynArray< Uint32 >& allUsedBones )
	{
		red::StaticArray<Uint32, 9> hist;
		meshData.CalcChunkSkinningHistogram( chunkIndex, hist, &allUsedBones );
	}

	//--------------------------------------------------------------------------------------------
	void MeshReport::FindDuplicateChunks( CMesh* mesh, ProblemList& problemList )
	{
#ifdef MESH_TODO
		Int32 chunksToBeDeleted = mesh->MergeChunks( true );
		if ( chunksToBeDeleted > 0 )
		{
			problemList.EmitProblem( "Found %i chunk(s) using the same shader and texture(s). <a href=\"MergeChunks\">Fix!</a>", chunksToBeDeleted );
		}
#endif
	}

	//--------------------------------------------------------------------------------------------
	void MeshReport::UpdateStatsTotals( String& finalCode, const mesh::DataViewRO& meshData, 
	                                    const DynArray< MeshTextureInfo* >& usedTextures, Uint32 textureDataSize, ProblemList& problemList )
	{
		// Totals
		{
			CHTMLTag tagP( finalCode, "p" );
			finalCode += "<h1>Totals</h1>";

			// Render data
			finalCode += "Render data: <b>";
			finalCode += MemSizeToText( CalcMeshRenderDataSize( meshData ) );
			finalCode += "</b><br>";

			// Collision data
			finalCode += "Collision data: <b>";
			finalCode += MemSizeToText( CalcMeshCollisionDataSize( meshData ) );
			finalCode += "</b><br>";

			// Mesh data
			finalCode += "Engine data: <b>";
			finalCode += MemSizeToText( CalcMeshRawDataSize( meshData ) );
			finalCode += "</b><br>";

			// Mesh data
			finalCode += "Texture data: <b>";
			finalCode += MemSizeToText( textureDataSize );
			if ( textureDataSize > 0 )
			{
				finalCode += String::Printf( " in %i texture(s)", usedTextures.Size() );
			}
			finalCode += "</b><br>";

			// Autohide distance
			finalCode += "Autohide Distance data: <b>";
			finalCode += String::Printf( "%1.2f", meshData.GetMesh()->GetAutoHideDistance() );
			finalCode += "</b><br>";
		}

		// Problems
		if ( problemList.m_problemsCode.Length() || problemList.m_warningCode.Length() )
		{
			CHTMLTag tagP( finalCode, "p" );
			finalCode += "<h1>Problems</h1>";
			finalCode += problemList.m_problemsCode;
			finalCode += problemList.m_warningCode;
		}
	}

	//--------------------------------------------------------------------------------------------
	void MeshReport::UpdateStatsImportInfo( String& sourceCode, String& outputImportPath, MeshEditorReportInfo& outReportInfo, const THandle< CMesh >& mesh )
	{
		CHTMLTag tagP( sourceCode, "p" );
		sourceCode += "<h1>Import info</h1>";

		String importPath;
		String assetPath;
		String assetVersionControllPath;
		String sourcePrefabPath;
		res::ResourcePath path = mesh->GetPath();

		outReportInfo.m_resourcePath = path.ToString();

		if ( THandle<res::ResourceMetadata> meta = res::ResourceMetadata::Get( mesh ) )
		{
			if ( const THandle<res::ResourceImportMetadata>& importMeta = meta->GetImportData() )
			{
				outputImportPath = importPath = importMeta->GetIntermediatePath();
				assetPath = importMeta->GetAssetPath();
				assetVersionControllPath = importMeta->GetAssetVersionControlPath();

				if ( importPath.Empty() )
				{
					if ( assetVersionControllPath.Empty() )
					{
						outputImportPath = importPath = assetPath;
					}
					else
					{
						// add checksum here
						red::StringBuilder<String> sb;
						sb.Append( "vc:" );
						sb.Append( assetVersionControllPath );
						sb.Append( String::Printf( "#%d", importMeta->GetAssetVersionControlRevision() ) );
						outputImportPath = importPath = sb.ToString();
					}
				}
			}
			else
			{
				importPath = "No import metadata is defined for this mesh";
			}

			auto description = meta->GetDescription();
			if (description.BeginsWithNoCase( "Source prefab:" ))
			{
				sourcePrefabPath = description.StringAfter( ": " );
			}
		}
		else
		{
			importPath = "No resource metadata is defined for this mesh";
		}

		outReportInfo.m_importPath = outputImportPath;
		outReportInfo.m_assetPath = assetPath;

		sourceCode += "Resource path: <a href=\"" + path.ToString() + "\"><b>" + path.ToString() + "</b></a><br><br>";
		sourceCode += "Import path:<br> <b>" + importPath + "</b><br><br>";
		if (!sourcePrefabPath.Empty())
		{
			sourceCode += "Source prefab: <a href=\"" + sourcePrefabPath + "\"><b>" + sourcePrefabPath + "</b></a><br><br>";
		}
		
		if ( !assetPath.Empty() )
		{
			sourceCode += "Asset path:<br> <b>" + assetPath + "</b><br><br>";
		}
	}

	//--------------------------------------------------------------------------------------------
	void MeshReport::UpdateStatsTextures( String& sourceCode, const mesh::DataViewRO& meshData, red::DynArray< MeshTextureInfo* >& usedTextures )
	{
		sourceCode += "<hr>";
		CHTMLTag tagP( sourceCode, "p" );
		sourceCode += String::Printf( "<h1>Textures</h1>" );

		sourceCode += "<table border=1>";

		// Header
		sourceCode += "<tr><th width=130 align=center><i>Texture</i></th>";
		sourceCode += "<th align=center><i>Extra data</i></th></tr>";

		// Sort group
		std::sort( usedTextures.Begin(), usedTextures.End(), &MeshTextureInfo::CmpFuncByDataSize );

		// Add row for each chunk
		for ( Uint32 j = 0; j < usedTextures.Size(); j++ )
		{
			MeshTextureInfo* info = usedTextures[j];

			sourceCode += "<tr>";

			// The icon
			String texturePath = "Internal";
			if ( info->m_texture->HasValidPath() )
			{
				texturePath = info->m_texture->GetPath().ToString();
				sourceCode += String::Printf( "<td><a href=\"%s\"><img src=\"%s\"/></a></td>", texturePath.AsChar(), info->m_thumbnailFile.AsChar() );
			}
			else
			{
				sourceCode += String::Printf( "<td><img src=\"%s\"/></td>", info->m_thumbnailFile.AsChar() );
			}

			sourceCode += "<td valign=top>";

			// Name
			sourceCode += String::Printf( "<a href=\"%s\">%s</a>", texturePath.AsChar(), texturePath.AsChar() );
			sourceCode += "<br><br>";

			// Data
			sourceCode += String::Printf( "%i x %i, %i mipmap(s)<br>", info->m_width, info->m_height, info->m_mipMapCount );
			sourceCode += String::Printf( "%s<br>", GetTextureGroupName( info->m_texture ).AsChar() );
			sourceCode += "Mem: <b>";
			sourceCode += MemSizeToText( info->m_dataSize );
			sourceCode += "</b><br><br>";

			// List of used materials
			if ( !info->m_usedByMaterials.Empty() )
			{
				// Print list
				sourceCode += "Used in materials: ";
				for ( Uint32 k = 0; k < info->m_usedByMaterials.Size(); k++ )
				{
					const Uint32 materialIndex = info->m_usedByMaterials[k];
					const CName name = meshData.GetMaterialName( materialIndex );

					// Add to list
					if ( k > 0 )
						sourceCode += ", ";
					sourceCode += String::Printf( "<a href=\"mat:%s\">", name.AsChar() );
					sourceCode += name.AsChar();
					sourceCode += "</a>";
				}
				sourceCode += "<br><br>";

				// Scan for chunks that uses this texture
				DynArray< Uint32 > usedByChunks{ red::PoolBackend() };
				for ( Uint32 k = 0; k < info->m_usedByMaterials.Size(); k++ )
				{
					const Uint32 materialIndex = info->m_usedByMaterials[k];
#ifdef MESH_TODO
					for ( Uint32 z = 0; z < chunks.Size(); z++ )
					{
						const SMeshChunk& chunk = chunks[z];
						if ( chunk.m_materialID == materialIndex )
						{
							usedByChunks.PushBackUnique( z );
						}
					}
#endif
				}

				// Print texture triangle usage for each LOD
#ifdef MESH_TODO
				for ( Uint32 k = 0; k < lods.Size(); k++ )
				{
					const CMesh::LODLevel& lod = lods[k];

					Uint32 texturedTriangles = 0;
					Double texturedArea = 0.0;
					for ( Uint32 z = 0; z < lod.m_chunks.Size(); z++ )
					{
						const Uint32 chunkIndex = lod.m_chunks[z];
						if ( usedByChunks.Exist( chunkIndex ) )
						{
							texturedTriangles += chunks[chunkIndex].m_numIndices / 3;
							texturedArea += chunkAreas[chunkIndex];
						}
					}

					// Print stats
					const Float prcTriangles = (Float)texturedTriangles / (Float)lodTriangles[k];
					const Float prcArea = (Float)texturedArea / (Float)lodArea[k];
					sourceCode += wxString::Format( wxT( "LOD%i: %i triangles ( %1.2f%% ), %1.2f%% of Area<br>" ), k, texturedTriangles, prcTriangles * 100.0f, prcArea * 100.0f );
				}
#endif
			}

			sourceCode += "</td>";
			sourceCode += "</tr>";
		}

		// End of table
		sourceCode += "</table><br>";
	}

	//--------------------------------------------------------------------------------------------
	void MeshReport::UpdateStatsTextureGroups( String& sourceCode, const mesh::DataViewRO& meshData, const red::DynArray< MeshTextureInfo* >& usedTextures, ProblemList& problemList )
	{
		sourceCode += "<hr>";
		CHTMLTag tagP( sourceCode, "p" );
		sourceCode += String::Printf( "<h1>Texture Groups</h1>" );

		// Gather texture group info
		DynArray< MeshTextureGroupInfo > groupInfos{ red::PoolBackend() };

		for ( Uint32 j = 0; j < usedTextures.Size(); j++ )
		{
			MeshTextureInfo* info = usedTextures[j];
			CName textureGroupName = GetTextureGroupName( info->m_texture );

			// Find group info
			MeshTextureGroupInfo* group = nullptr;

			for ( MeshTextureGroupInfo& groupInfo : groupInfos )
			{
				if ( textureGroupName == groupInfo.m_name )
				{
					group = &groupInfo;
					break;
				}
			}

			// Create new group
			if ( group == nullptr )
			{
				groupInfos.PushBack( MeshTextureGroupInfo( textureGroupName ) );
				group = &groupInfos.Back();
			}

			// Report textures that are not assigned to any group
			if ( textureGroupName == RED_NAME_CONSTEXPR_NOREG( "Default" ) || !textureGroupName )
			{
				if ( info->m_texture->HasValidPath() )
				{
					String texturePath = info->m_texture->GetPath().ToString();
					problemList.EmitWarning( "Texture <a href=\"%s\">%s</a> is not assigned to any texture group.", texturePath.AsChar(), texturePath.AsChar() );
				}
			}

			// Accumulate stats
			group->m_numTextures++;
			group->m_dataSize += info->m_dataSize;
		}

		// Sort group
		std::sort( groupInfos.Begin(), groupInfos.End(), &MeshTextureGroupInfo::CmpFumcByDataSize );

		// Texture group table
		sourceCode += "<table border=1>";

		// Header
		sourceCode += "<tr><th width=50 align=center><i>Name</i></th>";
		sourceCode += "<th align=center><i>Num Textures</i></th>";
		sourceCode += "<th align=center><i>Data Size</i></th></tr>";

		// Add row for each texture group
		for ( Uint32 j = 0; j < groupInfos.Size(); j++ )
		{
			const MeshTextureGroupInfo& group = groupInfos[j];

			sourceCode += "<tr>";

			sourceCode += "<td>" + String( group.m_name.AsChar() ) + "</td>";                // Name
			sourceCode += "<td>" + String::Printf( "%i", group.m_numTextures ) + "</td>";    // Count
			sourceCode += "<td>" + MemSizeToText( group.m_dataSize ) + "</td>";              // Memory size

			sourceCode += "</tr>";
		}

		// End of table
		sourceCode += "</table><br>";
	}

	//--------------------------------------------------------------------------------------------
	void MeshReport::UpdateStatsNotUsedMaterials( String& sourceCode, const mesh::DataViewRO& meshData, ProblemList& problemList )
	{
		// Initialize with all materials
		DynArray< Uint32 > notUsedMaterials{ red::PoolBackend() };
		for ( Uint32 i = 0; i < meshData.GetNumMaterials(); i++ )
		{
			notUsedMaterials.PushBack( i );
		}

		for ( Uint32 appearanceIndex = 0; appearanceIndex < meshData.GetNumAppearances(); appearanceIndex++ )
		{
			THandle< mesh::MeshAppearance > appearance = meshData.GetAppearanceData( appearanceIndex );
			for ( Uint32 chunkIndex = 0; chunkIndex < appearance->GetNumChunks(); ++chunkIndex )
			{
				const CName& materialName = appearance->GetChunkMaterialName( chunkIndex );
				for ( Uint32 i = 0; i < meshData.GetNumMaterials(); i++ )				/// @todo DP: move to mesh::DataViewRO, GetMaterialIndex
				{
					if ( materialName == meshData.GetMaterialName( i ) )
					{
						notUsedMaterials.Remove( i );
						break;
					}
				}
			}
		}

		// Report any unused materials !
		for ( Uint32 i = 0; i < notUsedMaterials.Size(); i++ )
		{
			const CName materialName = meshData.GetMaterialName( notUsedMaterials[i] );
			problemList.EmitProblem( "Material \"%s\" is not used! <a href=\"RemoveUnusedMaterials\">Fix!</a>", materialName.AsChar() );
		}
	}

	//--------------------------------------------------------------------------------------------
	void MeshReport::UpdateStatsSkeleton( String& sourceCode, const mesh::DataViewRO& meshData, const red::DynArray< Uint32 >& allUsedBones, ProblemList& problemList )
	{
		Uint32 numMeshBones = meshData.GetNumBones();
		if ( numMeshBones > 0 )
		{
			sourceCode += "<hr>";
			CHTMLTag tagP( sourceCode, "p" );
			sourceCode += String::Printf( "<h1>Skeleton</h1>" );

			// Vertex count
			sourceCode += "Number of bones: <b>";
			sourceCode += RawCountToText( numMeshBones );
			sourceCode += "</b><br><i>";

#ifdef MESH_TODO
			for ( Uint32 i = 0; i < lods.Size(); i++ )
			{
				const Uint32 lodIndex = i;
				const Uint32 numBones = mesh->CountLODBones( lodIndex );

				sourceCode += String::Printf( "LOD %i - ", lodIndex );
				sourceCode += RawCountToText( numBones );
				sourceCode += " bone(s)<br>";
			}
#endif

			sourceCode += "</i><br>";

			// Create table
			sourceCode += "<table border=1>";

			// Header
			sourceCode += "<tr><th width=50 align=center><i>ID</i></th>";
			sourceCode += "<th align=center><i>Name</i></th>";
			sourceCode += "<th align=center><i>Vertices</i></th></tr>";
			sourceCode += "<th align=center><i>Vertex epsilon</i></th>";

			// Add bone info
			Uint32 notUsedBonesCount = 0;
			const Uint32 maxNotUsedBonesReported = 10;
			for ( Uint32 i = 0; i < numMeshBones; i++ )
			{
				sourceCode += "<tr>";

				// ID
				sourceCode += String::Printf( "<td>%i</td>", i );

				// Name
				sourceCode += "<td>";
				sourceCode += meshData.GetBoneName( i ).AsChar();
				sourceCode += "</td>";

				// Mapped vertices
				sourceCode += "<td>";
				sourceCode += RawCountToText( allUsedBones[i] );
				sourceCode += "</td>";

				// Vertex epsilon
				sourceCode += "<td>";
				sourceCode += String::Printf( "%f", meshData.GetBoneVertexEpsilon( i ) );
				sourceCode += "</td>";

				// End
				sourceCode += "</tr>";

				// Not used bone !
				if ( !allUsedBones[i] )
				{
					if ( notUsedBonesCount < maxNotUsedBonesReported )
					{
						problemList.EmitWarning( "Bone '%s' is not used by skinning. <a href=\"RemoveUnusedBones\">Fix!</a>", meshData.GetBoneName( i ).AsChar() );
					}

					notUsedBonesCount++;
				}
			}

			// Final error
			if ( notUsedBonesCount > maxNotUsedBonesReported )
			{
				const Uint32 moreBones = notUsedBonesCount - maxNotUsedBonesReported;
				problemList.EmitWarning( "%i more bones are not used by skinning...", moreBones );
			}

			// End of table
			sourceCode += "</table><br>";
		}
	}

	//--------------------------------------------------------------------------------------------
	void MeshReport::UpdateStatsSkinning( String& sourceCode, const mesh::DataViewRO& meshData, const red::DynArray< Uint32 >& allUsedBones, ProblemList& problemList )
	{
		// Count used bones
		Uint32 numUsedBones = 0;
		for ( Uint32 i = 0; i < allUsedBones.Size(); i++ )
		{
			if ( allUsedBones[i] )
			{
				numUsedBones++;
			}
		}

		if ( numUsedBones <= 1 )
		{
			problemList.EmitProblem( "%s Skinning is not needed. <a href=\"RemoveSkinningData\">Fix!</a>", numUsedBones == 1 ? "Mesh uses only 1 bone." : "Mesh doesn't use any bones." );
		}
	}

	//--------------------------------------------------------------------------------------------
	void MeshReport::UpdateStatsCollision( String& sourceCode, const mesh::DataViewRO& meshData, ProblemList& problemList )
	{
		auto system_resource = mesh::utils::GetActivePhysics(*meshData.GetMesh());
		if( !system_resource )
			return;

		physics::SystemResourceValidator validator;
		if( validator.Validate(system_resource ) == false )
		{
			if( validator.m_reason != physics::SystemResourceValidator::FailReason_None )
			{
				problemList.EmitProblem( "Physics Data Error: %hs", validator.m_message.AsChar() );
			}

			for( auto& actorFailData : validator.m_perActorData )
			{
				if( actorFailData.m_reason != physics::SystemResourceValidator::FailReason_None )
				{
					problemList.EmitProblem( "%hs", actorFailData.m_message.AsChar() );
				}

				for( Uint32 i = 0; i < actorFailData.m_perShapeReasons.Size(); ++i )
				{
					if( actorFailData.m_perShapeReasons[i] != physics::SystemResourceValidator::FailReason_None )
					{
						problemList.EmitProblem( "%hs", actorFailData.m_perShapeMessages[i].AsChar() );
					}
				}
			}
		}

		sourceCode += "<hr>";
		CHTMLTag tagP( sourceCode, "p" );
		sourceCode += String::Printf( "<h1>Physics</h1>" );

		sourceCode += "Number of Bodies: <b>";
		sourceCode += RawCountToText( system_resource->GetNumBodies() );
		sourceCode += "</b><br><i>";

		sourceCode += "Number of Joints: <b>";
		sourceCode += RawCountToText( system_resource->GetNumJoints() );
		sourceCode += "</b><br><i>";

		sourceCode += "<table border=1>";

		// Header
		sourceCode += "<tr><th width=50 align=center><i>Index</i></th>";
		sourceCode += "<th align=center><i>Num Shapes</i></th>";

		// Add row for each texture group
		Uint32 idx = 0;
		for ( auto& obj : system_resource->GetBodies() )
		{
			auto& body = Cast<physics::SystemBody>(obj);

			sourceCode += "<tr>";

			sourceCode += "<td>" + String::Printf( "%d", idx ) + "</td>";
			sourceCode += "<td>" + String::Printf( "%d", body->m_collisionShapes.Size() ) + "</td>";    // Count

			sourceCode += "</tr>";
		}

		// End of table
		sourceCode += "</table><br>";
	}

	void MeshReport::UpdateStatsBoundingBox(String& sourceCode, const mesh::DataViewRO& meshData, ProblemList& problemList)
	{	
		Vector4 boundingBoxSize = meshData.GetMesh()->GetBoundingBox().CalcSize();
		float longestBoundingBoxEdge = Max<float>(boundingBoxSize.X, boundingBoxSize.Y, boundingBoxSize.Z);
		if (longestBoundingBoxEdge > MAX_BOUNDING_BOX_EDGE_LENGHT)
		{
			problemList.EmitProblem("Mesh exceeds the %im bounding box limit", MAX_BOUNDING_BOX_EDGE_LENGHT);
		}
	}

	//--------------------------------------------------------------------------------------------
	//void MeshReport::GetReportInfo( MeshEditorReportInfo& outReportInfo )
	//{
	//	outReportInfo = *m_reportInfo;
	//}

	const tools::MeshEditorReportInfo& MeshReport::GetReportInfo()
	{
		return *m_reportInfo;
	}

	//--------------------------------------------------------------------------------------------

} // tools
