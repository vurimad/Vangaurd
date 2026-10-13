#include "build.h"
#include "re_archive/reFileArchive.h"

#include "importMeshREX_Common.h"
#include "../../../common/worldStatic/include/instancedMeshNode.h"
#include "../../../common/renderData/include/visibilityWorldCommon.h"

#include "../../../backend/backendEngine/include/importer.h"
#include "../../../common/resourceMesh/include/meshDataViewRO.h"


RTTI_BEGIN_TYPE( CREXMeshResourceImportOptions );
RTTI_PARENT_TYPE( ResourceImportOptions );
RTTI_PROPERTY_CATEGORY( "Import switches" );
RTTI_PROPERTY( m_importChunks ).readOnly(); /// @todo DP: support chunks and skinning switches; when used make sure meshes tagged as "lod0Removed" are handled properly
RTTI_PROPERTY( m_importMaterials ).editable();
RTTI_PROPERTY( m_resetLODDistances ).editable();
RTTI_PROPERTY( m_importCollision ).editable();
RTTI_PROPERTY( m_cleanPhysicsImport ).editable();
RTTI_PROPERTY( m_importSkinning ).readOnly();
RTTI_PROPERTY( m_createSkinningLOD ).editable();
RTTI_PROPERTY( m_importRigs ).editable();
RTTI_PROPERTY( m_importSnapPoints ).editable();
RTTI_PROPERTY( m_importOcclusion ).editable();
RTTI_PROPERTY( m_importWorkspotOffsets ).editable();
RTTI_PROPERTY( m_importAsPhysicalClothMesh ).editable();
RTTI_PROPERTY( m_importAsGraphicalClothMesh ).editable();
RTTI_PROPERTY( m_matchChunksByOrder ).editable();
RTTI_PROPERTY_CATEGORY( "Extra options" );
RTTI_PROPERTY( m_importUV2AsMorphVertexData ).editable();
RTTI_PROPERTY( m_importLightConeData ).editable();
RTTI_PROPERTY( m_removeUnusedMaterials ).editable();
RTTI_PROPERTY( m_prepareMeshTopology ).editable();
RTTI_PROPERTY( m_extrudeBoundingBox ).editable();
RTTI_PROPERTY( m_boundingBoxExtrusionFactor ).editable().range( 0.f, 100.f );
RTTI_PROPERTY( m_occluderType ).editable();
RTTI_PROPERTY_CATEGORY( "" );
RTTI_PROPERTY ( m_overrideExistingRenderFlags ).editable().tooltip("If you're reimporting the mesh you might have some flags manually changed. Enable this option to override those flags with the ones set below. Otherwise the importer will try to keep the ones from existing mesh.");
RTTI_PROPERTY( m_renderFlags ).editable();
RTTI_PROPERTY( m_objectType ).editable();
RTTI_PROPERTY( m_forceFlagsFromReFile ).editable();
RTTI_END_TYPE();


CREXMeshResourceImportOptions::CREXMeshResourceImportOptions()
	: m_importChunks( true )
	, m_importMaterials( true )
	, m_resetLODDistances( false )
	, m_importCollision( true )
	, m_importSkinning( true )
    , m_createSkinningLOD( true )
	, m_importRigs( true )
	, m_importSnapPoints( true )
	, m_importOcclusion( true )
	, m_removeUnusedMaterials( true )
	, m_prepareMeshTopology( false )
	, m_extrudeBoundingBox( false )
	, m_boundingBoxExtrusionFactor( 0.f )
	, m_cleanPhysicsImport( false )
	, m_occluderType( vis::WorldOccluderType::Default )
	, m_importWorkspotOffsets( true )
	, m_importAsGraphicalClothMesh( false )
	, m_matchChunksByOrder( false )
	, m_importUV2AsMorphVertexData( false )
	, m_objectType( ERenderObjectType::ROT_Static )
	, m_overrideExistingRenderFlags( false )
	, m_importLightConeData( false )
	, m_forceFlagsFromReFile( false )
	{};

CREXMeshResourceImportOptions::CREXMeshResourceImportOptions( const CREXMeshResourceImportOptions& other )
	: m_importChunks( other.m_importChunks )
	, m_importMaterials( other.m_importMaterials )
	, m_resetLODDistances( other.m_resetLODDistances )
	, m_importCollision( other.m_importCollision )
	, m_importSkinning( other.m_importSkinning )
	, m_importRigs( other.m_importRigs )
	, m_importSnapPoints( other.m_importSnapPoints )
	, m_importOcclusion( other.m_importOcclusion )
	, m_removeUnusedMaterials( other.m_removeUnusedMaterials )
	, m_prepareMeshTopology( other.m_prepareMeshTopology )
	, m_extrudeBoundingBox( other.m_extrudeBoundingBox )
	, m_boundingBoxExtrusionFactor( other.m_boundingBoxExtrusionFactor )
	, m_cleanPhysicsImport( other.m_cleanPhysicsImport )
	, m_occluderType( other.m_occluderType )
	, m_importWorkspotOffsets( other.m_importWorkspotOffsets )
	, m_importAsGraphicalClothMesh( other.m_importAsGraphicalClothMesh )
	, m_matchChunksByOrder( other.m_matchChunksByOrder )
	, m_importUV2AsMorphVertexData( other.m_importUV2AsMorphVertexData )
	, m_overrideExistingRenderFlags( other.m_overrideExistingRenderFlags )
	, m_forceFlagsFromReFile( other.m_forceFlagsFromReFile )
	, m_importLightConeData( other.m_importLightConeData )
{
	// if there's a cached value in resource metadata, otherwise leave the existing one, before changing this see CYB-475726
	if ( other.m_objectType != ROT_Static )
	{
		m_objectType = other.m_objectType;
	}
}

CREXMeshResourceImportOptions& CREXMeshResourceImportOptions::operator=( const CREXMeshResourceImportOptions& other )
{
	m_importChunks = other.m_importChunks;
	m_importMaterials = other.m_importMaterials;
	m_resetLODDistances = other.m_resetLODDistances;
	m_importCollision = other.m_importCollision;
	m_importSkinning = other.m_importSkinning;
    m_createSkinningLOD = other.m_createSkinningLOD;
	m_importRigs = other.m_importRigs;
	m_importSnapPoints = other.m_importSnapPoints;
	m_importOcclusion = other.m_importOcclusion;
	m_removeUnusedMaterials = other.m_removeUnusedMaterials;
	m_prepareMeshTopology = other.m_prepareMeshTopology;
	m_extrudeBoundingBox = other.m_extrudeBoundingBox;
	m_boundingBoxExtrusionFactor = other.m_boundingBoxExtrusionFactor;
	m_cleanPhysicsImport = other.m_cleanPhysicsImport;
	m_occluderType = other.m_occluderType;
	m_importWorkspotOffsets = other.m_importWorkspotOffsets;
	m_importAsGraphicalClothMesh = other.m_importAsGraphicalClothMesh;
	m_matchChunksByOrder = other.m_matchChunksByOrder;
	m_importUV2AsMorphVertexData = other.m_importUV2AsMorphVertexData;
	m_overrideExistingRenderFlags = other.m_overrideExistingRenderFlags;
	m_forceFlagsFromReFile = other.m_forceFlagsFromReFile;
	m_importLightConeData = other.m_importLightConeData;

	if ( other.m_objectType != ROT_Static )
	{
		m_objectType = other.m_objectType;
	}

	return *this;
}


Bool CREXMeshResourceImportOptions::NeedOptions() const
{
	return true;
};

bool CREXMeshResourceImportOptions::ParseFromCommandLine( const red::CommandLine& stringOptions )
{
	m_importChunks = !stringOptions.HasOption( "skipChunks" );
	m_importMaterials = !stringOptions.HasOption( "skipMaterials" );
	m_resetLODDistances = stringOptions.HasOption( "resetLODDistances" );
	m_importCollision = !stringOptions.HasOption( "skipCollision" );
	m_importSkinning = !stringOptions.HasOption( "skipSkinning" );
    m_createSkinningLOD = !stringOptions.HasOption( "skipSkinningLOD" );
	m_importRigs = !stringOptions.HasOption( "skipRigs" );
	m_importSnapPoints = !stringOptions.HasOption( "skipSnapPoints" );
	m_importOcclusion = !stringOptions.HasOption( "skipOcclusion" );
	m_cleanPhysicsImport = stringOptions.HasOption( "cleanPhysicsImport" );
	m_removeUnusedMaterials = !stringOptions.HasOption( "skipMaterialCleanup" );
	m_prepareMeshTopology = stringOptions.HasOption( "prepareMeshTopology" );
	m_extrudeBoundingBox = stringOptions.HasOption( "extrudeBoundingBox" );
	m_importWorkspotOffsets = !stringOptions.HasOption( "skipImportWorkspotOffsets" );
	m_importAsGraphicalClothMesh = stringOptions.HasOption( "importAsGraphicalClothMesh" );
	m_importAsPhysicalClothMesh = stringOptions.HasOption("importAsPhysicalClothMesh");
	m_matchChunksByOrder = stringOptions.HasOption("matchChunksByOrder");
	m_overrideExistingRenderFlags = stringOptions.HasOption("overrideExistingRenderFlags");
	m_importLightConeData = stringOptions.HasOption("importLightConeData");

	if( m_extrudeBoundingBox )
	{
		String extrusionFactorString;
		if( stringOptions.GetFirstOption( "boundingBoxExtrusionFactor", extrusionFactorString ) )
		{
			Float boundingBoxExtrusionFactor;
			if( FromString( extrusionFactorString, boundingBoxExtrusionFactor ) )
			{
				m_boundingBoxExtrusionFactor = boundingBoxExtrusionFactor;
			}
			else
			{
				RED_LOG_WARNING( "Core: Invalid bounding box extrusion factor: %ls", extrusionFactorString );
			}
		}
	}

	if( m_importOcclusion )
	{
		String occlusionTypeString;
		if( stringOptions.GetFirstOption( "occlusionType", occlusionTypeString ) )
		{
			const rtti::EnumType* wotType = static_cast< const rtti::EnumType* >( GetTypeObject< vis::WorldOccluderType >() );
			Int64 occlusionTypeValue;
			if( wotType->FindValue( RED_NAME( occlusionTypeString ), occlusionTypeValue ) )
			{
				m_occluderType = static_cast< vis::WorldOccluderType >( occlusionTypeValue );
			}
			else
			{
				RED_LOG_WARNING( "Core: Invalid occlusion type value: %ls", occlusionTypeString );
			}
		}
	}

	return true;
};

void CREXMeshResourceImportOptions::ParseRenderFlagsFromReFile( const red::AbsolutePath sourceFilePath )
{
	std::unique_ptr< ReReadStream > stream( ReReadStream::Create( sourceFilePath.AsChar() ) );
	if( stream == nullptr )
		return;

	std::vector< ReFileMeshPtr > meshes;
	stream->Read( meshes );

	if( meshes.size() > 0 )
	{
		ReFileMeshPtr reMesh = meshes[ 0 ];

		if( reMesh->nodeFlags().hasFlag( "renderInScene" ) )
			m_renderFlags.m_renderInScene = reMesh->nodeFlags().getFlag( "renderInScene" );
		if( reMesh->nodeFlags().hasFlag( "isTwoSided" ) )
			m_renderFlags.m_isTwoSided = reMesh->nodeFlags().getFlag( "isTwoSided" );
		if( reMesh->nodeFlags().hasFlag( "isRayTracedEmissive" ) )
			m_renderFlags.m_isRayTracedEmissive = reMesh->nodeFlags().getFlag( "isRayTracedEmissive" );
		
		// Leaving the one below for back compatibility...
		if ( reMesh->nodeFlags().hasFlag( "renderInCascades" ) )
			m_renderFlags.m_renderInShadows = reMesh->nodeFlags().getFlag( "renderInCascades" );

		// ... but override with the new one if we have it.
		if ( reMesh->nodeFlags().hasFlag( "renderInShadows" ) )
			m_renderFlags.m_renderInShadows = reMesh->nodeFlags().getFlag( "renderInShadows" );

		if ( reMesh->hasDataChannel( "renderObjectType" ) )
		{
			unsigned int size = -1;
			int* data;
			reMesh->getDataChannel( "renderObjectType", size, data );
			if ( size > 0 )
			{
				m_objectType = static_cast< ERenderObjectType >( data[ 0 ] );
			}
			m_skipSuggestedOptions = true;
		}
		else
		{
			const String pathStr = sourceFilePath.AsString();
			if ( pathStr.Contains( "vehicle" ) )
			{
				m_objectType = ROT_Vehicle;
			}
			else if ( pathStr.Contains( "character" ) )
			{
				m_objectType = ROT_Character;
			}
			else if ( pathStr.Contains( "weapon" ) )
			{
				m_objectType = ROT_Weapon;
			}
			else if ( pathStr.Contains( "road" ) )
			{
				m_objectType = ROT_Road;
			}
		}

		if ( m_forceFlagsFromReFile )
		{
			m_skipSuggestedOptions = true;
		}
	}
}

void CREXMeshResourceImportOptions::OnPostLoad(const PostLoadContext& context)
{
	m_overrideExistingRenderFlags = false;
}
