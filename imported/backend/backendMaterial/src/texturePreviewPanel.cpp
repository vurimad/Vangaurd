/**
* Copyright (c) 2016 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"
#include "texturePreviewPanel.h"

#include "../../../common/renderData/include/renderCommandHandler.h"
#include "../../../common/renderData/include/renderFrameInfo.h"
#include "../../../common/renderData/include/renderer.h"
#include "../../../common/renderDraw/include/fragmentBuilder.h"
#include "../../../common/resourceMaterial/include/bitmapTexture.h"

namespace tools
{
	TexturePreviewInputListener::TexturePreviewInputListener( TexturePreviewPanel* texturePreviewPanel )
		: m_texturePreviewPanel{ texturePreviewPanel }
	{
	}

	TexturePreviewInputListener::~TexturePreviewInputListener() = default;

	void TexturePreviewInputListener::RegisterShortcuts( ShortcutManager& manager )
	{
		// Zoom in
		{
			ShortcutBinding binding1{ ShortcutBinding().Key( EInputKey::IK_Equals ).Modifier( EInputKey::IK_LControl ) };
			ShortcutBinding binding2{ ShortcutBinding().Key( EInputKey::IK_Equals ).Modifier( EInputKey::IK_RControl ) };
			ShortcutCallback callback{ [this]( const KeyEventData& data )
			{
				const Point viewportCenter{ ( Int32 )m_texturePreviewPanel->GetWidth() / 2, ( Int32 )m_texturePreviewPanel->GetHeight() / 2 };
				m_texturePreviewPanel->ZoomToCoordinates( 2.0f, viewportCenter );
				return true;
			} };
			ShortcutBuilder shortcut1{ ShortcutBuilder().IdName( "idTextureZoomInShortcut1" ).Binding( binding1 ).Callback( callback ).Name( "Zoom in texture" ) };
			ShortcutBuilder shortcut2{ ShortcutBuilder().IdName( "idTextureZoomInShortcut2" ).Binding( binding2 ).Callback( callback ).Name( "Zoom in texture" ) };

			manager.Register( shortcut1 );
			manager.Register( shortcut2 );
		}

		// Zoom out
		{
			ShortcutBinding binding1{ ShortcutBinding().Key( EInputKey::IK_Minus ).Modifier( EInputKey::IK_LControl ) };
			ShortcutBinding binding2{ ShortcutBinding().Key( EInputKey::IK_Minus ).Modifier( EInputKey::IK_RControl ) };
			ShortcutCallback callback{ [this]( const KeyEventData& data )
			{
				const Point viewportCenter{ ( Int32 )m_texturePreviewPanel->GetWidth() / 2, ( Int32 )m_texturePreviewPanel->GetHeight() / 2 };
				m_texturePreviewPanel->ZoomToCoordinates( 0.5f, viewportCenter );
				return true;
			} };
			ShortcutBuilder shortcut1{ ShortcutBuilder().IdName( "idTextureZoomOutShortcut1" ).Binding( binding1 ).Callback( callback ).Name( "Zoom out texture" ) };
			ShortcutBuilder shortcut2{ ShortcutBuilder().IdName( "idTextureZoomOutShortcut2" ).Binding( binding2 ).Callback( callback ).Name( "Zoom out texture" ) };

			manager.Register( shortcut1 );
			manager.Register( shortcut2 );
		}

		// Reset zoom and texture position
		{
			ShortcutBinding binding1{ ShortcutBinding().Key( EInputKey::IK_0 ).Modifier( EInputKey::IK_LControl ) };
			ShortcutBinding binding2{ ShortcutBinding().Key( EInputKey::IK_0 ).Modifier( EInputKey::IK_RControl ) };
			ShortcutCallback callback{ [this]( const KeyEventData& data )
			{
				m_texturePreviewPanel->SetZoom( 100.0f );
				m_texturePreviewPanel->SetTextureOffset( Point::ZERO() );
				return true;
			} };
			ShortcutBuilder shortcut1{ ShortcutBuilder().IdName( "idTextureZoom100Shortcut1" ).Binding( binding1 ).Callback( callback ).Name( "Zoom out texture" ) };
			ShortcutBuilder shortcut2{ ShortcutBuilder().IdName( "idTextureZoom100Shortcut2" ).Binding( binding2 ).Callback( callback ).Name( "Zoom out texture" ) };

			manager.Register( shortcut1 );
			manager.Register( shortcut2 );
		}
	}

	Bool TexturePreviewInputListener::OnMouseDown( PreviewPanel& panel, const InputMouseButton& event )
	{
		if ( event.m_button == EMouseButton::RMB )
		{
			m_isRMBDown = true;
		}
		return false;
	}

	Bool TexturePreviewInputListener::OnMouseUp( PreviewPanel& panel, const InputMouseButton& event )
	{
		if ( event.m_button == EMouseButton::RMB )
		{
			m_isRMBDown = false;
		}
		return false;
	}

	Bool TexturePreviewInputListener::OnMouseMoved( PreviewPanel& panel, const InputMouseMove& event )
	{
		if ( m_isRMBDown )
		{
			Point textureOffset{ m_texturePreviewPanel->GetTextureOffset() };
			textureOffset.x -= event.m_dx;
			textureOffset.y -= event.m_dy;
			m_texturePreviewPanel->SetTextureOffset( textureOffset );
		}
		return false;
	}

	Bool TexturePreviewInputListener::OnMouseWheel( PreviewPanel& panel, const InputMouseWheel& event )
	{
		if ( math::EqualsZero( event.m_delta ) )
		{
			return false;
		}

		const Float baseMultiplier{ 1.2f };
		const Float exponent{ event.m_delta < 0.0f ? -1.0f : 1.0f };
		const Float multiplier{ std::roundf( std::powf( baseMultiplier, exponent ) * 100.0f ) / 100.0f };
		const Point coordinates{ m_texturePreviewPanel->GetMouseX(), m_texturePreviewPanel->GetMouseY() };

		m_texturePreviewPanel->ZoomToCoordinates( multiplier, coordinates );
		return false;
	}


	//


	TexturePreviewPanel::TexturePreviewPanel( RenderingWindowPtr renderingWindow, const PreviewPanelSharedDataPtr& sharedState )
		: PreviewPanel( renderingWindow, sharedState )
		, m_texturePreviewInputListener{ red::CreateUniquePtr< TexturePreviewInputListener >( this ) }
		, m_zoom{ 100.0f }
		, m_textureOffset{ Point::ZERO() }
		, m_isInitializeZoom_HACK{ false }
	{
		m_texturePreviewInputListener->RegisterShortcuts( *GetShortcutManager() );
		AttachInputListener( m_texturePreviewInputListener.Get() );
	}

	TexturePreviewPanel::~TexturePreviewPanel()
	{
		DetachInputListener( m_texturePreviewInputListener.Get() );
	}

	void TexturePreviewPanel::SetupInterop( TexturePreviewPanel* panel, ::interop::utils::Object& op )
	{
		op.RegisterFunctor< void >( "initializeZoom",
			[panel]( interop::IContext& ctx ) { panel->InitializeZoom(); panel->m_isInitializeZoom_HACK = true; } );

		op.RegisterValue< Float >( "zoom",
			[panel]( interop::IContext& ctx, Float& outValue ) { outValue = panel->m_zoom; },
			[panel]( interop::IContext& ctx, const Float& newValue ) { panel->SetZoom( newValue ); } );

		op.RegisterNotifier( "_onZoomChanged", panel->m_zoomChanged );
	}

	void TexturePreviewPanel::SetController( const red::SharedPtr< TextureController >& controller )
	{
		m_controller = controller;
	}

	void TexturePreviewPanel::InitializeZoom()
	{
		const auto& texture{ m_controller->GetTexture() };
		if ( !texture )
		{
			return;
		}

		if ( texture->GetSizeDesc().width > GetViewport()->GetWidth() 
			|| texture->GetSizeDesc().height > GetViewport()->GetHeight() )
		{
			ZoomToFit();
		}
	}

	void TexturePreviewPanel::SetZoom( Float zoom )
	{
		zoom = math::Clamp( zoom, s_minZoom, s_maxZoom );
		if ( math::AlmostEquals( zoom, m_zoom ) )
		{
			return;
		}

		m_zoom = zoom;
		m_zoomChanged.Notify( m_zoom );
	}

	void TexturePreviewPanel::ZoomToFit()
	{
		const auto& texture{ m_controller->GetTexture() };
		if ( !texture )
		{
			return;
		}

		const auto scaleWidth{ ( Float )GetViewport()->GetWidth() / ( Float )texture->GetSizeDesc().width };
		const auto scaleHeight{ ( Float )GetViewport()->GetHeight() / ( Float )texture->GetSizeDesc().height };
		const auto scale{ std::min( scaleWidth, scaleHeight ) };

		SetZoom( 100.0f * scale );
		SetTextureOffset( Point::ZERO() );
	}

	void TexturePreviewPanel::ZoomToCoordinates( Float multiplier, const Point& coordinates )
	{
		if ( math::AlmostEquals( multiplier, 1.0f ) )
		{
			return;
		}

		Float zoom{ m_zoom * multiplier };
		if ( zoom < s_minZoom )
		{
			zoom = s_minZoom;
			multiplier = s_minZoom / m_zoom;
		}
		else if ( zoom > s_maxZoom )
		{
			zoom = s_maxZoom;
			multiplier = s_maxZoom / m_zoom;
		}

		if ( math::AlmostEquals( zoom, m_zoom ) )
		{
			return;
		}

		//

		Rect previousTextureRect{ GetTextureRect() };
		m_zoom = zoom;

		// adjust texture offset
		if ( previousTextureRect.Contains( coordinates ) )
		{
			previousTextureRect.Translate( -coordinates );
			const Point preScaleTextureCenter{ previousTextureRect.GetCenter() };

			previousTextureRect.m_left = static_cast< Int32 >( previousTextureRect.m_left * multiplier );
			previousTextureRect.m_right = static_cast< Int32 >( previousTextureRect.m_right * multiplier );
			previousTextureRect.m_top = static_cast< Int32 >( previousTextureRect.m_top * multiplier );
			previousTextureRect.m_bottom = static_cast< Int32 >( previousTextureRect.m_bottom * multiplier );

			const Point postScaleTextureCenter{ previousTextureRect.GetCenter() };
			const Point deltaScaleTextureCenter{ postScaleTextureCenter - preScaleTextureCenter };

			SetTextureOffset( m_textureOffset + deltaScaleTextureCenter );
		}
		else
		{
			static red::Function< Point( const Rect&, const Point& ) > GetTransformOffset{ 
				[]( const Rect& textureRect, const Point& coordinates ) 
			{
				Point transformOffset{ Point::ZERO() };
				if ( textureRect.m_left > coordinates.x )
				{
					transformOffset.x = textureRect.m_left - coordinates.x;
				}
				else if ( textureRect.m_right < coordinates.x )
				{
					transformOffset.x = textureRect.m_right - coordinates.x;
				}
				if ( textureRect.m_top > coordinates.y )
				{
					transformOffset.y = textureRect.m_top - coordinates.y;
				}
				else if ( textureRect.m_bottom < coordinates.y )
				{
					transformOffset.y = textureRect.m_bottom - coordinates.y;
				}
				return transformOffset;
			} };

			const Point preTransformOffset{ GetTransformOffset( previousTextureRect, coordinates ) };

			previousTextureRect.Translate( -( preTransformOffset + coordinates ) );
			const Point preScaleTextureCenter{ previousTextureRect.GetCenter() };

			previousTextureRect.m_left = static_cast< Int32 >( previousTextureRect.m_left * multiplier );
			previousTextureRect.m_right = static_cast< Int32 >( previousTextureRect.m_right * multiplier );
			previousTextureRect.m_top = static_cast< Int32 >( previousTextureRect.m_top * multiplier );
			previousTextureRect.m_bottom = static_cast< Int32 >( previousTextureRect.m_bottom * multiplier );

			const Point postScaleTextureCenter{ previousTextureRect.GetCenter() };
			const Point deltaScaleTextureCenter{ postScaleTextureCenter - preScaleTextureCenter };

			SetTextureOffset( m_textureOffset + deltaScaleTextureCenter );

			// post-pixel-perfect adjustments
			const Rect currentTextureRect{ GetTextureRect() };
			const Point postTransformOffset{ GetTransformOffset( currentTextureRect, coordinates ) };
			const Point deltaTransformOffset{ postTransformOffset - preTransformOffset };

			SetTextureOffset( m_textureOffset - deltaTransformOffset );
		}

		m_zoomChanged.Notify( m_zoom );
	}

	const Rect TexturePreviewPanel::GetTextureRect() const
	{
		const auto& texture{ m_controller->GetTexture() };
		if ( texture )
		{
			const auto viewportWidth{ static_cast< Int32 >( GetViewport()->GetWidth() ) };
			const auto viewportHeight{ static_cast< Int32 >( GetViewport()->GetHeight() ) };

			const auto scale{ m_zoom / 100.0f };
			const auto width{ static_cast< Int32 >( texture->GetSizeDesc().width * scale ) };
			const auto height{ static_cast< Int32 >( texture->GetSizeDesc().height * scale ) };
			const auto x{ ( viewportWidth - width ) / 2 + m_textureOffset.x };
			const auto y{ ( viewportHeight - height ) / 2 + m_textureOffset.y };

			return Rect::Make( x, y, width, height );
		}
		else
		{
			return Rect::EMPTY();
		}
	}

	void TexturePreviewPanel::SetTextureOffset( const Point& textureOffset )
	{
		const Rect textureRect{ GetTextureRect() };
		const Int32 viewportBorder{ 15 };
		const auto viewportWidth{ static_cast< Int32 >( GetViewport()->GetWidth() ) };
		const auto viewportHeight{ static_cast< Int32 >( GetViewport()->GetHeight() ) };

		if ( viewportWidth <= viewportBorder || viewportHeight <= viewportBorder )
		{
			return;
		}

		const auto maxTextureOffsetX{ viewportWidth / 2 + textureRect.Width() / 2 - viewportBorder };
		const auto maxTextureOffsetY{ viewportHeight / 2 + textureRect.Height() / 2 - viewportBorder };

		m_textureOffset.x = math::Clamp< Int32 >( textureOffset.x, -maxTextureOffsetX, maxTextureOffsetX );
		m_textureOffset.y = math::Clamp< Int32 >( textureOffset.y, -maxTextureOffsetY, maxTextureOffsetY );
	}

	void TexturePreviewPanel::OnRender( RenderContext& ctx )
	{
		PreviewPanel::OnRender( ctx );
		ctx.m_frameInfo.SetRenderingMode( RM_OverlayOnly );

		if ( !m_controller )
		{
			return;
		}

		const auto& texture{ m_controller->GetTexture() };
		if ( texture && texture->GetRenderResource() )
		{
			IRenderCommandHandler::DebugTexturePreviewDesc previewDesc;
			previewDesc.rect = GetTextureRect();
			previewDesc.mipIdx = m_controller->GetMipmapLevel();
			previewDesc.sliceIdx = m_controller->GetSlice();
			previewDesc.channelMask = m_controller->GetChannelMask();
			previewDesc.numChannels = m_controller->GetChannelNumber();
			previewDesc.allowInterpret = !m_controller->GetShowRaw();
			previewDesc.min = m_controller->GetMinValue();
			previewDesc.max = m_controller->GetMaxValue();
			previewDesc.pointSample = m_controller->GetPointSample();

			GRender->GetCommands()->SendDebugTexturePreview( ctx.m_frameInfo.GetScenePtr(), texture->GetRenderResource(), previewDesc );
		}

		// hack for zoom initialization issue
		if ( m_isInitializeZoom_HACK )
		{
			static Uint8 refreshCount{ 0 };
			if ( refreshCount > 10 )
			{
				InitializeZoom();
				refreshCount = 0;
				m_isInitializeZoom_HACK = false;
			}
			else
			{
				refreshCount++;
			}
		}
	}
} // tools

