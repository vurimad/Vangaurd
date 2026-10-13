/**
* Copyright (c) 2016 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"

#include "mlmaskPreviewPanel.h"
#include "../../../common/resourceMaterial/include/bitmapTexture.h"
#include "../../../common/renderData/include/renderer.h"
#include "../../../common/renderData/include/renderCommandHandler.h"
#include "../../../common/renderData/include/renderFrameInfo.h"
#include "../../../common/renderDraw/include/fragmentBuilder.h"

#include "../../../common/redReflection/include/gatheredResource.h"
#include "../../../common/resourceMaterial/include/materialTemplate.h"
#include "../../../common/resourceMaterial/include/materialInstance.h"

namespace tools
{
	class EngineViewport;

	static res::TStaticResource<CMaterialTemplate> resMLMaskPreviewMaterial( RED_CONST_RESOURCEPATH( "engine\\materials\\internal\\editor_mlmask_preview.mt" ), GRF_Backend | GRF_Startup );

	MLMaskPreviewPanel::MLMaskPreviewPanel( RenderingWindowPtr renderingWindow, const PreviewPanelSharedDataPtr& sharedState )
		: PreviewPanel( renderingWindow, sharedState )
	{}

	MLMaskPreviewPanel::~MLMaskPreviewPanel() 
	{}

	void MLMaskPreviewPanel::SetupInterop(MLMaskPreviewPanel* panel, ::interop::utils::Object& op)
	{}

	void MLMaskPreviewPanel::OnRender( RenderContext& ctx )
	{
		//ctx.m_frameInfo.SetRenderingMode(RM_OverlayOnly);

		PreviewPanel::OnRender( ctx );

		auto& mask = m_controller->GetMLMask();
		if (mask && mask->GetRenderResource() && m_controller)
		{
			rend::FragmentBuilder fragmentBuilder(ctx.m_cameraInfo.GetDebugDrawer());

			fragmentBuilder.BindMaterial(m_previewMatInst);
			fragmentBuilder.SetColor(Color::WHITE());
			fragmentBuilder.BindTexture(nullptr);
			fragmentBuilder.BindFont(nullptr);

			const rend::RenderMultilayerMaskResource multiLayerResource = mask->GetRenderMultilayerMaskResource();
			THandle< rend::RenderMultilayerMaskBlob > multilayerResourceBlob = Cast< rend::RenderMultilayerMaskBlob >(multiLayerResource.GetRenderResourceBlob());

			const Uint32 screenWidth = fragmentBuilder.GetScreenWidth();
			const Uint32 screenHeight = fragmentBuilder.GetScreenHeight();

			const Uint32 maskWidth = multilayerResourceBlob->m_header.m_maskWidth;
			const Uint32 maskHeight = multilayerResourceBlob->m_header.m_maskHeight;

			Uint32 previewWidth, previewHeight;
			if ( (screenWidth * maskHeight / maskWidth) > screenHeight )
			{
				previewWidth = screenHeight * maskWidth / maskHeight;
				previewHeight = screenHeight;
			}
			else
			{
				previewWidth = screenWidth;
				previewHeight = screenWidth * maskHeight / maskWidth;
			}

			Vector2 screenCenter(screenWidth / 2.f, screenHeight / 2.f);
			Vector2 rectMin(screenCenter.X - previewWidth / 2.0f, screenCenter.Y - previewHeight / 2.0f);
			Vector2 rectMax(screenCenter.X + previewWidth / 2.0f, screenCenter.Y + previewHeight / 2.0f);
			fragmentBuilder.AddFlatRect(rectMin, rectMax, 0.f, 0.f, 1.f, 1.f);

			Vector2 cursorPosition((float)ctx.m_frameInfo.m_debugPreviewSettings.m_pointedPixelX, (float)ctx.m_frameInfo.m_debugPreviewSettings.m_pointedPixelY);
			Vector2 cursorUV((cursorPosition.X - rectMin.X) / previewWidth, (cursorPosition.Y - rectMin.Y) / previewHeight);

			if (cursorPosition.X >= rectMin.X && cursorPosition.Y >= rectMin.Y && cursorPosition.X < rectMax.X && cursorPosition.Y < rectMax.Y)
			{
				//hovering over the rect so we should show the value
				//RED_LOG("pointed pixel: %d, %d", ctx.m_frameInfo.m_debugPreviewSettings.m_pointedPixelX, ctx.m_frameInfo.m_debugPreviewSettings.m_pointedPixelY);
				m_previewMatInst->WriteParameter(RED_NAME_CONSTEXPR("EditorShowValue"), 1.f, true); // bools don't seem to work as expected
				m_previewMatInst->WriteParameter(RED_NAME_CONSTEXPR("EditorCursorPosition"), Vector4(cursorPosition.X, cursorPosition.Y, cursorUV.X, cursorUV.Y), true);
				m_previewMatInst->InvalidateRenderResource();
			}
		}
	}

	void MLMaskPreviewPanel::SetController( const red::SharedPtr< MLMaskController >& options )
	{
		m_controller = options;

		SetupRendering();
	}

	void MLMaskPreviewPanel::SetupRendering()
	{
		auto& mask = m_controller->GetMLMask();
		if (mask && mask->GetRenderResource())
		{
			m_previewMatInst = CreateHandle<CMaterialInstance>();
			m_previewMatInst->SetBaseMaterial(resMLMaskPreviewMaterial.Get());
			m_previewMatInst->WriteParameter(RED_NAME_CONSTEXPR("MultilayerMask"), TResRef<Multilayer_Mask>(mask), false);
			m_previewMatInst->WriteParameter(RED_NAME_CONSTEXPR("EditorShowValue"), 0.f, true); // bools don't seem to work as expected
			OnParametersChanged();
		}
	}

	void MLMaskPreviewPanel::OnParametersChanged()
	{
		m_previewMatInst->WriteParameter(RED_NAME_CONSTEXPR("EditorMaskLayerIndex"), (float)m_controller->GetLayer(), true);
		m_previewMatInst->WriteParameter(RED_NAME_CONSTEXPR("EditorVisualizationModeIndex"), (float)m_controller->GetVisualizationMode(), true);
		m_previewMatInst->InvalidateRenderResource();
	}
}