#include "build.h"
#include "materialLayersLoader.h"
#include "../../../common/renderBackend/include/materialLibrary.h"
#include "../../../common/resourceMaterial/include/materialLayerLibrary.h"
#include "../../../common/resource/include/jsonFileHelper.h"
#include "../../../common/redCore/include/absolutePath.h"
#include "../../../common/resourceMesh/include/mesh.h"
#include "../../../common/resourceMesh/include/meshDataViewRO.h"
#include "../../../backend/backendMesh/include/meshDataView.h"
#include "../../../backend/backendMesh/include/meshDataBuilder.h"
#include "../../../common/resourceMaterial/include/materialInstance.h"
#include "../../../common/resourceMaterial/include/bitmapTexture.h"
#include "../../../common/resourceMesh/include/meshAppearance.h"
#include "../../../common/redFileSystem/include/fileSys.h"
#include "../../../common/redReflection/include/resourceUtils.h"

namespace MaterialLayerHelpers
{
	namespace LOCAL_HELPER
	{
		static void FixPath( red::String& path )
		{
			while( path[0] == '\\' || path[0] == '/' )
			{
				path.RemoveAt( 0 );
			}
		}
	}

	Bool Load( const red::AbsolutePath& path, TMeshLayerDefinitionArray& outLayersDefinition )
	{
		// Create path to json material layers definition
		red::AbsolutePath layerDefPath = red::utils::ReplaceExtension( path, "json" );

		// load the file data
		String fileData;

		if ( !red::LoadFileToString( layerDefPath, fileData ) )
		{
			RED_LOG_ERROR( "Couldn't find source .json file for layers definition.");
			return false;
		}

		// parse JSON file
		using JSONDocument = rapidjson::JSONDocument;
		using JSONValue = rapidjson::JSONValue;
		JSONDocument d;
		d.Parse<0>(fileData.AsChar());

		// errors, cooker data base is invalid, manual deletion is required
		if ( d.HasParseError() )
		{
			RED_ASSERT( false, "Json file data for the mesh is invalid.");
			return false;
		}

		//	TO DO: This code needs to be refactored after mehs editor ui is updated ( support for importing jsons stright to multilayer material instance )

		//const JSONValue& db = d["chunks"];
		outLayersDefinition.Grow( 1 ); // chunks number

		// Chunks:
		for( Uint32 j = 0; j < 1; ++j )
		{
			const JSONValue& c = d;

			Uint32 id = 0; // JSONFileHelper::GetAttrInt(c, "chunk_id");
			
			ChunkLayerDefinition& chunkLayerDef = outLayersDefinition[id];
			TMaterialLayersArray& layers = chunkLayerDef.m_layers;

			const JSONValue& chunkLayers = c["chunk_layers"];
 			Uint32 chunkLayersCount = chunkLayers.Size();
			layers.Grow( chunkLayersCount );

			// set chunk id, texture set, start layer
			chunkLayerDef.m_chunkId = id;

			// set chunk texture mask
//			chunkLayerDef.m_maskPath = JSONFileHelper::GetAttrStr( c, "mask_path" ).AsChar();
			
//			LOCAL_HELPER::FixPath( chunkLayerDef.m_maskPath );

			Uint32 isUsingUniqueNormal = JSONFileHelper::GetAttrInt( c, "use_normal" );
			// set global normal tex
			if( isUsingUniqueNormal )
			{
			//	chunkLayerDef.m_uniqueNormalPath = JSONFileHelper::GetAttrStr( c, "unique_normal" ).AsChar();

			//	LOCAL_HELPER::FixPath( chunkLayerDef.m_uniqueNormalPath );
			}

			for( Uint32 i = 0; i < chunkLayersCount; ++i )
			{
				const JSONValue& l = chunkLayers[i];
				MaterialLayerDefinition& layerDef = layers[i];

				{
					// layer ID used for library lookup
					layerDef.m_layerName = RED_NAME( JSONFileHelper::GetAttrStr( l, "material" ) );

					// microblend ID used for library lookup
					layerDef.m_microblendName = RED_NAME( JSONFileHelper::GetAttrStr( l, "microblend" ) );

					// Override params if necessary
					if( l.HasMember( "color" ) )
					{
						const JSONValue& col = l["color"];

						if( col.IsArray() && col.Size() == 3 )
						{
							Color color( 255, 255, 255, 255 );

							RED_ASSERT( col[0u].IsNumber() ); color.R = static_cast<Uint8>(col[0u].GetDouble() * UINT8_MAX);
							RED_ASSERT( col[1u].IsNumber() ); color.G = static_cast<Uint8>(col[1u].GetDouble() * UINT8_MAX);
							RED_ASSERT( col[2u].IsNumber() ); color.B = static_cast<Uint8>(col[2u].GetDouble() * UINT8_MAX);

							layerDef.m_params.m_color = std::pair<Color, Bool>( color, true );
						}
					}
					if( l.HasMember("mat_tile") )
					{
						layerDef.m_params.m_layerTile = std::pair<Float, Bool>( JSONFileHelper::GetAttrFloat( l, "mat_tile" ), true );
					}
					if (l.HasMember("layer_opacity"))
					{
						layerDef.m_params.m_layerOpacity = std::pair<Float, Bool>(JSONFileHelper::GetAttrFloat(l, "layer_opacity"), true);
					}
					if (l.HasMember("layer_offsetU") )
					{
						layerDef.m_params.m_layerOffsetU = std::pair<Float, Bool>(JSONFileHelper::GetAttrFloat(l, "layer_offsetU"), true);
					}
					if (l.HasMember("layer_offsetV"))
					{
						layerDef.m_params.m_layerOffsetV = std::pair<Float, Bool>(JSONFileHelper::GetAttrFloat(l, "layer_offsetV"), true);
					}
					if( l.HasMember("mb_tile") )
					{
						layerDef.m_params.m_microblendTile = std::pair<Float, Bool>( JSONFileHelper::GetAttrFloat( l, "mb_tile" ), true );
					}
					if( l.HasMember("microblend_contrast") )
					{
						layerDef.m_params.m_microblendContrast = std::pair<Float, Bool>( JSONFileHelper::GetAttrFloat( l, "microblend_contrast" ), true );
					}
					if( l.HasMember("microblend_normal_strength") )
					{
						layerDef.m_params.m_microblendStrength = std::pair<Float, Bool>( JSONFileHelper::GetAttrFloat( l, "microblend_normal_strength" ), true );
					}
					if( l.HasMember("palette_color_index") )
					{
						layerDef.m_params.m_paletteColorIndex = std::pair<Uint32, Bool>( JSONFileHelper::GetAttrInt( l, "palette_color_index" ), true );
					}
				}
			}
		}

		return true;
	}

	THandle< CMaterialLayerLibrary > GetMaterialLayerLibrary()
	{
		THandle< CMaterialLayerLibrary > matlib = GetMaterialLibrary()->GetMatlib();

		if ( !matlib )
		{
			RED_LOG_ERROR( "Engine: Matlib is not avialable. Trying to load..." );
			const Bool matlibLoaded = GetMaterialLibrary()->InitializeMatlib( matlib );
			RED_ASSERT( matlibLoaded, "Engine: Matlib is not avialable, skipping material assigments!" );
		}

		if ( !matlib )
		{
			RED_LOG_ERROR( "Engine: Matlib is not avialable! Skipping material assigments!" );
			return nullptr;
		}

		return matlib;
	}

	void ApplySingleChunk( red::DynArray<CName>& outMaterialNames, ::mesh::DataView& meshView, const THandle< CMaterialLayerLibrary >& matlib,
	                       const ChunkLayerDefinition& chunkLayerDef, Bool replaceExistingMaterials )
	{
		bool recreateResource = false;

		// Clear up the chunk material setup
		for ( Uint32 layer = 0; layer < chunkLayerDef.m_layers.Size(); ++layer )
		{
			const MaterialLayerDefinition& layerDef = chunkLayerDef.m_layers[layer];

			// Get base layer entry
			const MaterialLayerDef* baseLayerDef = matlib->GetLayerEntry( layerDef.m_layerName );
			if ( baseLayerDef && baseLayerDef->m_material.Get() )
			{
				THandle< CMaterialInstance > baseLayerMaterial = baseLayerDef->m_material.Get();

				// TODO LAYERS: Handle situation where override exists but base material doesn't
				THandle< CMaterialInstance > matInstance( CreateHandle< CMaterialInstance >( meshView.GetMesh(), baseLayerMaterial ) );
				if ( !matInstance )
				{
					RED_LOG_ERROR( "Engine: Creating material instance failed." );
					continue;
				}

				// Microblend
				if ( const MicroblendDef* mbDef = matlib->GetMicroblendEntry( layerDef.m_microblendName ) )
				{
					THandle< CBitmapTexture > texHandle = mbDef->m_texture.Get();
					if ( texHandle )
					{
						TResRef< ITexture > textureRef( texHandle );
						matInstance->WriteParameter( RED_NAME_CONSTEXPR( "MicroBlends" ), textureRef, recreateResource );
					}
				}

				// Mask
				THandle< CBitmapTexture > texHandle = LoadResource_DEPRECATED< CBitmapTexture >( chunkLayerDef.m_maskPath );
				if ( texHandle )
				{
					TResRef< ITexture > textureRef( texHandle );
					matInstance->WriteParameter( RED_NAME_CONSTEXPR( "Mask" ), textureRef, recreateResource );
				}

				// Scalars
				if ( layerDef.m_params.m_layerTile.second )
				{
					matInstance->WriteParameter( RED_NAME_CONSTEXPR( "LayerTile" ), layerDef.m_params.m_layerTile.first, recreateResource );
				}
				if (layerDef.m_params.m_layerOpacity.second)
				{
					matInstance->WriteParameter( RED_NAME_CONSTEXPR("LayerOpacity"), layerDef.m_params.m_layerOpacity.first, recreateResource );
				}
				if (layerDef.m_params.m_layerOffsetU.second)
				{
					matInstance->WriteParameter( RED_NAME_CONSTEXPR("LayerOffsetU"), layerDef.m_params.m_layerOffsetU.first, recreateResource );
				}
				if (layerDef.m_params.m_layerOffsetV.second)
				{
					matInstance->WriteParameter( RED_NAME_CONSTEXPR("LayerOffsetV"), layerDef.m_params.m_layerOffsetV.first, recreateResource );
				}
				if ( layerDef.m_params.m_microblendTile.second )
				{
					matInstance->WriteParameter( RED_NAME_CONSTEXPR( "MicroblendTile" ), layerDef.m_params.m_microblendTile.first, recreateResource );
				}
				if ( layerDef.m_params.m_microblendContrast.second )
				{
					matInstance->WriteParameter( RED_NAME_CONSTEXPR( "MicroblendContrast" ), layerDef.m_params.m_microblendContrast.first, recreateResource );
				}
				if ( layerDef.m_params.m_microblendStrength.second )
				{
					matInstance->WriteParameter( RED_NAME_CONSTEXPR( "MicroblendNormalStrength" ), layerDef.m_params.m_microblendStrength.first, recreateResource );
				}
				if ( layerDef.m_params.m_paletteColorIndex.second )
				{
					matInstance->WriteParameter( RED_NAME_CONSTEXPR( "TintColor" ), baseLayerDef->GetColorTint( layerDef.m_params.m_paletteColorIndex.first ), recreateResource );
				}
				if ( layerDef.m_params.m_color.second )
				{
					matInstance->WriteParameter( RED_NAME_CONSTEXPR( "TintColor" ), layerDef.m_params.m_color.first, recreateResource );
				}

				meshView.ForceRecreateMaterialSetup();      // Since we are not recreating in WriteParameter (recreateResource = false), we need to mark for recreation later.

				// Add new one
				String newMaterialName = meshView.GenerateUniqueMaterialName( layerDef.m_layerName.AsChar() );
				const CName materialName = RED_NAME( newMaterialName );

				Bool result = meshView.AddMaterial( matInstance, materialName, -1, replaceExistingMaterials );
				outMaterialNames.PushBack( materialName );
			}
			else
			{
				RED_LOG_WARNING( "Engine: Layer definition of this chunk doesn't map to base material from matlib." );
			}
		}
	}

	void Apply(const THandle< CMesh >& meshData, mesh::DataBuilder& builder, const TMeshLayerDefinitionArray& layers, Bool replaceExistingMaterials )
	{
		::mesh::DataView view(*meshData);

		THandle< CMaterialLayerLibrary > matlib = GetMaterialLayerLibrary();
		if (!matlib)
		{
			return;
		}

		for (Uint32 chunk = 0; chunk < layers.Size(); ++chunk)
		{
			const ChunkLayerDefinition& chunkLayerDef = layers[chunk];
			if (chunkLayerDef.m_chunkId >= builder.m_chunks.Size())
			{
				RED_LOG_ERROR("Engine: Chunk id %d of the layer data does not map to chunk data of the mesh.", chunkLayerDef.m_chunkId);
				continue;
			}

			ApplySingleChunk( builder.m_chunks[chunkLayerDef.m_chunkId].m_materials, view, matlib, chunkLayerDef, replaceExistingMaterials );
		}

		for ( mesh::DataBuilder::UnpackedChunk& chunk : builder.m_chunks )
		{
			if ( chunk.m_materials.Empty() && !layers.Empty() )
			{
				// Chunk has no materials. This should mean that it had "multi_layer" material name set,
				// and it was removed by importer to make it ready for JSON material application.
				// We need to fill this up with something because we do not allow empty chunks. For now we just replicate first chunk from JSON.
				ApplySingleChunk( chunk.m_materials, view, matlib, layers.Front(), replaceExistingMaterials );
			}
		}

		view.Apply();
	}

	void ApplySingleChunk( red::DynArray<CName>& outMaterialNames, ::mesh::DataView& meshView, const ChunkLayerDefinition& chunkLayerDef )
	{
		THandle< CMaterialLayerLibrary > matlib = GetMaterialLayerLibrary();
		if ( !matlib )
		{
			return;
		}

		ApplySingleChunk( outMaterialNames, meshView, matlib, chunkLayerDef, true );
	}

}