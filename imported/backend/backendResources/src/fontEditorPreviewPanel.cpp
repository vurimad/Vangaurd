/*
* Copyright (c) 2017 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"
#include "../../../common/resourceMaterial/include/bitmapTexture.h"
#include "../../../common/renderData/include/renderFrameInfo.h"
#include "../../../common/renderDraw/include/fragmentBuilder.h"
#include "../../../common/renderData/include/font.h"
#include "../../../common/renderDraw/include/textDrawer.h"
#include "../../backendEngine/include/resourceEditor.h"
#include "fontEditorPreviewPanelSharedData.h"
#include "fontEditorPreviewPanel.h"

namespace tools
{
	FontEditorPreviewPanel::FontEditorPreviewPanel( const red::SharedPtr<RenderingWindow>& renderingWindow, const red::SharedPtr<FontEditorPreviewPanelSharedData>& sharedData )
		: GenericPreviewPanel( renderingWindow, sharedData )
	{
	}

	FontEditorPreviewPanel::~FontEditorPreviewPanel()
	{
	}

	void FontEditorPreviewPanel::OnRender( RenderContext& ctx )
	{
		ctx.m_cameraInfo.m_renderScene = IRenderScene::Null();

		TBaseClass::OnRender( ctx );

		const red::SharedPtr< FontEditorPreviewPanelSharedData > data = GetSharedData();
		if( !data )
		{
			return;
		}

		if ( const THandle<rend::Font>& font = data->GetFont() )
		{
			const char* c_testText = "The quick brown fox jumps over the lazy dog";

			rend::IDebugDrawer& drawer = ctx.m_cameraInfo.GetDebugDrawer();
			rend::FragmentBuilder builder( drawer );
			builder.SetColor( Color::BLACK() );
			builder.AddFlatRectWH( 0, 0, drawer.GetWidth(), drawer.GetHeight() );

			builder.BindFont( font );
			builder.SetColor( Color::WHITE() );
			Uint32 y = 0;
			for ( Uint16 fontSize = 40; fontSize >= 10; fontSize -= 2 )
			{
				const rend::FontStyle style{ fontSize };
				const rend::Font::Metrics fontMetrics = font->GetMetrics( style );

				rend::TextLayout layout;
				layout.rect = Rect::Make( 0, y, 0, 0 );
				builder.AddFlatTextLayout( layout, style, c_testText );

				y += fontMetrics.lineSpacing;
			}
		}
	}

	void FontEditorPreviewPanel::SetupInterop( FontEditorPreviewPanel* panel, ::interop::utils::Object& op )
	{
	}

	const red::SharedPtr< FontEditorPreviewPanelSharedData > FontEditorPreviewPanel::GetSharedData() const
	{
		return red::StaticCast<FontEditorPreviewPanelSharedData>( PreviewPanel::GetSharedData() );
	}

}	// namespace tools
