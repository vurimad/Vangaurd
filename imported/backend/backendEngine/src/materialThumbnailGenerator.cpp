/**
* Copyright (c) 2017 CDProjekt Red, Inc. All Rights Reserved.
*/

#include "build.h"

#include "materialThumbnailGenerator.h"
#include "thumbnailRendering.h"

#include "../../../common/renderData/include/renderer.h"
#include "../../../common/renderData/include/renderProxy.h"
#include "../../../common/resourceMaterial/include/materialDefinition.h"
#include "../../../common/resourceMaterial/include/materialInstance.h"
#include "../../../common/resourceMesh/include/mesh.h"
#include "../../../common/resourceMesh/include/meshAppearance.h"
#include "../../../common/resourceMesh/include/meshDataViewRO.h"
#include "../../../common/resourceMesh/src/materialSetupParamsProvider.h"

RTTI_BEGIN_TYPE_IN_NAMESPACE( MaterialThumbnailGenerator, res );
	RTTI_PARENT_TYPE( IThumbnailGenerator );
RTTI_END_TYPE()

namespace res
{
	const String s_spherePath( "engine\\meshes\\editor\\sphere.w2mesh" );

	MaterialThumbnailGenerator::MaterialThumbnailGenerator()
	{
		const auto & defaultObject = ClassID< CMaterialInstance >()->GetDefaultObject< CResource >();
		const String & extention = defaultObject->GetExtension();
		const String & deprecatedExtension = defaultObject->GetDeprecatedExtension();

		m_formatsExtensions.PushBack( extention );
		if ( !deprecatedExtension.Empty() && extention != deprecatedExtension )
		{
			m_formatsExtensions.PushBack( deprecatedExtension );
		}
	}

	void MaterialThumbnailGenerator::Generate( const THandle<CResource>& resource, ThumbGenContext& ctx, red::SharedPtr<rend::ColorResult>& result )
	{
		if ( THandle< IMaterial > material = Cast<IMaterial>( resource ) )
		{
			ThumbnailRenderingManager rendering( ctx.m_size );
			THandle< CMesh > mesh = LoadMesh( s_spherePath );
			material->UpdateRenderResource();
			SetupCamera( mesh, &rendering );

			IMaterialDefinition* materialDefinition = material->GetMaterialDefinition();
			if ( materialDefinition && materialDefinition->CanUseOnDecals() )
			{
				SetupProxy( mesh, mesh->GetDefaultAppearance()->GetRenderMaterialSetup(), &rendering );
				SetupDecalProxy( material, &rendering );
			}
			else
			{
				SetupProxy( mesh, material, &rendering );
			}
			rendering.Render( result, ctx.m_presetsManager );
		}
	}

	void MaterialThumbnailGenerator::OnReset()
	{

	}

	void MaterialThumbnailGenerator::SetupProxy( const THandle< CMesh >& mesh, const THandle< IMaterial >& material, ThumbnailRenderingManager* renderingManager )
	{
		mesh::DataViewRO dataView( *mesh );
		CMaterialSetupParamsProvider materialSetupParamsProvider( dataView, material, true );
		SetupProxy( mesh, GRender->UploadMaterialSetup( materialSetupParamsProvider.GetMaterialSetupEntries(), materialSetupParamsProvider.GetChunkMaterialNames() ), renderingManager );
	}

	void MaterialThumbnailGenerator::SetupProxy( const THandle< CMesh >& mesh, const TRenderPtr< IRenderResource >& material, ThumbnailRenderingManager* renderingManager )
	{
		RenderProxyMeshComponentInitData meshInfo;
		meshInfo.m_isCharacterShadowFallback = false;
		meshInfo.m_hasMeshResource = true;

		if ( meshInfo.m_hasMeshResource )
		{
			meshInfo.m_meshCanMergeIntoGlobalShadowMesh = false;
			meshInfo.m_renderMesh = mesh->GetRenderResource();
			meshInfo.m_materialSetup = material;
			meshInfo.m_setupsNum = 1;
		}
		else
		{
			meshInfo.m_meshCanMergeIntoGlobalShadowMesh = false;
			meshInfo.m_renderMesh = nullptr;
			meshInfo.m_setupsNum = 0;
		}
		// drawable component
		meshInfo.m_componentFriendlyName = "<Mesh>";
		meshInfo.m_forcedLODLevel = 0;
		meshInfo.m_isCastingGlobalShadows = false;
		meshInfo.m_isCastingLocalShadows = false;
		meshInfo.m_isTwoSided = false;
		meshInfo.m_isCastingShadowsEvenIfNotVisible = false;

		// drawable component
		meshInfo.m_isVisible = true;
		meshInfo.m_isDynamicGeometryComponent = true;
		meshInfo.m_isForceNoAutohide = true;
		meshInfo.m_autoHideDistanceNear = 0.0f;
		meshInfo.m_autoHideDistanceFar = mesh ? mesh->GetAutoHideDistance() : 25.f;
		meshInfo.m_renderingPlane = RPl_Scene;
		meshInfo.m_objectTypeID = mesh ? mesh->GetRenderObjectType() : 0;

		// base
		meshInfo.m_type = RPT_Mesh;
		meshInfo.m_boundingBox = mesh ? mesh->GetBoundingBox() : Box( Vector4::ZERO_3D_POINT(), 1.f );

		renderingManager->SetupProxy( meshInfo );
	}

	void MaterialThumbnailGenerator::SetupDecalProxy( const THandle< IMaterial >& material, ThumbnailRenderingManager* renderingManager )
	{
		RenderProxyDecalComponentInitData decalInfo;
		decalInfo.m_materialRenderResource = material->GetRenderResource();
		decalInfo.m_autoHideDistanceNear = 0.0f;
		decalInfo.m_autoHideDistanceFar = 200.0f;
		decalInfo.m_verticalFlip = false;
		decalInfo.m_horizontalFlip = false;
		decalInfo.m_normalThreshold = 1.0f;
		decalInfo.m_roughnessScale = 1.0f;
		decalInfo.m_isStretchingEnabled = false;
		decalInfo.m_alpha = 1.0f;
		decalInfo.m_orderNo = 1u;

		decalInfo.m_type = RPT_SSDecal;
		decalInfo.m_localToWorld = Transform( Vector3{ -0.3f, -0.3f, 0.25f }, EulerAngles( 0.0f, 45.0f, -45.0f ).ToQuat() );
		decalInfo.m_boundingBox = decalInfo.m_localToWorld.TransformBox( Box{ Vector4::ZEROS(), 1.0f } );

		RenderProxyInitInfo initInfo;
		initInfo.m_componentData = &decalInfo;

		renderingManager->SetupProxy( decalInfo );
	}

	void MaterialThumbnailGenerator::SetupCamera( const THandle<CMesh> mesh, ThumbnailRenderingManager* renderingManager )
	{
		const Box& boundingBox = mesh->GetBoundingBox();

		EulerAngles rot;
		rot.Yaw = -45.0f;
		rot.Pitch = -30.0f;
		rot.Roll = 0.0f;

		Vector4 forward, right, up;
		rot.ToAngleVectors( &forward, &right, &up );

		const Float distance = boundingBox.CalcSize().Mag3() * 0.65f;
		const Vector4 pos = boundingBox.CalcCenter() - ( forward * distance );

		ThumbnailPresentation camera{ forward, pos };
		renderingManager->SetupCamera( camera );
	}

	THandle< CMesh > MaterialThumbnailGenerator::LoadMesh( const String& path )
	{
		auto resourcePath = res::ResourcePath::Build( path );
		auto resourceLoader = GResourceLoader->IssueLoadingRequest( resourcePath );
		return Cast< CMesh >( resourceLoader->WaitUntilLoaded() );
	}

}