/**
* Copyright (c) 2019 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"
#include "backendMorphTargetPreviewPanel.h"
#include "backendMorphTargetEditor.h"
#include "../../backendEngine/include/genericPreviewPanelSharedData.h"
#include "../../../common/renderData/include/renderCommandHandler.h"
#include "../../../common/renderData/include/renderer.h"
#include "../../../common/renderData/include/renderProxy.h"
#include "../../../common/resourceMesh/include/meshAppearance.h"
#include "../../../common/resourceMesh/include/morphTargetMesh.h"
#include "../../../common/renderData/include/morphTargetCommon.h"
#include "../../../common/renderData/include/renderMaterialSetupParam.h"

namespace tools
{
	MorphTargetPreviewPanel::MorphTargetPreviewPanel( RenderingWindowPtr renderingWindow, const PreviewPanelSharedDataPtr& sharedState )
		: PreviewPanel( renderingWindow, sharedState )
		, m_renderProxy( nullptr )
		, m_editor( static_cast< MorphTargetResourceEditor* >( &sharedState->GetEditor() ) )
	{
		GetViewport()->EnableRadialMenu( false );
	}

	MorphTargetPreviewPanel::~MorphTargetPreviewPanel()
	{
		ReleaseRenderProxy();
	}

	void MorphTargetPreviewPanel::ReleaseRenderProxy()
	{
		m_renderProxy.Reset();
	}

	void MorphTargetPreviewPanel::SetupRendering( const THandle< MorphTargetMesh >& mesh )
	{
		ReleaseRenderProxy();
		PrepareRenderProxy( mesh );
	}

	void MorphTargetPreviewPanel::SetMorphWeights( const red::DynArray< MorphTargets::MorphTargetWeight >& morphWeights )
	{
		m_renderProxy->ApplyMorphTargetWeights( morphWeights );
	}

	void MorphTargetPreviewPanel::OnRender( RenderContext& ctx )
	{
		PreviewPanel::OnRender( ctx );
	}

	void MorphTargetPreviewPanel::OnUpdate( const UpdateContext& ctx )
	{
		if ( m_editor )
		{
			m_editor->OnUpdate( ctx );
		}
	}

	void MorphTargetPreviewPanel::PrepareRenderProxy( const THandle< MorphTargetMesh >& morph )
	{
		THandle< CMesh > mesh = morph->GetMesh();
		if ( !mesh )
		{
			return;
		}

		THandle< mesh::MeshAppearance > appearance = mesh->GetAppearanceSafe( morph->GetAppearance() );

		RenderProxyMorphTargetSkinnedMeshComponentInitData meshInfo;
		meshInfo.m_isCharacterShadowFallback = false;
		meshInfo.m_hasMeshResource = nullptr != mesh.Get();
		if ( morph->GetRenderResource() )
		{
			meshInfo.m_meshCanMergeIntoGlobalShadowMesh = false;
			meshInfo.m_renderMesh = morph->GetRenderResource();
			meshInfo.m_materialSetup = appearance->GetRenderMaterialSetup();
			meshInfo.m_setupsNum = 1;
		}
		else
		{
			meshInfo.m_meshCanMergeIntoGlobalShadowMesh = false;
			meshInfo.m_renderMesh = nullptr;
			meshInfo.m_setupsNum = 0;
		}

		meshInfo.m_componentFriendlyName = "<noname>";
		meshInfo.m_forcedLODLevel = -1 ;
		meshInfo.m_isCastingGlobalShadows = mesh->GetCastGlobalShadows();
		meshInfo.m_isCastingLocalShadows =  mesh->GetCastLocalShadows();
		meshInfo.m_isTwoSided = false;
		meshInfo.m_isCastingShadowsEvenIfNotVisible = false;

		// drawable component
		meshInfo.m_isVisible = true;
		meshInfo.m_isDynamicGeometryComponent = true;
		//meshInfo.m_isForceNoAutohide = IsFlagSet( EPBF_SimulationRunning );
		meshInfo.m_autoHideDistanceNear = 0.0f;
		meshInfo.m_autoHideDistanceFar = mesh->GetAutoHideDistance();
		meshInfo.m_renderingPlane = RPl_Scene;

		// base
		meshInfo.m_type = RPT_MorphTargetMesh;
		meshInfo.m_boundingBox = mesh->GetBoundingBox();

		for ( Uint32 iTex = 0; iTex < MorphTargets::BTT_USED_MAX; ++iTex )
		{
			const MorphTargets::BlendedTextureType texType = (MorphTargets::BlendedTextureType)iTex;
			meshInfo.m_baseTexture[ iTex ] = nullptr;
			RED_ASSERT( MorphTargets::ShouldProcess( texType ) );
			if ( auto& baseTex = morph->GetBaseTexture( texType ) )
			{
				// we will be re-initializing this texture with modified textureRef, so texture setup must allow that
				meshInfo.m_baseTexture[ iTex ] = baseTex->CloneRenderResourceWithDynamicAssignment();
			}
		}

		RenderProxyInitInfo initInfo;
		initInfo.m_componentData = &meshInfo;

		// create proxy
		auto proxy = GRender->CreateProxy( initInfo );
		m_renderProxy = red::CreateUniquePtr< RenderProxyHandle >( proxy );
		m_renderProxy->Initialize( GetSharedData()->GetRenderScene(), true );

		// add proxy to rendering scene
		m_renderProxy->Attach();

		SMaterialSetupParameters materialParams;
		for ( Uint32 i = 0; i < MorphTargets::BTT_USED_MAX; ++i )
		{
			const MorphTargets::BlendedTextureType texType = (MorphTargets::BlendedTextureType)i;
			RED_ASSERT( MorphTargets::ShouldProcess( texType ) );
			const CName texMatParam = morph->GetBaseTextureParamName( texType );
			materialParams.AddTextureParameter( texMatParam, meshInfo.m_baseTexture[ i ] );
		}
		m_renderProxy->SetMaterialParameterOverrides( std::move( materialParams ) );
	}
}
