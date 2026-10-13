/**
* Copyright (c) 2019 CD Projekt Red. All Rights Reserved.
*/
#include "build.h"
#include "../../../common/resourceMaterial/include/materialInstance.h"
#include "../../../common/resourceMaterial/include/materialTemplate.h"
#include "../../backendEngine/include/importer.h"
#include "../../../common/redReflection/include/resourceDepot.h"
#include "../../../common/resource/include/resourceSaver.h"

// Bitmaps
#include "../../../common/renderData/include/imageProcessing.h"
#include "../../../common/resourceMaterial/include/sourceTextureData.h"
#include "../../../common/resourceMaterial/include/bitmapTexture.h"

// Import
#include "../../backendMesh/include/meshDataView.h"
#include "../../backendMesh/include/meshDataBuilder.h"

#include "../../../common/redFileSystem/include/fileSys.h"
#include "../../../common/resourceMesh/include/meshParamSpeedTreeWind.h"
#include "../../../common/resourceMesh/include/meshParamPhysics.h"

#include "../../../common/physics/include/physicsSystemResource.h"
#include "../../../common/physics/include/physicsSystemBody.h"

#include "../../backendMaterial/include/bitmapTextureCooker.h"

#include "../../backendEngine/include/resourceMetadata.h"
#include "../../backendEngine/include/resourceImportMetadata.h"

#include "../../../common/resource/include/xmlDocument.h"

#include "../../backendResources/include/importMeshSpeedTree.h"

#include "../../../common/renderData/include/speedTreeWind.h"

#include "../../../common/resourceMesh/include/meshAppearance.h"

#include "../../../common/redReflection/include/deferredBufferMapper.h"

#include "../../../common/redContainers/include/fundamentalStringParser.h"
#include "../../../common/physics/include/physicsFilterData.h"

extern bool CopyChunkFlags( const mesh::DataViewRO& meshView, mesh::DataBuilder& buildData );

namespace mesh
{
	class DataBuilder;
}

const static Float EPSILON = 0.0001f;
const static Float REFERENCE_TREE_DIAGONAL_LENGTH = 16.0f;
const static Float BILLBOARD_LOD_START_DIST = 60.0f;
const static Float LOD1_START_DIST = 20.0f;
const static Float LAST_LOD_START_DIST = 40.0f;
const static Float LOD1_MIN_START_DIST = 14.0f;

//

struct ImportTexture2DContext
{
	THandle< class XMLSpeedTreeResourceImportOptions > importOptions{ nullptr };
	String sourceTexFilePath{ String::EMPTY() };
	String destTexFileName{ String::EMPTY() };
	void* destData{ nullptr };
	ETextureRawFormat destDataRawFormat{ ETextureRawFormat::TRF_Invalid };
	Uint32 destTexWidth{ 0 };
	Uint32 destTexHeight{ 0 };
	STextureGroupSetup destTexGroupSetup{};
	MipMapGenParams mipMapGenParams{};
};

//

class XMLSpeedTreeResourceImportOptions : public ResourceImportOptions
{
	RTTI_DECLARE_TYPE( XMLSpeedTreeResourceImportOptions );
	RED_BASE_CLASS( ResourceImportOptions );

public:
	XMLSpeedTreeResourceImportOptions()
		: m_importMaterialTextures( true )
		, m_transferMaterialTexturesProperties( true )
		, m_importCollision( true )
		, m_importNewChunkFlags( false )
		, m_alphaToCoverageThreshold( 32 )
		, m_alphaToCoverageThreshold_Channel( (Uint8)ETextureChannel::TextureChannel_A )
		, m_colorTextureCompression(TCM_DXTAlpha)//using BC3 compression for color textures with detailed alpha. BC7 has artifacts on the alpha channel
		, m_colorTextureCompressionForOneSidedGeometry(TCM_DXTNoAlpha)//using BC1 compression for textures witch don't need detailed alpha (binary is enough)
		, m_colorTextureCompressionForSeamsGeometry(TCM_DXTNoAlpha)//using BC1 compression for textures witch don't need detailed alpha (binary is enough)
		, m_normalTextureCompression(TCM_DXTNoAlpha) //using BC1 compression for textures witch have no alpha component
		, m_transGlossTextureCompression(TCM_DXTNoAlpha)//using BC1 compression for textures witch have no alpha component
	{
	}

	virtual ~XMLSpeedTreeResourceImportOptions() {}

	virtual Bool NeedOptions() const override { return true; }

	Bool m_importMaterialTextures;
	Bool m_transferMaterialTexturesProperties;
	Bool m_importCollision;
	Bool m_importNewChunkFlags;
	Uint8 m_alphaToCoverageThreshold;
	Uint8 m_alphaToCoverageThreshold_Channel;
	ETextureCompression m_colorTextureCompression;
	ETextureCompression m_colorTextureCompressionForOneSidedGeometry;
	ETextureCompression m_colorTextureCompressionForSeamsGeometry;
	ETextureCompression m_normalTextureCompression;
	ETextureCompression m_transGlossTextureCompression;
};

//

class XMLSpeedTreeMeshImporter : public SpeedTreeMeshImporter
{
	RTTI_DECLARE_TYPE(XMLSpeedTreeMeshImporter);

	enum MaterialVariantType
	{
		MVT_NONE,
		MVT_TWOSIDED,
		MVT_SEAMS,
		MVT_ONESIDED,
		MVT_BILLBOARD
	};

public:
	XMLSpeedTreeMeshImporter();
	virtual ImportResult DoImport(THandle< CResource > inputResource, const THandle<ResourceImportOptions>& options) override;
	virtual THandle< ResourceImportOptions > CreateOptions( const red::AbsolutePath& sourcePath ) override;

protected:
	STextureGroupSetup CreateDestTextureSetup(const String& fileStem, const MaterialVariantType materialVariant, const THandle< XMLSpeedTreeResourceImportOptions >& importOptions );

	ImportResult ImportAllChunks(THandle< CMesh >& outputMeshData, mesh::DataBuilder& meshBuildData, mesh::DataView& meshViewData, const THandle< XMLSpeedTreeResourceImportOptions >& importOptions);
	ImportResult ImportChunk(THandle< CMesh >& outputMeshData, mesh::DataBuilder& meshBuildData, mesh::DataView& meshViewData, const THandle< XMLSpeedTreeResourceImportOptions >& importOptions, const Int32 verticesCount, const Int32 indicesCount, Uint32 matId, Int32 lodIdx, red::DynArray<red::Uint16>& pointIndices,
		red::DynArray<red::Uint16>& vertexIndices, Bool nowImportingSeamsChunk, Int32 horizontalBillboardsCount );
	ImportResult ImportTexture2D( const ImportTexture2DContext& ctx );
	ImportResult CreateSpeedTreeMaterial(THandle< CMesh >& outputMeshData, mesh::DataView& meshViewData, const THandle< XMLSpeedTreeResourceImportOptions >& importOptions, Uint32 requestedMatId, Bool isSeamsChunk, Int32 horizontalBillboardsCount, Uint32 lodIdx, CName& outMaterialNameIndex );
	ImportResult ImportCollision(THandle< CMesh >& m_outputMeshData, const THandle< XMLSpeedTreeResourceImportOptions >& importOptions );
	ImportResult ImportWind(THandle< CMesh >& m_outputMeshData);
	ImportResult ImportUserData();
	ImportResult ImportBones( const THandle< XMLSpeedTreeResourceImportOptions >& importOptions );

	Bool DoesTriangleBelongToSeams(Float vert0Blend, Float vert1Blend, Float vert2Blend, Bool & replaceBlendValues);
	template<class XmlAttr>
	static ImportResult LoadAttributeValuesFromXml(const xml::NodeIterator& nodeIt, const XmlAttr* attributesToLoad, xml::StaticString* outAttrValues, const Uint32 attrCount);

	red::UniquePtr< xml::Document > m_xmlDoc;

	//hashmap containing unique materials
	red::HashMap<CName, Uint32> m_MatNameIndexToMatId;

	red::DynArray<Vector3> m_pointsData;
	red::DynArray<Vector3> m_vertNormalsData;
	red::DynArray<Vector3> m_vertBinormalsData;
	red::DynArray<Vector3> m_vertTangentsData;
	red::DynArray<Vector2> m_vertTexCoordsData;
	red::DynArray<Float> m_vertBlendData;
	red::DynArray<Float> m_vertAoData;
	red::DynArray<Vector3> m_vertColorData;
	red::DynArray<Int32> m_vertGeometryTypeData;
	red::DynArray<Vector4> m_vertWindBranchData;
	red::DynArray<Float> m_vertBranchData;
	red::DynArray<Int16> m_boneId;

	Vector3 m_boundingBoxMin;
	Vector3 m_boundingBoxMax;
	Int32 m_horizontalBillboardsCount;
	Bool m_containsTopBillboard;
	Bool m_isGrassWind;
	Bool m_xmlHasWindData;


	static const Uint8 BONE_INVALID_INDEX = 255;
	static const Uint8 BONE_DEFAULT_MASS = 255;
	static const Uint8 MAX_BONES_IN_CHAIN = 4;
	//minBoneMass and maxBoneMass must be the same like in speedtree_3d_v8.fx
	static constexpr Float MIN_BONE_MASS = 0.01f;
	static constexpr Float MAX_BONE_MASS = 4.0f;
	const red::String bonesPositionsFileSuffix = "_bones_positions.xbm";
	const red::String bonesAdditionalDataFileSuffix = "_bones_additional_data.xbm";
	struct BoneData
	{
		Int32 boneOriginalIndex;
		Float startPosX;
		Float startPosY;
		Float startPosZ;
		Float endPosX;
		Float endPosY;
		Float endPosZ;
		Uint8 parentOriginalIndex;
		Uint8 mass;
	};
	struct BonesSortedData
	{
		red::SortedArray<BoneData> m_bones;

		const Uint8 GetSortedBoneIndex( Int32 boneOriginalIndex )
		{
			Uint8 boneIndex = 0;
			for ( const auto& it : m_bones )
			{
				if ( it.boneOriginalIndex == boneOriginalIndex )
				{
					return boneIndex;
				}
				boneIndex++;
			}
			return BONE_INVALID_INDEX;
		}

		Uint8 PackBoneMass( Float mass )
		{
			mass = Clamp( mass, MIN_BONE_MASS, MAX_BONE_MASS );
			mass = Max( 0.0f, mass - MIN_BONE_MASS );
			mass = ( mass / ( MAX_BONE_MASS - MIN_BONE_MASS ) ); //<0, 1>
			return (Uint8)( Min( 255u, Uint32( mass * 255.f + 0.5f ) ) ); //floating point range <0, MAX_BONE_MASS - MIN_BONE_MASS> is saved into 8 bit uint range <0, 255>
		}

		void AdjustBonesMasses( Float massesToSet[MAX_BONES_IN_CHAIN] )
		{
			for ( auto& currBone : m_bones )
			{
				Uint32 boneLevel = 0;
				BoneData* chainBone = &currBone;
				while ( chainBone->parentOriginalIndex != BONE_INVALID_INDEX )
				{
					chainBone = &m_bones[GetSortedBoneIndex( chainBone->parentOriginalIndex )];
					boneLevel++;
				}

				RED_ASSERT( boneLevel < MAX_BONES_IN_CHAIN, "There can be maximum: %d bones in chain.", MAX_BONES_IN_CHAIN );

				if ( massesToSet[boneLevel] > 1e-4f ) //0 mass means - keep original mass
				{
					Float mass = massesToSet[boneLevel] * MAX_BONE_MASS;
					currBone.mass = PackBoneMass( mass );
				}
			}
		};
	};
	Bool m_xmlContainsBones;
	BonesSortedData m_bonesSortedData;
	Uint32 m_bonesPositionsTexWidth;
	Uint32 m_bonesPositionsTexHeight;
	Uint32 m_bonesAdditionalDataTexWidth;
	Uint32 m_bonesAdditionalDataTexHeight;
	Bool   m_setToBlendWithTerrain;

	red::String m_userDataFoliageProfile;
	Float m_userDataLodNear;
	Float m_userDataLodFar;
	Float m_userDataLodBillboard;
	Float m_userDataBoneMass[MAX_BONES_IN_CHAIN];
	enum TerrainBlendForceType
	{
		YES_IF_GRASS_WIND,
		ALWAYS,
		NEVER
	};
	TerrainBlendForceType m_userDataForceTerrainBlend;
	Float m_userDataTwosidedFlipNormals;

	friend Bool operator<( const BoneData& first, const BoneData& other );
};


//Sort by distance of bone from (0, 0, 0) in local space
Bool operator<( const XMLSpeedTreeMeshImporter::BoneData& first, const XMLSpeedTreeMeshImporter::BoneData& other )
{
	float thisDist = ( first.startPosX * first.startPosX + first.startPosY * first.startPosY + first.startPosZ * first.startPosZ );
	float otherDist = ( other.startPosX * other.startPosX + other.startPosY * other.startPosY + other.startPosZ * other.startPosZ );
	return thisDist < otherDist;
}

RTTI_BEGIN_TYPE(XMLSpeedTreeMeshImporter)
RTTI_PARENT_TYPE(SpeedTreeMeshImporter)
RTTI_END_TYPE()

RTTI_BEGIN_TYPE( XMLSpeedTreeResourceImportOptions );
RTTI_PARENT_TYPE( ResourceImportOptions );
RTTI_PROPERTY_CATEGORY( "Import switches" );
RTTI_PROPERTY( m_importMaterialTextures ).editable();
RTTI_PROPERTY( m_transferMaterialTexturesProperties ).editable().tooltip( "Transfer material textures properties if corresponding textures were found. Otherwise new\ntextures will be reseted. Works only if 'Import Material Textures' option is turned on." );
RTTI_PROPERTY( m_importCollision ).editable();
RTTI_PROPERTY( m_importNewChunkFlags ).editable();
RTTI_PROPERTY( m_alphaToCoverageThreshold ).editable().range(0, 255);
RTTI_PROPERTY( m_colorTextureCompression ).editable();
RTTI_PROPERTY( m_colorTextureCompressionForOneSidedGeometry ).editable();
RTTI_PROPERTY( m_colorTextureCompressionForSeamsGeometry ).editable();
RTTI_PROPERTY( m_normalTextureCompression ).readOnly(); //the sampling method of the normal map might need to be changed in shader after changing compression format (for example BC5 has just 2 channels and BC1 has 3)
RTTI_PROPERTY( m_transGlossTextureCompression ).editable();
RTTI_END_TYPE();

XMLSpeedTreeMeshImporter::XMLSpeedTreeMeshImporter()
{
	m_formats.PushBack(CFileFormat("xml", "Speed Tree XML Resource File"));
}

THandle< ResourceImportOptions > XMLSpeedTreeMeshImporter::CreateOptions( const red::AbsolutePath& sourcePath )
{
	THandle< XMLSpeedTreeResourceImportOptions > options = CreateHandle< XMLSpeedTreeResourceImportOptions >();
	return options;
}

STextureGroupSetup XMLSpeedTreeMeshImporter::CreateDestTextureSetup(const String& fileStem, const MaterialVariantType materialVariant, const THandle< XMLSpeedTreeResourceImportOptions >& importOptions )
{
	STextureGroupSetup texSetup;
	GpuWrapApi::eTextureGroup texGroup = GpuWrapApi::TEXG_Generic_Color;

	//The suffixes here have to match suffixes in redSpeedTreeTexturePacker.xml set for exported textures in SpeedTree Modeler
	const Bool isColorTex = fileStem.EndsWith(("_color"));
	const Bool isNormalTex = fileStem.EndsWith(("_normal"));
	const Bool isTranslGlossTex = fileStem.EndsWith(("_trans_gloss"));

	//setup texture group
	if (isNormalTex)
	{
		texGroup = GpuWrapApi::TEXG_Generic_Normal;
	}

	TexGroupHelpers::CreateBaseTextureGroup(texGroup, texSetup);

	//override specific parameters
	if ( isColorTex )
	{
		texSetup.m_alphaToCoverageThreshold			= importOptions->m_alphaToCoverageThreshold;
		texSetup.m_alphaToCoverageThreshold_Channel = importOptions->m_alphaToCoverageThreshold_Channel;
		texSetup.m_isGamma = true;
		if ( materialVariant == MVT_ONESIDED )
		{
			//one sided geometry doesn't contain alpha or its alpha can be often reduced to the binary format like in BC1
			texSetup.m_compression = importOptions->m_colorTextureCompressionForOneSidedGeometry;
		}
		else if( materialVariant == MVT_SEAMS )
		{
			texSetup.m_compression = importOptions->m_colorTextureCompressionForSeamsGeometry;
		}
		else
		{
			texSetup.m_compression = importOptions->m_colorTextureCompression;
		}
	}
	else if( isNormalTex )
	{
		texSetup.m_compression = importOptions->m_normalTextureCompression;
	}
	else if ( isTranslGlossTex )
	{
		texSetup.m_compression = importOptions->m_transGlossTextureCompression;
	}

	return texSetup;
}

ImportResult XMLSpeedTreeMeshImporter::ImportTexture2D( const ImportTexture2DContext& ctx )
{
	String destDirectory = GetDestTexturesDirectory( ctx.importOptions->GetResourcePath().ToString() );
	String destResourcePath = destDirectory + ctx.destTexFileName;
	res::ResourcePath resPath = res::ResourcePath::Build( destResourcePath );

	STextureGroupSetup textureGroupSetup = ctx.destTexGroupSetup;

	// Check for existing texture resource
	if ( GetResourceDepot()->ResourceExists( resPath ) )
	{
		const auto oldTexResource = Cast< CBitmapTexture >( GResourceLoader->IssueLoadingRequest( resPath )->WaitUntilLoaded() );
		if ( oldTexResource )
		{
			// Transfer parameters from existing texture resource
			if ( ctx.importOptions->m_transferMaterialTexturesProperties )
			{
				RED_LOG( "Core: Texture '%hs' already exists. Some parameters are getting transfered from previous one.", resPath.ToDebugString() );
				textureGroupSetup = oldTexResource->GetTextureSetup();
			}
			else
			{
				RED_LOG( "Core: Texture '%hs' already exists. It's getting overriden by import data.", resPath.ToDebugString() );
			}

			// This is needed to make sure that all deferred data buffers are loaded and there are no references to the file handle.
			// Without this, we were failing on trying to create FileWriter in res::SaveResource due to file handle "Sharing violation"
			serialization::DeferredDataBufferLoader bufferLoader;
			bufferLoader.Run( oldTexResource );
		}
	}

	THandle< CSourceTexture > sourceTexture = CreateHandle< CSourceTexture >();
	sourceTexture->Init( ctx.destTexWidth, ctx.destTexHeight, 1, ctx.destDataRawFormat );
	const Uint32 destTexPitch = ctx.destTexWidth * Texture::GetPixelSizeInBits( ctx.destDataRawFormat ) / 8;
	Bool result = sourceTexture->CreateFromRawData( ctx.destData, ctx.destTexWidth, ctx.destTexHeight, 1, destTexPitch );
	if ( !result )
	{
		return ImportResult::Error( "Something failed when creating source texture %hs", ctx.destTexFileName.AsChar() );
	}
	THandle< CBitmapTexture > texture = CreateHandle< CBitmapTexture >();
	texture->SetMipMapGenParams( ctx.mipMapGenParams );
	result = texture->InitFromSourceData( sourceTexture, textureGroupSetup );
	if ( !result )
	{
		return ImportResult::Error( "Something failed when creating texture (from the source) %hs", ctx.destTexFileName.AsChar() );
	}

	// Create all render blobs on import
	tools::BitmapTextureCookerSetup cookerSetup( *texture );
	cookerSetup.cookDURANGO = true;
	cookerSetup.cookORBIS = true;
	cookerSetup.cookWINDOWS_SERVER = true;
	tools::BitmapTextureCooker cooker( cookerSetup );
	result = cooker.DoCook();
	if ( !result )
	{
		return ImportResult::Error( "Something failed when cooking texture %hs", ctx.destTexFileName.AsChar() );
	}

	// Some resources are created with data generated from code and not from asset files and those not need import metadata
	if ( ctx.sourceTexFilePath != String::EMPTY() )
	{
		THandle< res::ResourceImportMetadata > importMetadata = CreateHandle<res::ResourceImportMetadata>();
		importMetadata->SetAssetPath( ctx.sourceTexFilePath );
		importMetadata->SetIntermediatePath( ctx.sourceTexFilePath );
		res::ResourceMetadata::SetImportData( texture, importMetadata );
	}

	res::ResourceSavingOptions options;
	options.m_targetPath = resPath;
	res::SaveResource( texture, options );

	return ImportResult::Success( nullptr );
}

ImportResult XMLSpeedTreeMeshImporter::CreateSpeedTreeMaterial(THandle< CMesh >& outputMeshData, mesh::DataView& meshViewData, const THandle< XMLSpeedTreeResourceImportOptions >& importOptions, Uint32 requestedMatId, Bool isSeamsChunk, Int32 horizontalBillboardsCount, Uint32 lodIdx, CName& outMaterialNameIndex )
{
	const red::AbsolutePath sourceFilePath = importOptions->GetSourcePath();
	MaterialVariantType materialVariantType = MVT_NONE;
	red::String materialVariantNameSuffix = red::String::EMPTY();

	xml::NodeIterator materialsIt = xml::NodeIterator(*m_xmlDoc, m_xmlDoc->GetRootNode(), "Materials");
	if (!materialsIt)
	{
		return ImportResult::Error("No 'Materials' root node in xml");
	}
	
	//check if requested material has already been generated and return it in that case.
	//Material template variant is also chosen here based on .xml attribute "Twosided" and on the isSeamsChunk function param.
	//Twosided should be only fronds, leaves and facing leaves. The remaining geometry types should never have TwoSided set in .xml material.
	xml::NodeIterator matIt(materialsIt, "Material");
	for (matIt; matIt; ++matIt)
	{
		xml::AttributeIterator matNameIt = xml::AttributeIterator(matIt, "Name");
		xml::AttributeIterator matIdIt = xml::AttributeIterator(matIt, "ID");
		xml::AttributeIterator matTwoSidedIt = xml::AttributeIterator(matIt, "TwoSided");
		if (!matNameIt || !matIdIt || !matTwoSidedIt)
		{
			return ImportResult::Error("Material doesn't contain required attributes in xml");
		}
		red::String materialName(matNameIt.GetValue().AsChar());
		Uint32 matId = matIdIt.GetValue().ToInt32();
		Bool isTwoSided = matTwoSidedIt.GetValue().ToBool();

		if (matId == requestedMatId)
		{
			if ( isTwoSided && isSeamsChunk)
			{
				return ImportResult::Error("Not supporting chunks which are two-sided and are seams simultaneously");
			}

			if ( horizontalBillboardsCount > 0 )
			{
				materialVariantNameSuffix = "_billboard";
				materialVariantType = MVT_BILLBOARD;
			}
			else if ( isTwoSided )
			{
				materialVariantNameSuffix = "_twosided";
				materialVariantType = MVT_TWOSIDED;
			}
			else if (isSeamsChunk)
			{
				materialVariantNameSuffix = "_seams";
				materialVariantType = MVT_SEAMS;
			}
			else
			{
				materialVariantNameSuffix = "_onesided";
				materialVariantType = MVT_ONESIDED;
			}

			outMaterialNameIndex = RED_NAME( ( materialName + materialVariantNameSuffix ) );

			Uint32 existingMatId = -1;
			if ( m_MatNameIndexToMatId.Find( outMaterialNameIndex, existingMatId ) )
			{
				if ( existingMatId == requestedMatId )
				{
					return ImportResult::Success( nullptr );
				}
				else
				{
					return ImportResult::Error( "There already exists a material with name: '%hs' generated from %u matID. Requested matID was %u.", outMaterialNameIndex, existingMatId, requestedMatId );
				}
			}
			break;
		}
	}

	if ( materialVariantType == MVT_NONE)
	{
		return ImportResult::Error("Referenced material ID: %u has not been defined in XML", requestedMatId);
	}

	//If we got here, then we want to generate new material from the chosen above materialVariant and based on requested matID from .xml
	res::ResourcePath materialTemplateResPath = res::ResourcePath::Build("base\\materials\\speedtree_3d_v8" + materialVariantNameSuffix + ".mt");

	//TODO wind parameters

	auto token = GResourceLoader->IssueLoadingRequest(materialTemplateResPath);
	token->WaitUntilLoaded();
	if (!token->GetResource())
	{
		return ImportResult::Error("Couldn't find speed tree material template for mesh: %hs.", sourceFilePath.AsChar());
	}

	THandle< CMaterialTemplate > speedTreeMaterialTemplate = Cast< CMaterialTemplate >(token->GetResource());

	/* Create material instance */
	THandle< CMaterialInstance > matInstance = CreateHandle< CMaterialInstance >( outputMeshData.Get(), speedTreeMaterialTemplate );
	if (!matInstance)
	{
		return ImportResult::Error("Creating speed tree material instance failed: %hs.", sourceFilePath.AsChar());
	}

	Float windLod = static_cast<Float>(lodIdx);
	matInstance->WriteParameter(RED_NAME_CONSTEXPR("WindLodFlags"), windLod, false);

	Float useWindData = m_xmlHasWindData ? 1.0f : 0.0f;
	matInstance->WriteParameter(RED_NAME_CONSTEXPR("WindDataAvailable"), useWindData, false); //TODO

	if ( horizontalBillboardsCount > 0 )
	{
		Float crownRadius = Max( m_boundingBoxMax.X - m_boundingBoxMin.X, m_boundingBoxMax.Y - m_boundingBoxMin.Y );
		matInstance->WriteParameter( RED_NAME_CONSTEXPR( "TreeCrownRadius" ), crownRadius, false );

		Float billboardsCount = static_cast<Float>( horizontalBillboardsCount );
		matInstance->WriteParameter( RED_NAME_CONSTEXPR( "HorizontalBillboardsCount" ), billboardsCount, false );

		Float containsTopBillboard = static_cast<Float>( m_containsTopBillboard );
		matInstance->WriteParameter( RED_NAME_CONSTEXPR( "ContainsTopBillboard" ), containsTopBillboard, false );
	}

	if (!m_userDataFoliageProfile.Empty())
	{
		res::ResourceTokenHandle token = GResourceLoader->IssueLoadingRequest(res::ResourcePath::Build(m_userDataFoliageProfile.AsChar()));
		TResRef<CFoliageProfile> fp = token->WaitUntilLoaded();
		if (token->HasFailed())
		{
			return ImportResult::Error("Cannot load the specified foliage profile in: %hs. Error: %d", res::ResourcePath::Build(m_userDataFoliageProfile.AsChar()).ToString().AsChar(), token->GetError() );
		}
		else
		{
			matInstance->WriteParameter(RED_NAME_CONSTEXPR("FoliageProfile"), fp, false);
		}
	}

	m_setToBlendWithTerrain = ( ( m_userDataForceTerrainBlend == YES_IF_GRASS_WIND && m_isGrassWind ) || m_userDataForceTerrainBlend == ALWAYS ) ? 1.0f : 0.0f;
	matInstance->WriteParameter( RED_NAME_CONSTEXPR( "ForceTerrainBlend" ), m_setToBlendWithTerrain, false );

	Float meshTotalHeight = m_boundingBoxMax.Z - m_boundingBoxMin.Z;
	matInstance->WriteParameter( RED_NAME_CONSTEXPR( "MeshTotalHeight" ), meshTotalHeight, false );

	if ( materialVariantType == MVT_TWOSIDED )
	{
		matInstance->WriteParameter( RED_NAME_CONSTEXPR( "TwosidedFlipN" ), m_userDataTwosidedFlipNormals, false );
	}

	Bool enableAlphaMask = true;

	/* We'll be importing textures as well. */
	const red::AbsolutePath sourceFileDirectory = red::utils::ParentAbsolutePath(sourceFilePath);

	// names must match parameter names in speedtree_3d material
	CName textureParamNames[] =
	{
		RED_NAME_CONSTEXPR("DiffuseMap"),
		RED_NAME_CONSTEXPR("NormalMap"),
		RED_NAME_CONSTEXPR("TransGlossMap")
	};

	TResAsyncRef<CBitmapTexture> sourceNormalMap;
	Uint32 texLayer = 0;
	for (xml::NodeIterator mapIt(matIt, "Map"); mapIt; ++mapIt, ++texLayer)
	{
		//TODO - does Index attribute of Map xml node correspond to ETextureLayer enumeration?

		String sourceTexFileName = "";
		if (xml::AttributeIterator fileIt = xml::AttributeIterator(mapIt, "File"))
			sourceTexFileName = fileIt.GetValue().AsChar();
		else
			continue;

		//TODO?
		// replace _s to _r (specular to roughness) if applicable
		if (sourceTexFileName.EndsWith("_s.dds"))
		{
			sourceTexFileName.Replace("_s.dds", "_r.tga", true);
		}

		String destPath = GetDestTexturesDirectory(importOptions->GetResourcePath().ToString());
		const red::AbsolutePath texFilePath(sourceFileDirectory.AddFilePath(sourceTexFileName));
		String sourceTextureStem = red::utils::ExtractFileStem(texFilePath);
		String destTextureName = sourceTextureStem + ".xbm";
		String texturePath = destPath + destTextureName;
		res::ResourcePath resPath = res::ResourcePath::Build(texturePath);

		if ( GetResourceDepot()->ResourceExists( resPath ) == false && !importOptions->m_importMaterialTextures )
		{
			return ImportResult::Error( "Chose option to not reimport textures but the destination texture %hs doesn't exist. Please, do the full reimport.", resPath.ToString().AsChar() );
		}

		if ( importOptions->m_importMaterialTextures )
		{

			// Import
			GpuWrapApi::TextureDataDesc desc;
			String extension = red::utils::ExtractExtension( texFilePath );
			GpuWrapApi::eTextureImportFormat format = TexGroupHelpers::FileExtensionToFormat( extension );
			red::UniqueBuffer srcData;
			if ( !GpuWrapApi::ImportTexture( texFilePath, format, srcData, desc ) )
			{
				return ImportResult::Error( "Texture %hs import failed!", texFilePath.AsChar() );
			}

			red::UniqueBuffer decompressedSrcData;
			GpuWrapApi::TextureDataDesc decompressedDesc;
			if ( desc.format != GpuWrapApi::eTextureFormat::TEXFMT_R8G8B8A8_Unorm )
			{
				GpuWrapApi::DecompressImage( desc, GpuWrapApi::eTextureFormat::TEXFMT_R8G8B8A8_Unorm, decompressedDesc, decompressedSrcData );
			}
			else
			{
				decompressedDesc = desc;
				decompressedSrcData = red::CreateUniqueBuffer< red::PoolBackend >( (Uint32)decompressedDesc.slicePitch, 16 );
				red::Memcpy( decompressedSrcData.Get(), srcData.Get(), (Uint32)decompressedDesc.slicePitch );
			}

			Uint32 pixelSize;
			ETextureRawFormat rawFormat;
			ETextureCompression texCompression;
			TexGroupHelpers::MapGpuToEngineTextureFormat( decompressedDesc.format, rawFormat, texCompression, pixelSize );
			STextureGroupSetup texSetup = CreateDestTextureSetup( sourceTextureStem, materialVariantType, importOptions );

			MipMapGenParams mipMapGenParams;
			if ( texLayer == 2 ) // setup mipMapGenparams only for trans_gloss tex to adjust roughness on mipmaps
			{
				mipMapGenParams.m_applyToksvig_ShouldInvChannel = true; //we store glossiness but want to adjust roughness
				mipMapGenParams.m_applyToksvig_Channel = 1;
				mipMapGenParams.m_applyToksvig_sourceNormalMap = sourceNormalMap;
			}

			ImportTexture2DContext importTexture2DContext{};
			importTexture2DContext.importOptions = importOptions;
			importTexture2DContext.sourceTexFilePath = texFilePath.AsString();
			importTexture2DContext.destTexFileName = destTextureName;
			importTexture2DContext.destData = decompressedSrcData.Get();
			importTexture2DContext.destDataRawFormat = rawFormat;
			importTexture2DContext.destTexWidth = ( Uint32 )desc.width;
			importTexture2DContext.destTexHeight = ( Uint32 )desc.height;
			importTexture2DContext.destTexGroupSetup = texSetup;
			importTexture2DContext.mipMapGenParams = mipMapGenParams;

			ImportResult texImportResult = ImportTexture2D( importTexture2DContext );
			if ( !texImportResult.IsSuccessful() )
			{
				return texImportResult;
			}
		}

		{
			// Cache bitmap
			auto token = GResourceLoader->IssueLoadingRequest(resPath);

			// wait until it's loaded
			token->WaitUntilLoaded();

			// make sure it's there
			if (token->GetResource())
			{
				TResRef< ITexture > textureResource(Cast< ITexture >(token->GetResource()));
				matInstance->WriteParameter(textureParamNames[texLayer], textureResource, false);

				if ( texLayer == 0 ) //Check if the diffuse texture contains alpha different than 255 to enable alphaMask
				{
					img::ImagePtr image = Cast<CBitmapTexture>( textureResource.Get().Get() )->GetImage();
					Float minAlpha = img::utils::GetChannelMinValue( image, 3 );
					if ( minAlpha > 1.0f - 1e-4f )
					{
						enableAlphaMask = false;
					}
				}
				else if (texLayer == 1) //normal map
				{
					sourceNormalMap.SetPath( resPath );
				}
			}
		}

	}

	matInstance->SetMaskEnabled( enableAlphaMask );

	// Add bones texture
	if( m_xmlContainsBones )
	{
		const String sourceFileStem = red::utils::ExtractFileStem(sourceFilePath);
		String destDirectory = GetDestTexturesDirectory(importOptions->GetResourcePath().ToString());

		//write bones positions texture as material parameter
		{
			String destResourcePath = destDirectory + sourceFileStem + bonesPositionsFileSuffix;
			res::ResourcePath resPath = res::ResourcePath::Build( destResourcePath );

			// Cache bitmap
			auto token = GResourceLoader->IssueLoadingRequest( resPath );

			// wait until it's loaded
			token->WaitUntilLoaded();

			// make sure it's there
			if ( token->GetResource() )
			{
				TResRef< ITexture > textureResource( Cast< ITexture >( token->GetResource() ) );
				matInstance->WriteParameter( RED_NAME_CONSTEXPR( "BonesPositionsMap" ), textureResource, false );
			}
			else
			{
				return ImportResult::Error( "Couldn't load Bones Positions texture from disk: %hs", resPath.ToDebugString() );
			}
		}

		//write bones additional data texture as material parameter
		{
			String destResourcePath = destDirectory + sourceFileStem + bonesAdditionalDataFileSuffix;
			res::ResourcePath resPath = res::ResourcePath::Build( destResourcePath );

			// Cache bitmap
			auto token = GResourceLoader->IssueLoadingRequest( resPath );

			// wait until it's loaded
			token->WaitUntilLoaded();

			// make sure it's there
			if ( token->GetResource() )
			{
				TResRef< ITexture > textureResource( Cast< ITexture >( token->GetResource() ) );
				matInstance->WriteParameter( RED_NAME_CONSTEXPR( "BonesAdditionalDataMap" ), textureResource, false );
			}
			else
			{
				return ImportResult::Error( "Couldn't load Bones Additional Data texture from disk: %hs", resPath.ToDebugString() );
			}
		}
	}

	Vector4 boneMapData = Vector4( m_xmlContainsBones ? 1.0f : 0.0f, (Float) m_bonesPositionsTexWidth, (Float)m_bonesAdditionalDataTexWidth, 0.0f );
	matInstance->WriteParameter( RED_NAME_CONSTEXPR( "BoneMapData" ), boneMapData, false );

	//add newly created material to our hash map and to currently generated mesh
	m_MatNameIndexToMatId.Insert(outMaterialNameIndex, requestedMatId);
	meshViewData.AddMaterial(matInstance, outMaterialNameIndex, -1, true);

	return ImportResult::Success(nullptr);
}

ImportResult XMLSpeedTreeMeshImporter::ImportBones( const THandle< XMLSpeedTreeResourceImportOptions >& importOptions )
{
	m_xmlContainsBones = false;

	// Export bones data to textures
	xml::NodeIterator bonesIt = xml::NodeIterator(*m_xmlDoc, m_xmlDoc->GetRootNode(), "Bones");
	if (bonesIt)
	{
		const Uint32 bonesCount = xml::AttributeIterator(bonesIt, "Count").GetValue().ToInt32();

		if ( bonesCount >= BONE_INVALID_INDEX )
		{
			return ImportResult::Error( "Mesh cannot contain more than %d bones. Bone %d is reserved for special handling", ( BONE_INVALID_INDEX -1), BONE_INVALID_INDEX );
		}

		m_bonesSortedData.m_bones.Clear();
		m_bonesSortedData.m_bones.Reserve(bonesCount);

		for (xml::NodeIterator boneIt(bonesIt, "Bone"); boneIt; ++boneIt)
		{
			//Debug
			/*BoneData boneData;
			boneData.startPosX	= ( -10.0f * ittDebug / bonesCount );
			boneData.startPosY	= ( 0 );
			boneData.startPosZ	= ( 0 );
			boneData.parentOriginalIndex	= ( 1.0f );
			boneData.endPosX	= ( -10.0f * ittDebug / bonesCount );
			boneData.endPosY	= ( 0 );
			boneData.endPosZ	= ( 0 );
			boneData.mass		= ( 1.0f );

			data.PushBack( boneData );
			ittDebug++;*/

			BoneData boneData;
			if ( xml::AttributeIterator attr = xml::AttributeIterator( boneIt, "ID" ) )
			{
				Int32 value = attr.GetValue().ToInt32();
				boneData.boneOriginalIndex = value;
			}
			else
			{
				return ImportResult::Error( "Bone lacks ParentID attribute" );
			}
			if (xml::AttributeIterator attr = xml::AttributeIterator(boneIt, "ParentID"))
			{
				Int32 value = attr.GetValue().ToInt32();
				if ( value == -1 )
				{
					value = BONE_INVALID_INDEX;
				}
				boneData.parentOriginalIndex = value;
			}
			else
			{
				return ImportResult::Error( "Bone lacks ParentID attribute" );
			}
			if (xml::AttributeIterator attr = xml::AttributeIterator(boneIt, "StartX"))
			{
				Float value = attr.GetValue().ToFloat();
				boneData.startPosX = value;
			}
			else
			{
				return ImportResult::Error( "Bone lacks StartX attribute" );
			}
			if (xml::AttributeIterator attr = xml::AttributeIterator(boneIt, "StartY"))
			{
				Float value = attr.GetValue().ToFloat();
				boneData.startPosY = value;
			}
			else
			{
				return ImportResult::Error( "Bone lacks StartY attribute" );
			}
			if (xml::AttributeIterator attr = xml::AttributeIterator(boneIt, "StartZ"))
			{
				Float value = attr.GetValue().ToFloat();
				boneData.startPosZ = value;
			}
			else
			{
				return ImportResult::Error( "Bone lacks StartZ attribute" );
			}
			if (xml::AttributeIterator attr = xml::AttributeIterator(boneIt, "EndX"))
			{
				Float value = attr.GetValue().ToFloat();
				boneData.endPosX = value;
			}
			else
			{
				return ImportResult::Error( "Bone lacks EndX attribute" );
			}
			if (xml::AttributeIterator attr = xml::AttributeIterator(boneIt, "EndY"))
			{
				Float value = attr.GetValue().ToFloat();
				boneData.endPosY = value;
			}
			else
			{
				return ImportResult::Error( "Bone lacks EndY attribute" );
			}
			if (xml::AttributeIterator attr = xml::AttributeIterator(boneIt, "EndZ"))
			{
				Float value = attr.GetValue().ToFloat();
				boneData.endPosZ = value;
			}
			else
			{
				return ImportResult::Error( "Bone lacks EndZ attribute" );
			}
			if (xml::AttributeIterator attr = xml::AttributeIterator(boneIt, "Mass"))
			{
				Float value = attr.GetValue().ToFloat();
				boneData.mass = m_bonesSortedData.PackBoneMass( value );
			}
			else
			{
				return ImportResult::Error( "Bone lacks Mass attribute" );
			}

			m_bonesSortedData.m_bones.PushBack( boneData );
		}

		//Apply bone mass modifications from user data for different levels of bones in chain
		m_bonesSortedData.AdjustBonesMasses( m_userDataBoneMass );

		// Save bone textures
		if( bonesCount > 0 )
		{
			m_bonesSortedData.m_bones.Sort();
			m_xmlContainsBones = true;

			const red::AbsolutePath sourceFilePath = importOptions->GetSourcePath();
			const red::AbsolutePath sourceFileDirectory = red::utils::ParentAbsolutePath( sourceFilePath );
			const String sourceFileStem = red::utils::ExtractFileStem( sourceFilePath );

			//Import bones positions texture, each bone occupies here 2 texels for start and end position of a bone
			{
				//Convert source data from TEXFMT_Float_R32G32B32A32 format to R16G16B16A16 with BC6H compression
				const ETextureRawFormat destinationDataRawFormat = TRF_HDRHalf; //it just has to match destination data's pixel size 
				const Uint32 pixelsCountPerBone = 2;// number of rgba pixels needed to hold start and end pos of a single bone

				//For some reason ConvertBuffer seemed to convert the format incorrectly. Converted texture seemed to contain garbage. Kept as potential future reference
				//m_bonesPositionsTexWidth = bonesCount * pixelsCountPerBone;
				//m_bonesPositionsTexHeight = 1;
				//const GpuWrapApi::eTextureFormat sourceDataFormat = GpuWrapApi::TEXFMT_Float_R32G32B32A32;
				//const GpuWrapApi::eTextureFormat destinationDataFormat = GpuWrapApi::TEXFMT_Float_R16G16B16A16;
				//const Uint32 bonesTexPixelBufferSize = m_bonesPositionsTexHeight * m_bonesPositionsTexWidth * GpuWrapApi::GetBitsPerPixel( sourceDataFormat ) / 8;
				//red::UniqueBuffer convertedData = red::CreateUniqueBuffer< red::PoolBackend >( bonesTexPixelBufferSize, 16 );
				//red::DynArray<Float> sourceData;
				//for ( Uint32 i = 0; i < m_bonesSortedData.m_bones.Size(); i++ )
				//{
				//	sourceData.PushBack( m_bonesSortedData.m_bones[i].startPosX );
				//	sourceData.PushBack( m_bonesSortedData.m_bones[i].startPosY );
				//	sourceData.PushBack( m_bonesSortedData.m_bones[i].startPosZ );
				//	sourceData.PushBack( 0.0f );
				//	sourceData.PushBack( m_bonesSortedData.m_bones[i].endPosX );
				//	sourceData.PushBack( m_bonesSortedData.m_bones[i].endPosY );
				//	sourceData.PushBack( m_bonesSortedData.m_bones[i].endPosZ );
				//	sourceData.PushBack( 0.0f );
				//}
				//GpuWrapApi::ConvertBuffer( GRender, m_bonesPositionsTexWidth, m_bonesPositionsTexHeight, 1, m_bonesPositionsTexWidth * GpuWrapApi::GetBitsPerPixel( sourceDataFormat ) / 8, sourceDataFormat,
				//						   sourceData.TypedData(), bonesTexPixelBufferSize, destinationDataFormat, convertedData, GpuWrapApi::EImageCompressionHint::CIH_None );

				const Uint32 totalNumberOfPixels = ( ( bonesCount * pixelsCountPerBone + 3 ) / 4 ) * 4;// round up to multiple of 4
				m_bonesPositionsTexHeight = 4; //minimum height of texture for BC6H compression
				m_bonesPositionsTexWidth = Max<Uint32>( 4, ( ( totalNumberOfPixels / m_bonesPositionsTexHeight + 3 ) / 4 ) * 4 ); // round up to multiple of 4
				const Uint32 bonesTexDepth = 1;

				//TODO - save the sorted bones in tiles of 4x4 for best compression results
				red::DynArray<Uint16> convertedData;
				convertedData.Resize( m_bonesPositionsTexHeight * m_bonesPositionsTexWidth * 4 ); //make this array hold start and end pos of bones, it's still 4 channel format here until it gets compressed to 3 channel BCH6H later
				for ( Uint32 i = 0; i < m_bonesSortedData.m_bones.Size(); i++ )
				{
					__m128i output;
					Vector4 input = Vector4( m_bonesSortedData.m_bones[i].startPosX, m_bonesSortedData.m_bones[i].startPosY, m_bonesSortedData.m_bones[i].startPosZ, 1.0f );
					output = Float16Compressor::CompressSSE( input.vec );
					_mm_storel_epi64( ( __m128i* )( convertedData.TypedData() + i * 8 ), output );

					input = Vector4( m_bonesSortedData.m_bones[i].endPosX, m_bonesSortedData.m_bones[i].endPosY, m_bonesSortedData.m_bones[i].endPosZ, 1.0f );
					output = Float16Compressor::CompressSSE( input.vec );
					_mm_storel_epi64( ( __m128i* )( convertedData.TypedData() + i * 8 + 4 ), output );
				}

				STextureGroupSetup textureGroupSetup;
				textureGroupSetup.m_group = GpuWrapApi::TEXG_Generic_Data;
				textureGroupSetup.m_rawFormat = destinationDataRawFormat;
				textureGroupSetup.m_compression = TCM_None; //TODO, - verify later with correct meshes if BC6_Signed compression won't introduce too many precision issues
				textureGroupSetup.m_isGamma = false;
				textureGroupSetup.m_hasMipchain = false;

				ImportTexture2DContext importTexture2DContext{};
				importTexture2DContext.importOptions = importOptions;
				importTexture2DContext.destTexFileName = sourceFileStem + bonesPositionsFileSuffix;
				importTexture2DContext.destData = convertedData.Data();
				importTexture2DContext.destDataRawFormat = destinationDataRawFormat;
				importTexture2DContext.destTexWidth = m_bonesPositionsTexWidth;
				importTexture2DContext.destTexHeight = m_bonesPositionsTexHeight;
				importTexture2DContext.destTexGroupSetup = textureGroupSetup;

				ImportResult texImportResult = ImportTexture2D( importTexture2DContext );
				if ( !texImportResult.IsSuccessful() )
				{
					return texImportResult;
				}
			}

			//Import additional bones data into second texture (cannot be compressed because we store indices of bones' parents)
			{
				//Store chains of 4 parents / masses in a square of 4 adjacent texels
				const Uint32 pixelsHorzCountPerBone = 2;
				m_bonesAdditionalDataTexHeight = 2;
				m_bonesAdditionalDataTexWidth = ( (bonesCount * pixelsHorzCountPerBone + 3 ) / 4 ) * 4; //round up to nearest 4 multiplier (otherwise for some weird reason zeros get saved into texture at last 2 upper-right texels)
				const Uint32 pixelChannelsCount = 2; //R8 texture format
				const Uint32 destDataRowSize = m_bonesAdditionalDataTexWidth * pixelChannelsCount;
				const Uint32 boneDataPixelByteSize = pixelChannelsCount * pixelsHorzCountPerBone;
				red::DynArray<Uint8> destData;
				destData.Resize( destDataRowSize * m_bonesAdditionalDataTexHeight );
				for ( Uint32 i = 0; i < m_bonesSortedData.m_bones.Size(); i++ )
				{
					//Setup default data
					destData[i * boneDataPixelByteSize] = BONE_INVALID_INDEX;
					destData[i * boneDataPixelByteSize + 1] = BONE_DEFAULT_MASS;
					destData[i * boneDataPixelByteSize + 2] = BONE_INVALID_INDEX;
					destData[i * boneDataPixelByteSize + 3] = BONE_DEFAULT_MASS;
					destData[destDataRowSize + i * boneDataPixelByteSize] = BONE_INVALID_INDEX;
					destData[destDataRowSize + i * boneDataPixelByteSize + 1] = BONE_DEFAULT_MASS;
					destData[destDataRowSize + i * boneDataPixelByteSize + 2] = BONE_INVALID_INDEX;
					destData[destDataRowSize + i * boneDataPixelByteSize + 3] = BONE_DEFAULT_MASS;

					//Fill pixels in clockwise order starting from top-left
					//first bone
					BoneData* currBone = &m_bonesSortedData.m_bones[i];
					Uint8 currBoneParentId = m_bonesSortedData.GetSortedBoneIndex( currBone->parentOriginalIndex );
					destData[i * boneDataPixelByteSize] = currBoneParentId;
					destData[i * boneDataPixelByteSize + 1] = currBone->mass;
					//second bone
					if ( currBoneParentId != BONE_INVALID_INDEX )
					{
						currBone = &m_bonesSortedData.m_bones[currBoneParentId];
						currBoneParentId = m_bonesSortedData.GetSortedBoneIndex( currBone->parentOriginalIndex );
						destData[i * boneDataPixelByteSize + 2] = currBoneParentId;
						destData[i * boneDataPixelByteSize + 3] = currBone->mass;
					}
					//third bone
					if ( currBoneParentId != BONE_INVALID_INDEX )
					{
						currBone = &m_bonesSortedData.m_bones[currBoneParentId];
						currBoneParentId = m_bonesSortedData.GetSortedBoneIndex( currBone->parentOriginalIndex );
						destData[destDataRowSize + i * boneDataPixelByteSize + 2] = currBoneParentId;
						destData[destDataRowSize + i * boneDataPixelByteSize + 3] = currBone->mass;
					}
					//fourth bone
					if ( currBoneParentId != BONE_INVALID_INDEX )
					{
						currBone = &m_bonesSortedData.m_bones[currBoneParentId];
						currBoneParentId = m_bonesSortedData.GetSortedBoneIndex( currBone->parentOriginalIndex );
						destData[destDataRowSize + i * boneDataPixelByteSize] = currBoneParentId;
						destData[destDataRowSize + i * boneDataPixelByteSize + 1] = currBone->mass;
					}
					//5th bone...
					if ( currBoneParentId != BONE_INVALID_INDEX )
					{
						return ImportResult::Error( "Too many bones in one chain. There is support for max %d bones in chain.", MAX_BONES_IN_CHAIN );
					}
				}
				const ETextureRawFormat destinationDataRawFormat = TRF_R8G8; // source data format (parentID, mass )
				STextureGroupSetup textureGroupSetup;
				textureGroupSetup.m_group = GpuWrapApi::TEXG_Generic_Data;
				textureGroupSetup.m_rawFormat = destinationDataRawFormat;
				textureGroupSetup.m_compression = TCM_None;
				textureGroupSetup.m_isGamma = false;
				textureGroupSetup.m_hasMipchain = false;

				ImportTexture2DContext importTexture2DContext{};
				importTexture2DContext.importOptions = importOptions;
				importTexture2DContext.destTexFileName = sourceFileStem + bonesAdditionalDataFileSuffix;
				importTexture2DContext.destData = destData.Data();
				importTexture2DContext.destDataRawFormat = destinationDataRawFormat;
				importTexture2DContext.destTexWidth = m_bonesAdditionalDataTexWidth;
				importTexture2DContext.destTexHeight = m_bonesAdditionalDataTexHeight;
				importTexture2DContext.destTexGroupSetup = textureGroupSetup;

				ImportResult texImportResult = ImportTexture2D( importTexture2DContext );
				if ( !texImportResult.IsSuccessful() )
				{
					return texImportResult;
				}
			}
		}
	}

	return ImportResult::Success( nullptr );
}

bool NewChunkMapsToOld( const mesh::DataBuilder::UnpackedChunk& newChunk, const mesh::DataViewRO& oldMeshView, Uint32 oldChunkIndex, bool materialMatchRequired )
{
	bool res = false;
	do
	{
		if ( newChunk.m_lodMask != oldMeshView.GetChunkLodMask( oldChunkIndex ) )
		{
			break;
		}

		auto oldChunkMatUUID = oldMeshView.GetChunkSourceMaterialUUID( oldChunkIndex );
		if ( materialMatchRequired && ( newChunk.m_materialUUID.Empty() || oldChunkMatUUID.Empty() ) )
		{
			break;
		}
		if ( ( !newChunk.m_materialUUID.Empty() && !oldChunkMatUUID.Empty() ) && ( newChunk.m_materialUUID != oldChunkMatUUID ) )
		{
			break;
		}

		auto chunkVertexType = oldMeshView.GetChunkVertexType( oldChunkIndex );
		if ( newChunk.m_vertexType != chunkVertexType )
		{
			break;
		}

		res = true;
	} while ( 0 );

	return res;
}

bool CopyChunkFlags( const mesh::DataViewRO& meshView, mesh::DataBuilder& buildData )
{
	bool copyResult = false;
	// Copy chunk flags, try to match old chunks with new ones and copy chunk flags from old to new, so they don't get reset after re-import.
	Uint32 totalOldChunks = meshView.GetNumChunks();
	if ( totalOldChunks == 0 ) return false;

	Uint32 totalNewChunks = buildData.m_chunks.Size();
	Uint32 matchedChunks = 0;

	if ( totalOldChunks == totalNewChunks ) //looks similar, hoping for small changes 
	{
		for ( Uint32 chunkIndex = 0; chunkIndex < totalNewChunks; ++chunkIndex )
		{
			Uint32 matchesTo = chunkIndex;
			mesh::DataBuilder::UnpackedChunk& newChunk = buildData.m_chunks[chunkIndex];
			if ( !NewChunkMapsToOld( newChunk, meshView, chunkIndex, false ) )
			{
				//Try to do rematch with other chunks but now material uids must be present and matched.
				bool rematchRes = false;
				for ( Uint32 rematchChunkIndex = 0; rematchChunkIndex < totalOldChunks; ++rematchChunkIndex )
				{
					if ( NewChunkMapsToOld( newChunk, meshView, rematchChunkIndex, true ) )
					{
						rematchRes = true;
						matchesTo = rematchChunkIndex;
						break;
					}
				}
				if ( !rematchRes )
				{
					continue;
					//unfortunately no luck with rematch.
				}
			}
			newChunk.m_flags = meshView.GetChunkFlags( matchesTo );
			matchedChunks++;
		}
	}
	else
	{
		for ( Uint32 chunkIndex = 0; chunkIndex < totalNewChunks; ++chunkIndex )
		{
			mesh::DataBuilder::UnpackedChunk& newChunk = buildData.m_chunks[chunkIndex];

			bool matchRes = false;
			Uint32 matchesTo = 0;
			for ( Uint32 matchChunkIndex = 0; matchChunkIndex < totalOldChunks; ++matchChunkIndex )
			{
				if ( NewChunkMapsToOld( newChunk, meshView, matchChunkIndex, true ) )
				{
					matchRes = true;
					matchesTo = matchChunkIndex;
					break;
				}
			}
			if ( !matchRes )
			{
				continue;
				//unfortunately no match found
			}

			newChunk.m_flags = meshView.GetChunkFlags( matchesTo );
			matchedChunks++;
		}
	}

	if ( matchedChunks == totalNewChunks )
	{
		copyResult = true;
	}

	return copyResult;
}

ImportResult XMLSpeedTreeMeshImporter::ImportAllChunks(THandle< CMesh >& outputMeshData, mesh::DataBuilder& meshBuildData, mesh::DataView& meshViewData, const THandle< XMLSpeedTreeResourceImportOptions >& importOptions)
{
	//hashmap containing unique materials
	m_MatNameIndexToMatId.Clear();

	//import chunks from <Objects> node
	xml::NodeIterator objectsIt(*m_xmlDoc, m_xmlDoc->GetRootNode(), "Objects");

	if ( !objectsIt )
	{
		return ImportResult::Error( "Couldn't find <Objects> node in XML" );
	}

	//Seems like each <Object> node corresponds to distinct LOD
	Int32 lodIdx = 0;
	const Int32 numberOfLods = xml::AttributeIterator(objectsIt, "Count").GetValue().ToInt32();
	Int32 numberOfLodsExcludingBillboard = numberOfLods;
	for (xml::NodeIterator objIt(objectsIt, "Object"); objIt; ++objIt, ++lodIdx)
	{
		const red::String objName(xml::AttributeIterator(objIt, "Name").GetValue().AsChar());
		if (!objName.Contains("LOD") && !objName.Contains("Billboard"))
		{
			return ImportResult::Error("Given object element : " + objName + " is not a LOD");
		}
		if (objName.Contains("Billboard"))
		{
			if (lodIdx == numberOfLods - 1)
			{
				numberOfLodsExcludingBillboard--;
			}
			else
			{
				return ImportResult::Error("Billboard LOD should be the last one in the .xml because order of LODs during an import is determined by the order of LODs in .xml.");
			}
		}
	}

	ImportResult bonesImportResult = ImportBones( importOptions );
	if ( !bonesImportResult.IsSuccessful() )
	{
		return bonesImportResult;
	}

	Float lodDistScale = 1.0f;
	lodIdx = 0;
	for (xml::NodeIterator objIt(objectsIt, "Object"); objIt; ++objIt, ++lodIdx)
	{
		const red::String objName(xml::AttributeIterator(objIt, "Name").GetValue().AsChar());
		const Bool thisLodIsABillboard = objName.Contains("Billboard");
		auto& newLevel = meshBuildData.m_lods.EmplaceBack();

		//Bounding box for billboard lod may be significantly different than the one for previous LODs...
		//Therefore, we make sure we get the lodDistScale just from the first LOD's bounding box
		if (lodIdx == 0)
		{
			m_boundingBoxMin = Vector3(
				xml::AttributeIterator(objIt, "BoundsMinX").GetValue().ToFloat(),
				xml::AttributeIterator(objIt, "BoundsMinY").GetValue().ToFloat(),
				xml::AttributeIterator(objIt, "BoundsMinZ").GetValue().ToFloat()
			);

			m_boundingBoxMax = Vector3(
				xml::AttributeIterator(objIt, "BoundsMaxX").GetValue().ToFloat(),
				xml::AttributeIterator(objIt, "BoundsMaxY").GetValue().ToFloat(),
				xml::AttributeIterator(objIt, "BoundsMaxZ").GetValue().ToFloat()
			);

			const Float diagonalLength = m_boundingBoxMax.DistanceTo(m_boundingBoxMin);
			lodDistScale = diagonalLength / REFERENCE_TREE_DIAGONAL_LENGTH;
			if (LOD1_START_DIST * lodDistScale < LOD1_MIN_START_DIST) // scale all LODs distances proportionally if first LOD gets too small
				lodDistScale *= (LOD1_MIN_START_DIST) / (LOD1_START_DIST * lodDistScale);
		}

		//scale linearly default LOD distances based on diagonal length ratio
		const Float billboardLodStartDist = m_userDataLodBillboard >= 0.0f ? m_userDataLodBillboard : BILLBOARD_LOD_START_DIST * lodDistScale;
		const Float lod0EndDist = m_userDataLodNear >= 0.0f ? m_userDataLodNear : LOD1_START_DIST * lodDistScale;
		const Float lastLodStartDist = m_userDataLodFar >= 0.0f ? m_userDataLodFar : LAST_LOD_START_DIST * lodDistScale;

		if ( lodIdx == 3 || thisLodIsABillboard )
		{
			newLevel.m_distance = billboardLodStartDist;
		}
		else if (lodIdx == 0)
		{
			newLevel.m_distance = 0.0f;
		}
		else if (lodIdx == 1)
		{
			newLevel.m_distance = lod0EndDist;
		}
		else
		{
			const Float lodFactor = static_cast<Float>(lodIdx-1) / (numberOfLodsExcludingBillboard-2);
			newLevel.m_distance = lod0EndDist + lodFactor * (lastLodStartDist - lod0EndDist);
		}

		//clear the vertex data
		m_pointsData.Clear();
		m_vertNormalsData.Clear();
		m_vertBinormalsData.Clear();
		m_vertTangentsData.Clear();
		m_vertTexCoordsData.Clear();
		m_vertBlendData.Clear();
		m_vertAoData.Clear();
		m_vertColorData.Clear();
		m_vertGeometryTypeData.Clear();
		m_vertWindBranchData.Clear();
		m_vertBranchData.Clear();
		m_boneId.Clear();

		xml::NodeIterator pointsIt(objIt, "Points");
		if ( !pointsIt )
		{
			return ImportResult::Error( "LOD %d contains no Points xml node", lodIdx );
		}
		else
		{
			red::DynArray<red::String> coords[3];
			red::Uint32 ind = 0;
			for (xml::NodeIterator coordIt(pointsIt); coordIt && ind < 3; ++coordIt, ++ind)
				coords[ind] = red::String(coordIt.GetValue().AsChar()).Split(" ");

			for (ind = 0; ind < coords[0].Size(); ++ind)
			{
				m_pointsData.PushBack(Vector3(
					Float(atof(coords[0][ind].AsChar())),
					Float(atof(coords[1][ind].AsChar())),
					Float(atof(coords[2][ind].AsChar()))));
			}
		}

		xml::NodeIterator vertsIt(objIt, "Vertices");
		if ( !vertsIt )
		{
			return ImportResult::Error( "LOD %d contains no Vertices xml node", lodIdx );
		}
		else
		{
			red::DynArray<red::String> vertsData[8];

			//all of the subnodes of <Vertices> node should have the same number of data
			/*red::Uint32 vertsCount = 0;
			if (xml::AttributeIterator attr = xml::AttributeIterator(vertsIt, "Count"))
				vertsCount = attr.GetValue().ToInt32();*/

			if (xml::NodeIterator vertDataIt = xml::NodeIterator(vertsIt, "NormalX"))
				vertsData[0] = red::String(vertDataIt.GetValue().AsChar()).Split(" ");
			if (xml::NodeIterator vertDataIt = xml::NodeIterator(vertsIt, "NormalY"))
				vertsData[1] = red::String(vertDataIt.GetValue().AsChar()).Split(" ");
			if (xml::NodeIterator vertDataIt = xml::NodeIterator(vertsIt, "NormalZ"))
				vertsData[2] = red::String(vertDataIt.GetValue().AsChar()).Split(" ");
			for (red::Uint32 i = 0; i < vertsData[0].Size(); ++i)
				m_vertNormalsData.PushBack(Vector3(
					Float(atof(vertsData[0][i].AsChar())),
					Float(atof(vertsData[1][i].AsChar())),
					Float(atof(vertsData[2][i].AsChar()))));

			if (xml::NodeIterator vertDataIt = xml::NodeIterator(vertsIt, "BinormalX"))
				vertsData[0] = red::String(vertDataIt.GetValue().AsChar()).Split(" ");
			if (xml::NodeIterator vertDataIt = xml::NodeIterator(vertsIt, "BinormalY"))
				vertsData[1] = red::String(vertDataIt.GetValue().AsChar()).Split(" ");
			if (xml::NodeIterator vertDataIt = xml::NodeIterator(vertsIt, "BinormalZ"))
				vertsData[2] = red::String(vertDataIt.GetValue().AsChar()).Split(" ");
			//we flip the sign of binormal because speedtree has Y axis downwards in tangent space 
			//(for this reason we also flip the normal y coordinate in our normal map which is exported by our custom texture packer as well)
			for (red::Uint32 i = 0; i < vertsData[0].Size(); ++i)
				m_vertBinormalsData.PushBack(/*!!!*/ - Vector3(
					Float(atof(vertsData[0][i].AsChar())),
					Float(atof(vertsData[1][i].AsChar())),
					Float(atof(vertsData[2][i].AsChar()))));

			if (xml::NodeIterator vertDataIt = xml::NodeIterator(vertsIt, "TangentX"))
				vertsData[0] = red::String(vertDataIt.GetValue().AsChar()).Split(" ");
			if (xml::NodeIterator vertDataIt = xml::NodeIterator(vertsIt, "TangentY"))
				vertsData[1] = red::String(vertDataIt.GetValue().AsChar()).Split(" ");
			if (xml::NodeIterator vertDataIt = xml::NodeIterator(vertsIt, "TangentZ"))
				vertsData[2] = red::String(vertDataIt.GetValue().AsChar()).Split(" ");
			for (red::Uint32 i = 0; i < vertsData[0].Size(); ++i)
				m_vertTangentsData.PushBack(Vector3(
					Float(atof(vertsData[0][i].AsChar())),
					Float(atof(vertsData[1][i].AsChar())),
					Float(atof(vertsData[2][i].AsChar()))));

			if (xml::NodeIterator vertDataIt = xml::NodeIterator(vertsIt, "TexcoordU"))
				vertsData[0] = red::String(vertDataIt.GetValue().AsChar()).Split(" ");
			if (xml::NodeIterator vertDataIt = xml::NodeIterator(vertsIt, "TexcoordV"))
				vertsData[1] = red::String(vertDataIt.GetValue().AsChar()).Split(" ");
			for (red::Uint32 i = 0; i < vertsData[0].Size(); ++i)
				m_vertTexCoordsData.PushBack(Vector2(
					Float(atof(vertsData[0][i].AsChar())),
					Float(atof(vertsData[1][i].AsChar()))));

			if (xml::NodeIterator vertDataIt = xml::NodeIterator(vertsIt, "Blend"))
				vertsData[0] = red::String(vertDataIt.GetValue().AsChar()).Split(" ");
			for (red::Uint32 i = 0; i < vertsData[0].Size(); ++i)
				m_vertBlendData.PushBack(
					Float(atof(vertsData[0][i].AsChar())));

			if (xml::NodeIterator vertDataIt = xml::NodeIterator(vertsIt, "AO"))
				vertsData[0] = red::String(vertDataIt.GetValue().AsChar()).Split(" ");
			for (red::Uint32 i = 0; i < vertsData[0].Size(); ++i)
				m_vertAoData.PushBack(
					Float(atof(vertsData[0][i].AsChar())));

			if (xml::NodeIterator vertDataIt = xml::NodeIterator(vertsIt, "VertexColorR"))
				vertsData[0] = red::String(vertDataIt.GetValue().AsChar()).Split(" ");
			if (xml::NodeIterator vertDataIt = xml::NodeIterator(vertsIt, "VertexColorG"))
				vertsData[1] = red::String(vertDataIt.GetValue().AsChar()).Split(" ");
			if (xml::NodeIterator vertDataIt = xml::NodeIterator(vertsIt, "VertexColorB"))
				vertsData[2] = red::String(vertDataIt.GetValue().AsChar()).Split(" ");
			for (red::Uint32 i = 0; i < vertsData[0].Size(); ++i)
				m_vertColorData.PushBack(Vector3(
					Float(atof(vertsData[0][i].AsChar())),
					Float(atof(vertsData[1][i].AsChar())),
					Float(atof(vertsData[2][i].AsChar()))));

			if (xml::NodeIterator vertDataIt = xml::NodeIterator(vertsIt, "GeometryType"))
				vertsData[0] = red::String(vertDataIt.GetValue().AsChar()).Split(" ");
			for (red::Uint32 i = 0; i < vertsData[0].Size(); ++i)
				m_vertGeometryTypeData.PushBack(
					Int32(atoi(vertsData[0][i].AsChar())));
			
			if (xml::NodeIterator vertDataIt = xml::NodeIterator(vertsIt, "BoneID"))
			{
				vertsData[0] = red::String(vertDataIt.GetValue().AsChar()).Split(" ");

				for ( red::Uint32 i = 0; i < vertsData[0].Size(); ++i )
				{
					Int16 boneOriginalIndex = Int16( atoi( vertsData[0][i].AsChar() ) );
					//we allow vertices not attached to any bones, so let the below stay commented out
					//if ( m_xmlContainsBones && !thisLodIsABillboard && boneOriginalIndex == -1 ) //if mesh is supposed to have bones, don't allow vertices without being attached to at least one bone
					//{
					//	return ImportResult::Error( "Mesh contains bones but not all vertices are attached to at least 1 bone" );
					//}
					Uint8 boneSortedIndex = m_bonesSortedData.GetSortedBoneIndex( boneOriginalIndex );
					m_boneId.PushBack( boneSortedIndex );
				}
			}
					
			if(m_xmlHasWindData)
			{
				Uint32 branchDataInd = 0;
				for (xml::NodeIterator vertDataIt = xml::NodeIterator(vertsIt, "WindBranchPos"); vertDataIt; ++vertDataIt, ++branchDataInd)
				{
					vertsData[branchDataInd] = red::String(vertDataIt.GetValue().AsChar()).Split(" ");
				}
				for (xml::NodeIterator vertDataIt = xml::NodeIterator(vertsIt, "WindBranchDir"); vertDataIt; ++vertDataIt, ++branchDataInd)
				{
					vertsData[branchDataInd] = red::String(vertDataIt.GetValue().AsChar()).Split(" ");
				}
				for (xml::NodeIterator vertDataIt = xml::NodeIterator(vertsIt, "WindBranchWeight"); vertDataIt; ++vertDataIt, ++branchDataInd)
				{
					vertsData[branchDataInd] = red::String(vertDataIt.GetValue().AsChar()).Split(" ");
				}
				for (xml::NodeIterator vertDataIt = xml::NodeIterator(vertsIt, "WindRippleWeight"); vertDataIt; ++vertDataIt, ++branchDataInd)
				{
					vertsData[branchDataInd] = red::String(vertDataIt.GetValue().AsChar()).Split(" ");
				}
				if (branchDataInd != 8)
				{
					return ImportResult::Error("Probably incorrect wind type is exported in .xml. Note, that we only support \"SDK\" wind type from Modeler.");
				}
				for (red::Uint32 i = 0; i < vertsData[0].Size(); ++i)
				{
					//ExtraData1.xyz = branchPos, ExtraData1.w = windBranchWeight, ExtraData2.xyz = branchDir, ExtraData2.w = windRippleWeight like in meshPacking.cpp
					m_vertBranchData.PushBack(Float(atof(vertsData[0][i].AsChar())));
					m_vertBranchData.PushBack(Float(atof(vertsData[1][i].AsChar())));
					m_vertBranchData.PushBack(Float(atof(vertsData[3][i].AsChar())));
					m_vertBranchData.PushBack(Float(atof(vertsData[4][i].AsChar())));
					m_vertBranchData.PushBack(Float(atof(vertsData[5][i].AsChar())));
					m_vertBranchData.PushBack(Float(atof(vertsData[2][i].AsChar())));
					m_vertBranchData.PushBack(Float(atof(vertsData[7][i].AsChar())));

					m_vertWindBranchData.PushBack(Vector4(0, 0, 0, Float(atof(vertsData[6][i].AsChar()))));
				}
			}

		}

		xml::NodeIterator trianglesIt( objIt, "Triangles" );
		if ( !trianglesIt )
		{
			return ImportResult::Error( "LOD %d contains no Triangles xml node", lodIdx );
		}

		//build vertex data based on indices
		for ( trianglesIt; trianglesIt; ++trianglesIt)
		{
			//map storing index of final vertex (in the Index buffer) based on unique pairs of point index and vertex index in .xml
			red::HashMap<std::pair<red::Uint16, red::Uint16>, red::Uint16> verticesMap;

			red::DynArray<red::Uint16> pointIndices;
			red::DynArray<red::Uint16> vertexIndices;

			if (xml::NodeIterator pointIt = xml::NodeIterator(trianglesIt, "PointIndices"))
			{
				red::DynArray<red::String> pointIndicesData = red::String(pointIt.GetValue().AsChar()).Split(" ");
				for (red::Uint32 i = 0; i < pointIndicesData.Size(); ++i)
					pointIndices.PushBack(red::Uint16(atoi(pointIndicesData[i].AsChar())));
			}

			if (xml::NodeIterator vertIt = xml::NodeIterator(trianglesIt, "VertexIndices"))
			{
				red::DynArray<red::String> vertIndicesData = red::String(vertIt.GetValue().AsChar()).Split(" ");
				for (red::Uint32 i = 0; i < vertIndicesData.Size(); ++i)
					vertexIndices.PushBack(red::Uint16(atoi(vertIndicesData[i].AsChar())));
			}

			Uint32 matId = 0;
			if (xml::AttributeIterator matIdIt = xml::AttributeIterator(trianglesIt, "Material"))
			{
				matId = matIdIt.GetValue().ToInt32();
			}

			//get number of unique vertices
			red::Uint16 verticesCount = 0;
			red::Uint32 horizontalBillboardsCount = 0;
			m_containsTopBillboard = false;
			if ( thisLodIsABillboard )
			{
				horizontalBillboardsCount = xml::AttributeIterator( trianglesIt, "Count" ).GetValue().ToInt32() / 4; //Assuming that each billboard consists of 4 triangles
				//Below we need to subtract 1 if top billboard exists
			}

			for (red::Uint16 i = 0; i < pointIndices.Size(); ++i)
			{
				std::pair<red::Uint16, red::Uint16> pointAndVertIndices = std::make_pair(pointIndices[i], vertexIndices[i]);
				auto dataIt = verticesMap.Find(pointAndVertIndices);
				if (dataIt == verticesMap.End())//not found
				{
					verticesMap[pointAndVertIndices] = verticesCount;
					verticesCount++;
				}
				// dumb check if there's a triangle with normal pointing upwards
				if ( thisLodIsABillboard && !m_containsTopBillboard && m_vertNormalsData[vertexIndices[i]].Z > 0.5f )
				{
					horizontalBillboardsCount--;
					m_containsTopBillboard = true;
				}
			}
			verticesMap.Clear();

			Int32 chunkVerticesCount = 0, chunkIndicesCount = 0;
			//map storing index of final vertex (in the Index buffer) based on unique pairs of point index and vertex index in .xml
			red::HashMap<std::pair<red::Uint16, red::Uint16>, red::Uint16> chunkVerticesMap;

			Int32 seamsVerticesCount = 0, seamsIndicesCount = 0;
			red::HashMap<std::pair<red::Uint16, red::Uint16>, red::Uint16> seamsVerticesMap;

			//Calculate separate vertices and indices counts if there's a "seam" object in the chunk.
			//Later, below we insert a separate mesh for the "seams" chunk
			for (red::Int32 i = 2; i < static_cast<Int32>(pointIndices.Size()); i += 3)
			{
				Bool replaceBlendValues = false;
				Bool triangleBelongsToSeams = DoesTriangleBelongToSeams(m_vertBlendData[vertexIndices[i-2]], m_vertBlendData[vertexIndices[i-1]], m_vertBlendData[vertexIndices[i]], replaceBlendValues);
				if (triangleBelongsToSeams)
				{
					seamsIndicesCount += 3;
					for (red::Int32 triangleInd = i; triangleInd > i - 3; --triangleInd)
					{
						std::pair<red::Uint16, red::Uint16> pointAndVertIndices = std::make_pair(pointIndices[triangleInd], vertexIndices[triangleInd]);
						auto dataIt = seamsVerticesMap.Find(pointAndVertIndices);
						if (dataIt == seamsVerticesMap.End())//not found
						{
							seamsVerticesMap[pointAndVertIndices] = seamsVerticesCount;
							seamsVerticesCount++;
						}
					}
				}
				else
				{
					chunkIndicesCount += 3;
					for (red::Int32 triangleInd = i; triangleInd > i - 3; --triangleInd)
					{
						std::pair<red::Uint16, red::Uint16> pointAndVertIndices = std::make_pair(pointIndices[triangleInd], vertexIndices[triangleInd]);
						auto dataIt = chunkVerticesMap.Find(pointAndVertIndices);
						if (dataIt == chunkVerticesMap.End())//not found
						{
							chunkVerticesMap[pointAndVertIndices] = chunkVerticesCount;
							chunkVerticesCount++;
						}
					}
				}
			}

			//import chunk and its extracted seams chunk if exists
			{
				ImportResult result = ImportChunk(outputMeshData, meshBuildData, meshViewData, importOptions, chunkVerticesCount, chunkIndicesCount, matId, lodIdx, pointIndices, vertexIndices, false, horizontalBillboardsCount );
				if (!result.IsSuccessful())
				{
					return result;
				}

				if (seamsVerticesCount > 0)
				{
					result = ImportChunk(outputMeshData, meshBuildData, meshViewData, importOptions, seamsVerticesCount, seamsIndicesCount, matId, lodIdx, pointIndices, vertexIndices, true, horizontalBillboardsCount );
					if (!result.IsSuccessful())
					{
						return result;
					}
				}
			}
		}

	}

	if ( !importOptions->m_importNewChunkFlags )
	{
		Bool copyFlagsRes = CopyChunkFlags( meshViewData, meshBuildData );

		if ( !copyFlagsRes )
		{
			String msg = String::Printf( "[WARNPOPUP] REVISIT MESH FLAGS!\n"
										 "On mesh import, could not copy all chunk flags,\n"
										 "please revisit flags to check if they are correct.\n"
										 "[%s]", outputMeshData->GetPath().ToString().AsChar() );
			RED_LOG_INFO( msg.AsChar() );
			RED_LOG_WARNING( msg.AsChar() );
			return ImportResult::Success( nullptr, msg );
		}
	}

	return ImportResult::Success( nullptr );
		
}

ImportResult XMLSpeedTreeMeshImporter::ImportChunk(THandle< CMesh >& outputMeshData, mesh::DataBuilder& meshBuildData, mesh::DataView& meshViewData, const THandle< XMLSpeedTreeResourceImportOptions >& importOptions, const Int32 verticesCount, const Int32 indicesCount, Uint32 matId, Int32 lodIdx, red::DynArray<red::Uint16>& pointIndices,
	red::DynArray<red::Uint16>& vertexIndices, Bool nowImportingSeamsChunk, Int32 horizontalBillboardsCount )
{
	auto& newChunk = meshBuildData.m_chunks.EmplaceBack();
	newChunk.m_lodMask = 1 << lodIdx;
	newChunk.m_vertexType = MVT_SpeedTreeMesh;

	Bool renderInCascades = nowImportingSeamsChunk == false;
	newChunk.m_flags.m_renderInShadows = renderInCascades;

	newChunk.m_streams.Resize(11);
	newChunk.m_streams[0].m_type = MST_Position_3F;
	newChunk.m_streams[1].m_type = MST_Normal_3F;
	newChunk.m_streams[2].m_type = MST_Tangent_3F;
	newChunk.m_streams[3].m_type = MST_Binormal_3F;
	newChunk.m_streams[4].m_type = MST_TexCoord0_2F;
	newChunk.m_streams[5].m_type = MST_TexCoord1_2F;
	newChunk.m_streams[6].m_type = MST_Color_U32;
	newChunk.m_streams[7].m_type = MST_Index_U16;
	newChunk.m_streams[8].m_type = MST_WindBranchData_4F;
	newChunk.m_streams[9].m_type = MST_BranchData_7F;
	newChunk.m_streams[10].m_type = MST_FoliageBoneId_I16;

	mesh::StreamWriter<Vector3> positions(newChunk.m_streams[0].m_data, verticesCount);
	mesh::StreamWriter<Vector3> normals(newChunk.m_streams[1].m_data, verticesCount);
	mesh::StreamWriter<Vector3> tangents(newChunk.m_streams[2].m_data, verticesCount);
	mesh::StreamWriter<Vector3> binormals(newChunk.m_streams[3].m_data, verticesCount);
	mesh::StreamWriter<Vector2> uv0(newChunk.m_streams[4].m_data, verticesCount);
	mesh::StreamWriter<Vector2> uv1(newChunk.m_streams[5].m_data, verticesCount);
	mesh::StreamWriter<Uint32> colors(newChunk.m_streams[6].m_data, verticesCount);
	mesh::StreamWriter<Uint16> indices(newChunk.m_streams[7].m_data, indicesCount);
	mesh::StreamWriter<Vector4> windBranchData(newChunk.m_streams[8].m_data, verticesCount);
	mesh::StreamWriter<Float> branchData(newChunk.m_streams[9].m_data, 7 * verticesCount);//todo increase MST_BranchData_7F size to 8f and send all of the data to extradata1  and 2
	mesh::StreamWriter<Int16> boneId(newChunk.m_streams[10].m_data, verticesCount);
	Int32 geometryType = -1;

	//map storing index of final vertex (in the Index buffer) based on unique pairs of point index and vertex index in .xml
	red::HashMap<std::pair<red::Uint16, red::Uint16>, red::Uint16> verticesMap;

	red::Uint16 nextInd = 0;
	for (red::Int32 i = 2; i < static_cast<Int32>(pointIndices.Size()); i += 3)
	{
		Bool replaceBlendValues = false;
		Bool triangleBelongsToSeams = DoesTriangleBelongToSeams(m_vertBlendData[vertexIndices[i - 2]], m_vertBlendData[vertexIndices[i - 1]], m_vertBlendData[vertexIndices[i]], replaceBlendValues);
		//flip the winding of vertices (exported XML has frontFace CCW by default and we want CW)
		for (red::Int32 triangleInd = i; triangleInd > i - 3; --triangleInd)
		{
			Int32 pointInd = pointIndices[triangleInd];
			Int32 vertInd = vertexIndices[triangleInd];

			std::pair<red::Uint16, red::Uint16> pointAndVertIndices = std::make_pair(pointInd, vertInd);
			auto dataIt = verticesMap.Find(pointAndVertIndices);
			if ((!nowImportingSeamsChunk && !triangleBelongsToSeams) || (nowImportingSeamsChunk && triangleBelongsToSeams))
			{
				if (dataIt == verticesMap.End())//not found
				{
					verticesMap[pointAndVertIndices] = nextInd;
					indices.Write(nextInd);
					nextInd++;

					Vector3 pos = m_pointsData[pointInd];
					positions.Write(pos);

					Vector3 normal = m_vertNormalsData[vertInd];
					normals.Write(normal);

					Vector3 tangent = m_vertTangentsData[vertInd];
					tangents.Write(tangent);

					Vector3 binormal = m_vertBinormalsData[vertInd];
					binormals.Write(binormal);

					uv0.Write(m_vertTexCoordsData[vertInd]);

					Vector3 colorMultipliedByAo = m_vertColorData[vertInd] * m_vertAoData[vertInd];
					Color color;
					color.R = Clamp<Uint8>(static_cast<Uint8>(colorMultipliedByAo.X * 255.f + 0.5f), 0, 255);
					color.G = Clamp<Uint8>(static_cast<Uint8>(colorMultipliedByAo.Y * 255.f + 0.5f), 0, 255);
					color.B = Clamp<Uint8>(static_cast<Uint8>(colorMultipliedByAo.Z * 255.f + 0.5f), 0, 255);
					Float blendVal = replaceBlendValues ? 1.0f : m_vertBlendData[vertInd];
					color.A = Clamp<Uint8>(static_cast<Uint8>(blendVal * 255.f + 0.5f), 0, 255 );
					colors.Write( color.ToUint32() );

					Int32 vertGeomType = m_vertGeometryTypeData[vertInd];
					//TODO: for now there's no reason to create a separate chunk per geometry type in vertex stream. For example fronds and leaves are 2 geometry types but 
					//can still be handled as one chunk using the same material (twosided variant). This might change when wind data arrives.
					/*if (geometryType == -1)
					{
						geometryType = vertGeomType;
					}
					else if (geometryType != vertGeomType)
					{
						return ImportResult::Error("Chunk %d in Lod %d with material id: %hs contains more than one geometry type: %d %d",
							meshBuildData.m_chunks.Size() - 1, lodIdx, materialIndex.AsChar(), geometryType, vertGeomType);
					}*/

					if (m_xmlHasWindData)
					{
						windBranchData.Write(m_vertWindBranchData[vertInd]);

						for (Uint32 branchDataInd = 0; branchDataInd < 7; branchDataInd++)
						{
							branchData.Write(m_vertBranchData[vertInd * 7 + branchDataInd]);
						}
					}

					if (!m_boneId.Empty())
					{
						boneId.Write(m_boneId[vertInd]);
					}
				}
				else
				{
					indices.Write(dataIt.Value());
				}
			}
		}
	}

	Uint32 numWrittenElements[] = {
		positions.GetNumWrittenElements(),
		normals.GetNumWrittenElements(),
		tangents.GetNumWrittenElements(),
		binormals.GetNumWrittenElements(),
		uv0.GetNumWrittenElements(),
		uv1.GetNumWrittenElements(),
		colors.GetNumWrittenElements(),
		indices.GetNumWrittenElements(),
		windBranchData.GetNumWrittenElements(),
		branchData.GetNumWrittenElements(),
		boneId.GetNumWrittenElements()
	};

	// Remove unused streams
	for (Uint32 i = newChunk.m_streams.Size() - 1; i > 0; --i)
	{
		if (numWrittenElements[i] == 0)
		{
			newChunk.m_streams.RemoveAt(i);
		}
	}

	//assign material to new chunk
	{
		CName outMaterialNameIndex;
		ImportResult result = CreateSpeedTreeMaterial(outputMeshData, meshViewData, importOptions, matId, nowImportingSeamsChunk, horizontalBillboardsCount, lodIdx, outMaterialNameIndex );
		if (!result.IsSuccessful())
		{
			return result;
		}
		newChunk.m_materials.PushBack(outMaterialNameIndex);
	}

	return ImportResult::Success(nullptr);
}

ImportResult XMLSpeedTreeMeshImporter::ImportCollision(THandle< CMesh >& outputMeshData, const THandle< XMLSpeedTreeResourceImportOptions >& importOptions )
{
	if ( !importOptions->m_importCollision )
	{
		return ImportResult::Success( nullptr );
	}

	using Collider = THandle<physics::ICollider>;
	physics::SystemBodyBuildData build_data;
	build_data.m_name = RED_NAME_CONSTEXPR("Speed Tree Collision");
	build_data.m_localToModel = Transform::IDENTITY();
	build_data.m_params.m_simulationType = physics::SimulationType::Static;

	Int32 objectsImported = 0;

	for (xml::NodeIterator objectsIt(*m_xmlDoc, m_xmlDoc->GetRootNode(), "CollisionObjects"); objectsIt; ++objectsIt)
	{
		for (xml::NodeIterator objIt(objectsIt, "CollisionObject"); objIt; ++objIt)
		{
			objectsImported++;
			
			xml::StaticString collisionType;
			if (xml::AttributeIterator typeIt = xml::AttributeIterator(objIt, "Type"))
				collisionType = typeIt.GetValue();

			Vector3 center1, center2;
			Float radius;

			if (xml::AttributeIterator posIt = xml::AttributeIterator(objIt, "Pos1X"))
				center1.X = posIt.GetValue().ToFloat();
			if (xml::AttributeIterator posIt = xml::AttributeIterator(objIt, "Pos1Y"))
				center1.Y = posIt.GetValue().ToFloat();
			if (xml::AttributeIterator posIt = xml::AttributeIterator(objIt, "Pos1Z"))
				center1.Z = posIt.GetValue().ToFloat();

			if (xml::AttributeIterator posIt = xml::AttributeIterator(objIt, "Pos2X"))
				center2.X = posIt.GetValue().ToFloat();
			if (xml::AttributeIterator posIt = xml::AttributeIterator(objIt, "Pos2Y"))
				center2.Y = posIt.GetValue().ToFloat();
			if (xml::AttributeIterator posIt = xml::AttributeIterator(objIt, "Pos2Z"))
				center2.Z = posIt.GetValue().ToFloat();

			if (xml::AttributeIterator radiusIt = xml::AttributeIterator(objIt, "Radius"))
				radius = radiusIt.GetValue().ToFloat();

			red::String collisionFilter = "";
			red::String physMat = "";
			if ( xml::AttributeIterator userDataIt = xml::AttributeIterator( *m_xmlDoc, m_xmlDoc->GetRootNode(), "UserData" ) )
			{
				Uint32 keyInd = -1;

				red::DynArray<String> data = red::String( userDataIt.GetValue().AsChar() ).Split( ";" );

				for ( const auto& entry : data )
				{
					entry.IndexOf( "=", keyInd );
					if ( keyInd != -1 )
					{
						String key = entry.LeftString( keyInd );
						key.RemoveWhiteSpaces();
						String value = entry.RightString( entry.Length() - keyInd - 1 );
						if ( key.Empty() || value.Empty() )
						{
							return ImportResult::Error( "Empty value or key in User data %hs. Format is key=value;", userDataIt.GetValue().AsChar() );
						}
						//remove spaces at the beginning and at the end
						Int32 startValInd = 0;
						Int32 endValInd = ( (Int32)value.Length() ) - 1;
						auto isWhiteChar = []( const char& c ) {	return ( c == ' ' || c == '\t' || c == '\n' || c == '\r' );	};
						while ( startValInd < (Int32)value.Length() && isWhiteChar( value[startValInd] ) )
						{
							startValInd++;
						}
						while ( endValInd >= 0 && isWhiteChar( value[endValInd] ) )
						{
							endValInd--;
						}
						value = value.MidString( startValInd, endValInd - startValInd + 1 );
						if ( value.Empty() )
						{
							return ImportResult::Error( "Empty value for key %hs in User data. Format is key=value;", key.AsChar() );
						}

						//Check key and load its value
						if ( key.ToLower() == "collisionfilter" )
						{
							collisionFilter = value;
						}
						else if ( key.ToLower() == "physmat" )
						{
							physMat = value;
						}
					}
					else
					{
						return ImportResult::Error( "Incorrect format of the string in User data. Format is key=value; " );
					}
				}
			}

			Collider collider;
			if (collisionType == "Sphere")
			{
				collider = physics::ColliderSphere::Create(radius);
				collider->m_localToBody = Transform(center1);
			}
			else if(collisionType == "Capsule")
			{
				const auto height = center2.DistanceTo(center1);
				collider = physics::ColliderCapsule::Create(radius, height);

				auto dir = (center2 - center1).Normalized();
				auto axis = Vector3::EZ().Cross(dir);
				auto angle = MAsin_safe(axis.Normalize());

				Quaternion q = Quaternion::IDENTITY();
				if (angle > std::numeric_limits<SpeedTree::st_float32>::epsilon())
				{
					q.SetAxisAngle(axis, angle);
				}

				Vector3 translation = center1 + dir * height * 0.5f;
				collider->m_localToBody = Transform(translation, q);
			}
			else
			{
				return ImportResult::Error("XMLSpeedTreeMeshImporter::ImportCollision - Collision type " + red::String(collisionType.AsChar()) + " is not supported");
			}

			//setup collider's default material
			if ( collisionFilter.Empty() )
			{
				if ( collisionType == "Sphere" )
				{
					physMat = "wood_crown.physmat";
				}
				else if ( collisionType == "Capsule" )
				{
					physMat = "wood_tree.physmat";
				}
			}
			collider->m_material = RED_NAME( physMat );

			//Setup collider's filter preset
			physics::FilterDataPtr filterData = CreateHandle< physics::FilterData >();
			if ( collisionFilter.Empty() )
			{
				if ( collisionType == "Sphere" )
				{
					collisionFilter = "Foliage Crown";
				}
				else if ( collisionType == "Capsule" )
				{
					collisionFilter = "Foliage Trunk";
				}
			}
			filterData->ApplyPreset( RED_NAME( collisionFilter ) );
			collider->m_filterData = filterData;

			build_data.m_collisionShapes.PushBack(collider);
		}
	}

	if (objectsImported > 0)
	{
		auto system_resource = CreateHandle<physics::SystemResource>();
		auto system_body = physics::SystemBody::Create(build_data);
		system_resource->AttachObject(system_body);

		auto physics_param = outputMeshData->CreateParameter<mesh::MeshParamPhysics>();
		physics_param->SetPhysicsData(system_resource);
	}

	return ImportResult::Success(nullptr);
}

ImportResult XMLSpeedTreeMeshImporter::ImportWind(THandle< CMesh >& outputMeshData)
{
	SpeedTree::SConfigGames8 windConfig;

	struct XmlAttribute
	{
		enum WindXmlAttribute
		{
			//main Wind attributes
			GlobalHeight,
			FullBranchMove,
			FullRipple,
			BranchStiffnessAmount,
			BranchStiffnessUp,
			BranchStretch,
			Shimmer,
			IsGrass,
			//Common attributes
			StrengthResponse,
			DirectionResponse,
			GustFrequency,
			GustStrengthMin,
			GustStrengthMax,
			GustDurationMin,
			GustDurationMax,
			GustRiseScalar,
			GustFallScalar,
			//Shared/Branch/Ripple attributes
			TurbulenceIndependence,
			//Curve attributes,
			CurveName,
			AttrCount
		};

		WindXmlAttribute attr;

		XmlAttribute(WindXmlAttribute attr) : attr(attr) {};

		const AnsiChar* toString() const
		{
			switch (attr)
			{
				//main Wind attributes
			case GlobalHeight:
				return "GlobalHeight";
			case FullBranchMove:
				return "FullBranchMove";
			case FullRipple:
				return "FullRipple";
			case BranchStiffnessAmount:
				return "BranchStiffnessAmount";
			case BranchStiffnessUp:
				return "BranchStiffnessUp";
			case BranchStretch:
				return "BranchStretch";
			case Shimmer:
				return "Shimmer";
			case IsGrass:
				return "IsGrass";
				//Common attributes
			case StrengthResponse:
				return "StrengthResponse";
			case DirectionResponse:
				return "DirectionResponse";
			case GustFrequency:
				return "GustFrequency";
			case GustStrengthMin:
				return "GustStrengthMin";
			case GustStrengthMax:
				return "GustStrengthMax";
			case GustDurationMin:
				return "GustDurationMin";
			case GustDurationMax:
				return "GustDurationMax";
			case GustRiseScalar:
				return "GustRiseScalar";
			case GustFallScalar:
				return "GustFallScalar";
				//Shared/Branch/Ripple attributes
			case TurbulenceIndependence:
				return "TurbulenceIndependence";
				//Curve attributes:
			case CurveName:
				return "Name";
			}
			return nullptr;
		}
	};

	const XmlAttribute windMainAttr[] = { XmlAttribute::GlobalHeight, XmlAttribute::FullBranchMove, XmlAttribute::FullRipple, XmlAttribute::BranchStiffnessAmount, 
		XmlAttribute::BranchStiffnessUp, XmlAttribute::BranchStretch, XmlAttribute::Shimmer, XmlAttribute::IsGrass };
	const Uint32 windMainAttrCount = sizeof(windMainAttr) / sizeof(XmlAttribute);

	const XmlAttribute windCommonAttr[] = { XmlAttribute::StrengthResponse, XmlAttribute::DirectionResponse, XmlAttribute::GustFrequency, XmlAttribute::GustStrengthMin,
		XmlAttribute::GustStrengthMax, XmlAttribute::GustDurationMin, XmlAttribute::GustDurationMax, XmlAttribute::GustRiseScalar, XmlAttribute::GustFallScalar };
	const Uint32 windCommonAttrCount = sizeof(windCommonAttr) / sizeof(XmlAttribute);

	const XmlAttribute windSharedAttr[] = { XmlAttribute::TurbulenceIndependence };
	const Uint32 windSharedAttrCount = sizeof(windSharedAttr) / sizeof(XmlAttribute);

	const XmlAttribute windCurveAttr[] = { XmlAttribute::CurveName };
	const Uint32 windCurveAttrCount = sizeof(windCurveAttr) / sizeof(XmlAttribute);

	xml::StaticString xmlAttrValues[XmlAttribute::WindXmlAttribute::AttrCount] = {};

	ImportResult result = ImportResult::Success(nullptr);

	if(xml::NodeIterator windIt = xml::NodeIterator(*m_xmlDoc, m_xmlDoc->GetRootNode(), "Wind"))
	{
		//load main Wind attributes from xml
		{
			result = LoadAttributeValuesFromXml(windIt, windMainAttr, xmlAttrValues, windMainAttrCount);
			if (!result.IsSuccessful()) { return result; }
			windConfig.m_fGlobalHeight = xmlAttrValues[XmlAttribute::GlobalHeight].ToFloat();
			windConfig.m_fFullBranchMove = xmlAttrValues[XmlAttribute::FullBranchMove].ToFloat();
			windConfig.m_fFullRipple = xmlAttrValues[XmlAttribute::FullRipple].ToFloat();
			windConfig.m_fBranchStiffnessAmount = xmlAttrValues[XmlAttribute::BranchStiffnessAmount].ToFloat();
			windConfig.m_fBranchStiffnessUp = xmlAttrValues[XmlAttribute::BranchStiffnessUp].ToFloat();
			windConfig.m_fBranchStretch = xmlAttrValues[XmlAttribute::BranchStretch].ToFloat();
			windConfig.m_fShimmer = xmlAttrValues[XmlAttribute::Shimmer].ToFloat();
			windConfig.m_fIsGrass = xmlAttrValues[XmlAttribute::IsGrass].ToFloat();
			m_isGrassWind = ( windConfig.m_fIsGrass > 0.5f );
		}

		//load Common attributes from xml
		{
			xml::NodeIterator innerNodeIt = xml::NodeIterator(windIt, "Common");
			if (!innerNodeIt)
			{
				return ImportResult::Error("There's no Wind Common data in xml");
			}
			result = LoadAttributeValuesFromXml(innerNodeIt, windCommonAttr, xmlAttrValues, windCommonAttrCount);
			if (!result.IsSuccessful()) { return result; }
			windConfig.m_fStrengthResponse = xmlAttrValues[XmlAttribute::StrengthResponse].ToFloat();
			windConfig.m_fDirectionResponse = xmlAttrValues[XmlAttribute::DirectionResponse].ToFloat();
			windConfig.m_fGustFrequency = xmlAttrValues[XmlAttribute::GustFrequency].ToFloat();
			windConfig.m_fGustStrengthMin = xmlAttrValues[XmlAttribute::GustStrengthMin].ToFloat();
			windConfig.m_fGustStrengthMax = xmlAttrValues[XmlAttribute::GustStrengthMax].ToFloat();
			windConfig.m_fGustDurationMin = xmlAttrValues[XmlAttribute::GustDurationMin].ToFloat();
			windConfig.m_fGustDurationMax = xmlAttrValues[XmlAttribute::GustDurationMax].ToFloat();
			windConfig.m_fGustRiseScalar = xmlAttrValues[XmlAttribute::GustRiseScalar].ToFloat();
			windConfig.m_fGustFallScalar = xmlAttrValues[XmlAttribute::GustFallScalar].ToFloat();
		}

		//load Shared data from xml
		{
			xml::NodeIterator innerNodeIt = xml::NodeIterator(windIt, "Shared");
			if (!innerNodeIt)
			{
				return ImportResult::Error("There's no Wind Shared data in xml");
			}
			result = LoadAttributeValuesFromXml(innerNodeIt, windSharedAttr, xmlAttrValues, windSharedAttrCount);
			if (!result.IsSuccessful()) { return result; }
			windConfig.m_sShared.m_fTurbulenceIndependence = xmlAttrValues[XmlAttribute::TurbulenceIndependence].ToFloat();
			//load curves
			for (xml::NodeIterator curveIt(innerNodeIt, "Curve"); curveIt; ++curveIt)
			{
				result = LoadAttributeValuesFromXml(curveIt, windCurveAttr, xmlAttrValues, windCurveAttrCount);
				if (!result.IsSuccessful()) { return result; }
				red::String curveName = xmlAttrValues[XmlAttribute::CurveName].AsChar();

				//load curve points
				red::DynArray<red::String> curvePoints = red::String(curveIt.GetValue().AsChar()).Split(" ");
				if (curvePoints.Size() > SpeedTree::SConfigGames8::c_nNumPointsInCurves)
				{
					return ImportResult::Error("Incorrect number of curvePoints: %d, expected: %d", curvePoints.Size(), SpeedTree::SConfigGames8::c_nNumPointsInCurves);
				}

				if (curveName == "Frequency")
				{
					for (Uint32 i = 0; i < curvePoints.Size(); ++i)
					{
						windConfig.m_sShared.m_afFrequency[i] = Float(atof(curvePoints[i].AsChar()));
					}
				}
				else if (curveName == "Bend")
				{
					for (Uint32 i = 0; i < curvePoints.Size(); ++i)
					{
						windConfig.m_sShared.m_afBend[i] = Float(atof(curvePoints[i].AsChar()));
					}
				}
				else if (curveName == "Turbulence")
				{
					for (Uint32 i = 0; i < curvePoints.Size(); ++i)
					{
						windConfig.m_sShared.m_afTurbulence[i] = Float(atof(curvePoints[i].AsChar()));
					}
				}
				else if (curveName == "Flexibility")
				{
					for (Uint32 i = 0; i < curvePoints.Size(); ++i)
					{
						windConfig.m_sShared.m_afFlexibility[i] = Float(atof(curvePoints[i].AsChar()));
					}
				}
			}
		}

		//load Branch data from xml
		{
			xml::NodeIterator innerNodeIt = xml::NodeIterator(windIt, "Branch");
			if (!innerNodeIt)
			{
				return ImportResult::Error("There's no Wind Branch data in xml");
			}
			result = LoadAttributeValuesFromXml(innerNodeIt, windSharedAttr, xmlAttrValues, windSharedAttrCount);
			if (!result.IsSuccessful()) { return result; }
			windConfig.m_sBranch.m_fTurbulenceIndependence = xmlAttrValues[XmlAttribute::TurbulenceIndependence].ToFloat();
			//load curves
			for (xml::NodeIterator curveIt(innerNodeIt, "Curve"); curveIt; ++curveIt)
			{
				result = LoadAttributeValuesFromXml(curveIt, windCurveAttr, xmlAttrValues, windCurveAttrCount);
				if (!result.IsSuccessful()) { return result; }
				red::String curveName = xmlAttrValues[XmlAttribute::CurveName].AsChar();

				//load curve points
				red::DynArray<red::String> curvePoints = red::String(curveIt.GetValue().AsChar()).Split(" ");
				if (curvePoints.Size() > SpeedTree::SConfigGames8::c_nNumPointsInCurves)
				{
					return ImportResult::Error("Incorrect number of curvePoints: %d, expected: %d", curvePoints.Size(), SpeedTree::SConfigGames8::c_nNumPointsInCurves);
				}

				if (curveName == "Frequency")
				{
					for (Uint32 i = 0; i < curvePoints.Size(); ++i)
					{
						windConfig.m_sBranch.m_afFrequency[i] = Float(atof(curvePoints[i].AsChar()));
					}
				}
				else if (curveName == "Bend")
				{
					for (Uint32 i = 0; i < curvePoints.Size(); ++i)
					{
						windConfig.m_sBranch.m_afBend[i] = Float(atof(curvePoints[i].AsChar()));
					}
				}
				else if (curveName == "Turbulence")
				{
					for (Uint32 i = 0; i < curvePoints.Size(); ++i)
					{
						windConfig.m_sBranch.m_afTurbulence[i] = Float(atof(curvePoints[i].AsChar()));
					}
				}
				else if (curveName == "Flexibility")
				{
					for (Uint32 i = 0; i < curvePoints.Size(); ++i)
					{
						windConfig.m_sBranch.m_afFlexibility[i] = Float(atof(curvePoints[i].AsChar()));
					}
				}
			}
		}

		//load Ripple data from xml
		{
			xml::NodeIterator innerNodeIt = xml::NodeIterator(windIt, "Ripple");
			if (!innerNodeIt)
			{
				return ImportResult::Error("There's no Wind Ripple data in xml");
			}
			result = LoadAttributeValuesFromXml(innerNodeIt, windSharedAttr, xmlAttrValues, windSharedAttrCount);
			if (!result.IsSuccessful()) { return result; }
			windConfig.m_sRipple.m_fTurbulenceIndependence = xmlAttrValues[XmlAttribute::TurbulenceIndependence].ToFloat();
			//load curves
			for (xml::NodeIterator curveIt(innerNodeIt, "Curve"); curveIt; ++curveIt)
			{
				result = LoadAttributeValuesFromXml(curveIt, windCurveAttr, xmlAttrValues, windCurveAttrCount);
				if (!result.IsSuccessful()) { return result; }
				red::String curveName = xmlAttrValues[XmlAttribute::CurveName].AsChar();

				//load curve points
				red::DynArray<red::String> curvePoints = red::String(curveIt.GetValue().AsChar()).Split(" ");
				if (curvePoints.Size() > SpeedTree::SConfigGames8::c_nNumPointsInCurves)
				{
					return ImportResult::Error("Incorrect number of curvePoints: %d, expected: %d", curvePoints.Size(), SpeedTree::SConfigGames8::c_nNumPointsInCurves);
				}

				if (curveName == "Bend")
				{
					for (Uint32 i = 0; i < curvePoints.Size(); ++i)
					{
						windConfig.m_sRipple.m_afBend[i] = Float(atof(curvePoints[i].AsChar()));
					}
				}
				else if (curveName == "TurbulenceFrequency")
				{
					for (Uint32 i = 0; i < curvePoints.Size(); ++i)
					{
						windConfig.m_sRipple.m_afTurbulenceFrequency[i] = Float(atof(curvePoints[i].AsChar()));
					}
				}
				else if (curveName == "Turbulence")
				{
					for (Uint32 i = 0; i < curvePoints.Size(); ++i)
					{
						windConfig.m_sRipple.m_afTurbulence[i] = Float(atof(curvePoints[i].AsChar()));
					}
				}
			}
		}

		//additional data outside of Wind xml data node
		windConfig.m_vBoundingBoxMin = float3(m_boundingBoxMin.X, m_boundingBoxMin.Y, m_boundingBoxMin.Z);
		windConfig.m_vBoundingBoxMax = float3(m_boundingBoxMax.X, m_boundingBoxMax.Y, m_boundingBoxMax.Z);

		auto parameter = outputMeshData->CreateParameter<mesh::MeshParamSpeedTreeWind>();
		parameter->m_speedTreeWind.SetParams(windConfig);
		//todo maybe setup some wind type additionally somehow? options like isGrass as shader define?
	}

	return result;
}

ImportResult XMLSpeedTreeMeshImporter::ImportUserData()
{
	m_userDataFoliageProfile = "";
	m_userDataLodFar = -1.0f;
	m_userDataLodNear = -1.0f;
	m_userDataLodBillboard = -1.0f;
	for ( Uint32 i = 0; i < MAX_BONES_IN_CHAIN; ++i )
	{
		m_userDataBoneMass[i] = 0.0f;
	}
	m_userDataForceTerrainBlend = YES_IF_GRASS_WIND;
	m_userDataTwosidedFlipNormals = 1.0f;

	if (xml::AttributeIterator userDataIt = xml::AttributeIterator(*m_xmlDoc, m_xmlDoc->GetRootNode(), "UserData"))
	{
		Uint32 keyInd = -1;

		red::DynArray<String> data = red::String( userDataIt.GetValue().AsChar() ).Split( ";" );

		for(const auto& entry : data )
		{
			entry.IndexOf( "=", keyInd );
			if ( keyInd != -1 )
			{
				String key = entry.LeftString( keyInd );
				key.RemoveWhiteSpaces();
				String value = entry.RightString( entry.Length() - keyInd -1 );
				if ( key.Empty() || value.Empty() )
				{
					return ImportResult::Error( "Empty value or key in User data %hs. Format is key=value;", userDataIt.GetValue().AsChar() );
				}
				//remove spaces at the beginning and at the end
				Int32 startValInd = 0;
				Int32 endValInd = ((Int32)value.Length()) - 1;
				auto isWhiteChar = [](const char& c){	return ( c == ' ' || c == '\t' || c == '\n' || c == '\r' );	};
				while ( startValInd < (Int32) value.Length() && isWhiteChar( value[startValInd] ) )
				{
					startValInd++;
				}
				while ( endValInd >= 0 && isWhiteChar( value[endValInd] ) )
				{
					endValInd--;
				}
				value = value.MidString( startValInd, endValInd - startValInd + 1 );
				if ( value.Empty() )
				{
					return ImportResult::Error( "Empty value for key %hs in User data. Format is key=value;", key.AsChar() );
				}

				//Check key and load its value
				if ( key.ToLower() == "fp" )
				{
					m_userDataFoliageProfile = "base\\environment\\vegetation\\foliage_profiles\\" + value + ".fp";
				}
				else if ( key.ToLower() == "lod" )
				{
					red::DynArray<String> lods = red::String( value ).Split( "," );
					for ( auto& str : lods )
					{
						str.RemoveWhiteSpaces();
					}
					if ( lods.Size() == 3 )
					{
						const AnsiChar* lodNear = lods[0].AsChar();
						const AnsiChar* lodFar = lods[1].AsChar();
						const AnsiChar* lodBillboard = lods[2].AsChar();
						if ( !GParseFloat( lodNear, m_userDataLodNear ) || !GParseFloat( lodFar, m_userDataLodFar ) || !GParseFloat( lodBillboard, m_userDataLodBillboard ) )
						{
							return ImportResult::Error( "Incorrect value in lod userData." );
						}
					}
					else
					{
						return ImportResult::Error( "There must be 3 LOD values: start distance for first change of LOD, start distance for last non billboard LOD, start distance for Billboard LOD." );
					}
				}
				else if ( key.ToLower() == "bonemass" )
				{
					red::DynArray<String> boneLevelMass = red::String( value ).Split( "," );
					for ( auto& str : boneLevelMass )
					{
						str.RemoveWhiteSpaces();
					}
					if ( boneLevelMass.Size() == 4 )
					{
						const AnsiChar* level0Mass = boneLevelMass[0].AsChar();
						const AnsiChar* level1Mass = boneLevelMass[1].AsChar();
						const AnsiChar* level2Mass = boneLevelMass[2].AsChar();
						const AnsiChar* level3Mass = boneLevelMass[3].AsChar();
						if ( !GParseFloat( level0Mass, m_userDataBoneMass[0] ) || !GParseFloat( level1Mass, m_userDataBoneMass[1] ) || !GParseFloat( level2Mass, m_userDataBoneMass[2] ) || 
							 !GParseFloat( level3Mass, m_userDataBoneMass[3] ) )
						{
							return ImportResult::Error( "Incorrect value in boneMass userData." );
						}
					}
					else
					{
						return ImportResult::Error( "There must be masses defined for all bone hierarchy levels: %d. Starts from left with the root bones level mass.", MAX_BONES_IN_CHAIN );
					}
				}
				else if ( key.ToLower() == "terrainblend" )
				{
					const AnsiChar* terrainBlend = value.AsChar();
					Int32 forceTerrainBlendVal = 0;
					if ( !GParseInt32( terrainBlend, forceTerrainBlendVal ) || ( forceTerrainBlendVal != 0 && forceTerrainBlendVal != 1 ) )
					{
						return ImportResult::Error( "Incorrect value in terrainBlend userData." );
					}
					m_userDataForceTerrainBlend = forceTerrainBlendVal == 1 ? ALWAYS : NEVER;
				}
				else if ( key.ToLower() == "flipn" )
				{
					const AnsiChar* flipN = value.AsChar();
					if ( !GParseFloat( flipN, m_userDataTwosidedFlipNormals ) )
					{
						return ImportResult::Error( "Incorrect value in flipN userData." );
					}
				}
			}
			else
			{
				return ImportResult::Error( "Incorrect format of the string in User data. Format is key=value; " );
			}
		}
	}

	return ImportResult::Success(nullptr);
}

Bool XMLSpeedTreeMeshImporter::DoesTriangleBelongToSeams(Float vert0Blend, Float vert1Blend, Float vert2Blend, Bool & replaceBlendValues)
{
	Bool triangleBelongsToSeams = false;
	replaceBlendValues = false;
	//The "lumps" objects on a tree had 0 blend values for all triangles' vertices and currently no blending occurs for them even in SpeedTree Modeler
	//Therefore we do not want to count them as "seams" and we replace zeros with ones (during writing to colors buffer) if 3 vertices of a triangle contain just zero blend value
	//because zero blend value would mean for us complete discard of whole triangle.
	if (vert0Blend < 0.0f + EPSILON && vert1Blend < 0.0f + EPSILON && vert2Blend < 0.0f + EPSILON)
	{
		replaceBlendValues = true;
	}
	else if (vert0Blend < 1.0f - EPSILON || vert1Blend < 1.0f - EPSILON || vert2Blend < 1.0f - EPSILON)
	{
		triangleBelongsToSeams = true;
	}
	return triangleBelongsToSeams;
}

template<class XmlAttr>
static ImportResult XMLSpeedTreeMeshImporter::LoadAttributeValuesFromXml(const xml::NodeIterator& nodeIt, const XmlAttr* attributesToLoad, xml::StaticString* outAttrValues, const Uint32 attrCount)
{
	for (Uint32 i = 0; i < attrCount; ++i)
	{
		const AnsiChar* attrXmlName = attributesToLoad[i].toString();
		xml::AttributeIterator attrIt(nodeIt, attrXmlName);
		if (!attrIt || attrXmlName == nullptr)
		{
			return ImportResult::Error("There's no xml attribute in Wind main attributes: %hs", attrXmlName);
		}
		outAttrValues[attributesToLoad[i].attr] = attrIt.GetValue();
	}

	return ImportResult::Success(nullptr);
}

ImportResult XMLSpeedTreeMeshImporter::DoImport(THandle< CResource > inputResource, const THandle<ResourceImportOptions>& options)
{
	// get mesh to import into
	THandle< CMesh > outputMeshData = Cast< CMesh >(inputResource);
	if (!outputMeshData)
	{
		return ImportResult::Error("Core: Unable to import mesh into something that is not a mesh");
	}
	mesh::DataView meshViewData(*outputMeshData);
	mesh::DataBuilder meshBuildData;

	Bool meshExistedBefore = meshViewData.GetNumChunks() != 0;
	m_setToBlendWithTerrain = false;

	THandle<XMLSpeedTreeResourceImportOptions> importOptions = Cast< XMLSpeedTreeResourceImportOptions >( options );
	RED_FATAL_ASSERT( importOptions != nullptr );

	const red::AbsolutePath sourceFilePath = importOptions->GetSourcePath();

	THandle<res::ResourceImportMetadata> importMetadata = CreateHandle<res::ResourceImportMetadata>();
	importMetadata->SetAssetPath(sourceFilePath.AsChar());
	importMetadata->SetIntermediatePath(sourceFilePath);
	res::ResourceMetadata::SetImportData(outputMeshData, importMetadata);

	auto file = GFileManager->CreateFileReader(sourceFilePath, FOF_AbsolutePath);
	if (!file)
		return ImportResult::Error("Couldn't find file: %hs.", sourceFilePath.AsChar());

	m_xmlDoc = red::MakeUniquePtr< xml::Document >(xml::Document::Create(file.Get()));
	if (m_xmlDoc == nullptr)
	{
		return ImportResult::Error("Couldn't load xml file: %hs.", sourceFilePath.AsChar());
	}

	m_xmlHasWindData = false;
	if (xml::NodeIterator windIt = xml::NodeIterator(*m_xmlDoc, m_xmlDoc->GetRootNode(), "Wind"))
	{
		m_xmlHasWindData = true;
	}

	m_isGrassWind = false;
	ImportResult result = ImportWind( outputMeshData );
	if ( !result.IsSuccessful() )
	{
		return result;
	}
	String successMsg = result.GetSuccessMessage();

	result = ImportUserData();
	if (!result.IsSuccessful())
	{
		return result;
	}
	successMsg += "\n" + result.GetSuccessMessage();

	result = ImportAllChunks(outputMeshData, meshBuildData, meshViewData, importOptions );
	if (!result.IsSuccessful())
	{
		return result;
	}
	successMsg += "\n" + result.GetSuccessMessage();

	result = ImportCollision( outputMeshData, importOptions );
	if (!result.IsSuccessful())
	{
		return result;
	}
	successMsg += "\n" + result.GetSuccessMessage();

	if ( !meshExistedBefore ) //don't overwrite for existing meshes (somebody could have changed the parameter manually in the mesh editor)
	{
		outputMeshData->SetRenderObjectType( ERenderObjectType::ROT_Foliage );
	}
	outputMeshData->SetBlendedWithTerrainHeightMap( m_setToBlendWithTerrain );

	m_xmlDoc = nullptr; //release	

	mesh::DataBuilder::FlushOptions flushOptions;
	flushOptions.m_rebuildLODDistances = true;
	meshBuildData.Flush(*outputMeshData, flushOptions ); // creates geometry
	
	meshViewData.RemoveUnusedMaterials();
	meshViewData.Apply(); // creates materials

	return ImportResult::Success(outputMeshData, successMsg );
}