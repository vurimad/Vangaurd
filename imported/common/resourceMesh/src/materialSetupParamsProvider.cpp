/**
* Copyright (c) 2007 CD Projekt Red. All Rights Reserved.
*/
#include "build.h"
#include "materialSetupParamsProvider.h"
#include "../../resourceMaterial/include/materialDefinition.h"
#include "../../resourceMaterial/include/materialTemplate.h"
#include "../../resourceMaterial/include/materialInstance.h"
#include "../../resourceMaterial/include/materialParameter.h"
#include "../../resourceMaterial/include/multilayerSetup.h"
#include "../../resourceMaterial/include/multilayerMask.h"
#include "meshDataViewRO.h"
#include "meshAppearance.h"
#include "../../resourceMaterial/include/texture.h"
#include "../../renderData/include/renderUtils.h"
#include "../../redReflection/include/gatheredResource.h"

CMaterialSetupParamsProvider::CMaterialSetupParamsProvider( const mesh::DataViewRO& meshView, const THandle< IMaterial >& material, Bool preloadResources )
	: m_params( rend::PoolRenderData() )
	, m_chunkMaterialNames( rend::PoolRenderData() )
{
	// Extract parameters from IMaterials of the mesh resource
	const auto& numMaterials = meshView.GetNumMaterials();
	m_params.Reserve( numMaterials );
	for ( Uint32 i = 0; i < numMaterials; ++i )
	{
		const auto mat = material;
		const auto materialName = meshView.GetMaterialName( i );

		ConstructMaterialSetupParameters( mat, materialName, preloadResources );
	}

	ConstructChunkMaterialNames( meshView );
}

CMaterialSetupParamsProvider::CMaterialSetupParamsProvider( const mesh::MeshAppearance& appearance, Bool preloadResources )
	: m_params( rend::PoolRenderData() )
	, m_chunkMaterialNames( rend::PoolRenderData() )
{
	// Extract parameters from IMaterials of the mesh resource
	m_params.Reserve( appearance.GetNumChunks() );
	for ( Uint32 chunkIndex = 0; chunkIndex < appearance.GetNumChunks(); ++chunkIndex )
	{
		const CName materialName = appearance.GetChunkMaterialName( chunkIndex );
		const THandle< IMaterial >& mat = appearance.GetMaterialDataByName( materialName );
		ConstructMaterialSetupParameters( mat, materialName, preloadResources );
	}

	ConstructChunkMaterialNames( appearance );
}

CMaterialSetupParamsProvider::~CMaterialSetupParamsProvider()
{
}

// LK concerns: This is the most ridiculously inefficient method ever created in this engine (well, probably not... but you know.), but I don't care. It's just for my tests.
// "Engine will have to worry how to implement this efficiently" (Signed by AC & Dex ^^).
void CMaterialSetupParamsProvider::ConstructMaterialSetupParameters( const THandle<IMaterial>& mat, const CName materialName, Bool preloadResources )
{
	if ( mat )
	{
		SMaterialSetupEntry setupEntryParam;
		RED_FATAL_ASSERT( !setupEntryParam.m_isMasked );
		setupEntryParam.m_isMasked = mat->IsMasked();

		const IMaterialDefinition* definition = mat->GetMaterialDefinition();
		if ( definition )
		{
			setupEntryParam.m_materialResource = definition->GetRenderResource();

			for ( Int32 target = MSH_First; target < MSH_MAX; ++target )
			{
				SMaterialSetupParameters& parameters = setupEntryParam.m_parameters[target];

				const IMaterialDataProvider::TParameterDefinitionArray& paramDefinitions = definition->GetParameterDefinitions( (EMaterialShaderTarget)target );

				static_assert(IMaterialDataProviderDesc::PT_MAX == 17, "EParameterType changed, check here!");

				// Reserve space for the parameters we're about to fill.
				Uint32 numTextures = 0;
				Uint32 numScalars = 0;
				Uint32 numVectors = 0;
				Uint32 numColors = 0;
				Uint32 numNames = 0;
				Uint32 numSpecial = 0;
				for ( const auto& paramDefinition : paramDefinitions )
				{
					switch ( paramDefinition.m_type )
					{
					case IMaterialDataProviderDesc::PT_Texture:
					case IMaterialDataProviderDesc::PT_TextureArray:
					case IMaterialDataProviderDesc::PT_Cube:
						++numTextures;
						break;
					case IMaterialDataProviderDesc::PT_Scalar:
						++numScalars;
						break;
					case IMaterialDataProviderDesc::PT_Vector:
						++numVectors;
						break;
					case IMaterialDataProviderDesc::PT_Color:
						++numColors;
						break;
					case IMaterialDataProviderDesc::PT_Cpu_NameU64:
						++numNames;
						break;
					case IMaterialDataProviderDesc::PT_SkinProfile:
					case IMaterialDataProviderDesc::PT_HairProfile:
					case IMaterialDataProviderDesc::PT_Gradient:
					case IMaterialDataProviderDesc::PT_FoliageProfile:
					case IMaterialDataProviderDesc::PT_MultilayerSetup:
					case IMaterialDataProviderDesc::PT_MultilayerMask:
					case IMaterialDataProviderDesc::PT_TerrainSetup:
						++numSpecial;
						break;
					}
				}
				parameters.m_textureParameters.Reserve(parameters.m_textureParameters.Size() + numTextures);
				parameters.m_scalarParameters.Reserve(parameters.m_scalarParameters.Size() + numScalars);
				parameters.m_vectorParameters.Reserve(parameters.m_vectorParameters.Size() + numVectors);
				parameters.m_colorParameters.Reserve(parameters.m_colorParameters.Size() + numColors);
				parameters.m_nameU64Parameters.Reserve(parameters.m_nameU64Parameters.Size() + numNames);
				parameters.m_specialParameters.Reserve(parameters.m_specialParameters.Size() + numSpecial);

				for ( const auto& paramDefinition : paramDefinitions )
				{
					const CName& paramName = paramDefinition.m_name;
					switch ( paramDefinition.m_type )
					{
						case IMaterialDataProviderDesc::PT_Texture:
						case IMaterialDataProviderDesc::PT_TextureArray:
						case IMaterialDataProviderDesc::PT_Cube:
							{
								TRenderPtr< IRenderResource > texture;
								if ( mat->ExtractTextureParam( paramName, texture, preloadResources ) )
								{
									if ( texture )
									{
										MaterialTextureParameter texParam;
										texParam.m_name = paramName;
										texParam.m_value = texture;

										parameters.m_textureParameters.PushBack( texParam );
									}
								}
							}
							break;
						case IMaterialDataProviderDesc::PT_Scalar:
							{
								MaterialScalarParameter scalarParam;
								scalarParam.m_name = paramName;
								mat->ReadParameter( paramName, scalarParam.m_value );

								parameters.m_scalarParameters.PushBack( scalarParam );
							}
							break;
						case IMaterialDataProviderDesc::PT_Vector:
							{
								MaterialVectorParameter vectorParam;
								vectorParam.m_name = paramName;
								mat->ReadParameter( paramName, vectorParam.m_value );

								parameters.m_vectorParameters.PushBack( vectorParam );
							}
							break;
						case IMaterialDataProviderDesc::PT_Color:
							{
								MaterialColorParameter colorParam;
								colorParam.m_name = paramName;
								mat->ReadParameter( paramName, colorParam.m_value );

								parameters.m_colorParameters.PushBack( colorParam );
							}
							break;
						case IMaterialDataProviderDesc::PT_StructBuffer:
							{
								break;
							}
						case IMaterialDataProviderDesc::PT_Cpu_NameU64:
							{
								MaterialNameU64Parameter nameParam;
								nameParam.m_name = paramName;
	
								CName value;
								mat->ReadParameter( paramName, value );
								nameParam.m_value = rend::CalculateNameU64( value );
	
								parameters.m_nameU64Parameters.PushBack( nameParam );
							}
							break;
						case IMaterialDataProviderDesc::PT_SkinProfile:
							{
								TRenderPtr< IRenderResource > skinProfile;
								if ( mat->ExtractSkinProfileParam( paramName, skinProfile, preloadResources ) )
								{
									if ( skinProfile )
									{
										MaterialSpecialResourceParameter skinProfileParam;
										skinProfileParam.m_name = paramName;
										skinProfileParam.m_type = IMaterialDataProviderDesc::PT_SkinProfile;
										skinProfileParam.m_value = skinProfile;
										parameters.m_specialParameters.PushBack( skinProfileParam );
									}
								}
							}
							break;
						case IMaterialDataProviderDesc::PT_HairProfile:
							{
								TRenderPtr< IRenderResource > hairProfile;
								if ( mat->ExtractHairProfileParam( paramName, hairProfile, preloadResources ) )
								{
									if ( hairProfile )
									{
										MaterialSpecialResourceParameter hairProfileParam;
										hairProfileParam.m_name = paramName;
										hairProfileParam.m_type = IMaterialDataProviderDesc::PT_HairProfile;
										hairProfileParam.m_value = hairProfile;
										parameters.m_specialParameters.PushBack( hairProfileParam );
									}
								}
							}
							break;
						case IMaterialDataProviderDesc::PT_Gradient:
						{
							TRenderPtr< IRenderResource > gradient;
							if (mat->ExtractGradientParam(paramName, gradient, preloadResources))
							{
								if (gradient)
								{
									MaterialSpecialResourceParameter gradientParam;
									gradientParam.m_name = paramName;
									gradientParam.m_type = IMaterialDataProviderDesc::PT_Gradient;
									gradientParam.m_value = gradient;
									parameters.m_specialParameters.PushBack(gradientParam);
								}
							}
						}
						break;
						case IMaterialDataProviderDesc::PT_FoliageProfile:
						{
							TRenderPtr< IRenderResource > foliageProfile;
							if (mat->ExtractFoliageProfileParam(paramName, foliageProfile, preloadResources))
							{
								if (foliageProfile)
								{
									MaterialSpecialResourceParameter foliageProfileParam;
									foliageProfileParam.m_name = paramName;
									foliageProfileParam.m_type = IMaterialDataProviderDesc::PT_FoliageProfile;
									foliageProfileParam.m_value = foliageProfile;
									parameters.m_specialParameters.PushBack(foliageProfileParam);
								}
							}
						}
						break;
						case IMaterialDataProviderDesc::PT_MultilayerSetup:
							{
								TRenderPtr< IRenderResource > setup;
								if ( mat->ExtractMultilayerSetupParam( paramName, setup, preloadResources ) )
								{
									if ( setup )
									{
										MaterialSpecialResourceParameter setupParam;
										setupParam.m_name = paramName;
										setupParam.m_type = IMaterialDataProviderDesc::PT_MultilayerSetup;
										setupParam.m_value = setup;
										parameters.m_specialParameters.PushBack( setupParam );
									}
								}
							}
							break;
						case IMaterialDataProviderDesc::PT_MultilayerMask:
							{
								TRenderPtr< IRenderResource > mask;
								if ( mat->ExtractMultilayerMaskParam( paramName, mask, preloadResources ) )
								{
									if ( mask )
									{
										MaterialSpecialResourceParameter maskParam;
										maskParam.m_name = paramName;
										maskParam.m_type = IMaterialDataProviderDesc::PT_MultilayerMask;
										maskParam.m_value = mask;
										parameters.m_specialParameters.PushBack( maskParam );
									}
								}
							}
							break;
						case IMaterialDataProviderDesc::PT_TerrainSetup:
							{
							TRenderPtr< IRenderResource > setup;
								if( mat->ExtractTerrainSetupParam( paramName, setup, preloadResources ) )
								{
									if( setup )
									{
										MaterialSpecialResourceParameter setupParam;
										setupParam.m_name = paramName;
										setupParam.m_type = IMaterialDataProviderDesc::PT_TerrainSetup;
										setupParam.m_value = setup;
										parameters.m_specialParameters.PushBack( setupParam );
									}
								}
							}
							break;
						default:
							RED_ASSERT(false, "Unhandled parameter type %u", paramDefinition.m_type);
					}
				}
			}

			m_params.Insert( materialName, std::move(setupEntryParam) );
		}
	}
}

void CMaterialSetupParamsProvider::ConstructChunkMaterialNames( const mesh::DataViewRO& meshView )
{
	THandle< mesh::MeshAppearance > appearance = meshView.GetDefaultAppearanceData();
	RED_FATAL_ASSERT( appearance != nullptr );
	ConstructChunkMaterialNames( *appearance );
}

void CMaterialSetupParamsProvider::ConstructChunkMaterialNames( const mesh::MeshAppearance& appearance )
{
	m_chunkMaterialNames.Resize( appearance.GetNumChunks() );
	for ( Uint32 chunkIndex = 0; chunkIndex < appearance.GetNumChunks(); ++chunkIndex )
	{
		m_chunkMaterialNames[chunkIndex] = appearance.GetChunkMaterialName( chunkIndex );
	}
}


