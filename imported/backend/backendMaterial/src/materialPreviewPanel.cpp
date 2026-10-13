/**
* Copyright (c) 2016 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"

#include "materialPreviewPanel.h"
#include "../../backendEngine/include/previewPanelSharedData.h"
#include "../../backendEngine/include/renderingWindowCamera.h"
#include "../../../common/resourceMesh/include/mesh.h"
#include "../../../common/engine/include/renderEngineViewport.h"
#include "../../../common/resourceMesh/include/meshDataViewRO.h"
#include "../../../common/renderData/include/renderer.h"
#include "../../../common/renderData/include/renderProxy.h"
#include "../../../common/renderData/include/renderCommandHandler.h"
#include "../../../common/resourceMesh/src/materialSetupParamsProvider.h"
#include "../../../common/resourceMaterial/include/material.h"
#include "../../../common/resourceMaterial/include/materialDefinition.h"

namespace tools
{
	class EngineViewport;

	MaterialPreviewPanel::MaterialPreviewPanel( RenderingWindowPtr renderingWindow, const PreviewPanelSharedDataPtr& sharedState )
		: PreviewPanel( renderingWindow, sharedState )
		, m_renderProxy( nullptr )
	{
		GetViewport()->EnableRadialMenu( false );
	}

	MaterialPreviewPanel::~MaterialPreviewPanel()
	{
		ReleaseRendering();
	}

	void MaterialPreviewPanel::SetupRendering( const THandle< CMesh >& mesh, const THandle< IMaterial >& material )
	{
		ReleaseRendering();
		m_renderProxy = CreateRenderProxy( GetSharedData()->GetRenderScene(), mesh, material );
		auto camera = GetViewport()->GetCamera();
		GetViewport()->SetupOrbitCamera( mesh->GetBoundingBox().CalcCenter(), camera->GetRotation(), camera->GetOrbitDistance() );
	}

	void MaterialPreviewPanel::ReleaseRendering()
	{
		if ( m_renderProxy )
		{
			GRender->GetCommands()->RemoveProxyFromScene( GetSharedData()->GetRenderScene(), m_renderProxy );
			m_renderProxy = nullptr;
		}
	}

	TRenderPtr<IRenderProxy> MaterialPreviewPanel::CreateRenderProxy( const TRenderPtr<IRenderScene>& scene, const THandle< CMesh >& mesh, const THandle< IMaterial >& material )
	{
		if ( material && material->GetMaterialDefinition() )
		{
			if ( !material->GetMaterialDefinition()->GetSupportedVertexFactories().Get( MVF_MeshStatic ) || material->GetMaterialDefinition()->CanUseOnDecals() )
			{
				// don't create mesh preview if the material cannot be used on meshes
				// otherwise the material will disappear in all other editors, because we turn the whole material into fallback in that case
				// TODO use different mesh resource for previewing the material?
				return nullptr;
			}
		}

		RenderProxyMeshComponentInitData meshInfo;
		meshInfo.m_isCharacterShadowFallback = false;
		meshInfo.m_hasMeshResource = nullptr != mesh.Get();
		if ( meshInfo.m_hasMeshResource )
		{
			meshInfo.m_meshCanMergeIntoGlobalShadowMesh = false;
			meshInfo.m_renderMesh = mesh->GetRenderResource();
			mesh::DataViewRO dataView( *mesh );
			CMaterialSetupParamsProvider materialSetupParamsProvider( dataView, material, true );
			meshInfo.m_materialSetup = GRender->UploadMaterialSetup( materialSetupParamsProvider.GetMaterialSetupEntries(), materialSetupParamsProvider.GetChunkMaterialNames() );
			meshInfo.m_setupsNum = 1;
		}
		else
		{
			meshInfo.m_meshCanMergeIntoGlobalShadowMesh = false;
			meshInfo.m_renderMesh = nullptr;
			meshInfo.m_setupsNum = 0;
		}

		meshInfo.m_componentFriendlyName = "<Mesh>";
		meshInfo.m_forcedLODLevel = 0;
		meshInfo.m_isCastingGlobalShadows = mesh ? mesh->GetCastGlobalShadows() : false;
		meshInfo.m_isCastingLocalShadows = mesh ? mesh->GetCastLocalShadows() : false;
		meshInfo.m_isTwoSided = false;
		meshInfo.m_isCastingShadowsEvenIfNotVisible = false;

		meshInfo.m_isVisible = true;
		meshInfo.m_isDynamicGeometryComponent = true;
		meshInfo.m_isForceNoAutohide = false;
		meshInfo.m_autoHideDistanceNear = 0.0f;
		meshInfo.m_autoHideDistanceFar = mesh ? mesh->GetAutoHideDistance() : 25.f;
		meshInfo.m_renderingPlane = RPl_Scene;
		meshInfo.m_objectTypeID = mesh ? mesh->GetRenderObjectType() : 0;

		meshInfo.m_type = RPT_Mesh;
		meshInfo.m_boundingBox = mesh ? mesh->GetBoundingBox() : Box( Vector4::ZERO_3D_POINT(), 1.f );

		RenderProxyInitInfo initInfo;
		initInfo.m_componentData = &meshInfo;

		auto proxy = GRender->CreateProxy( initInfo );

		if( proxy != nullptr )
		{
			GRender->GetCommands()->AddProxyToScene( scene, proxy );
		}
			
		return proxy;
	}

	void MaterialPreviewPanel::SetupInterop( MaterialPreviewPanel* panel, ::interop::utils::Object& op )
	{
	}

}