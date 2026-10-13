/**
* Copyright (c) 2016 CD Projekt Red. All Rights Reserved.
*/

#pragma once

#include "../../backendEngine/include/previewInputListener.h"
#include "../../backendEngine/include/previewPanel.h"
#include "textureView.h"

namespace tools
{
	class TexturePreviewPanel;


	//


	class TexturePreviewInputListener : public IPreviewInputListener
	{
		RED_USE_MEMORY_POOL( red::PoolBackend );

	public:
		explicit TexturePreviewInputListener( TexturePreviewPanel* texturePreviewPanel );
		virtual ~TexturePreviewInputListener() override;

		void RegisterShortcuts( ShortcutManager& manager );

		/// IPreviewInputListener interface
		virtual Bool OnMouseDown( PreviewPanel& panel, const InputMouseButton& event ) override;
		virtual Bool OnMouseUp( PreviewPanel& panel, const InputMouseButton& event ) override;
		virtual Bool OnMouseMoved( PreviewPanel& panel, const InputMouseMove& event ) override;
		virtual Bool OnMouseWheel( PreviewPanel& panel, const InputMouseWheel& event ) override;

	private:
		TexturePreviewPanel* m_texturePreviewPanel;
		Bool m_isRMBDown;
	};


	//


	class TexturePreviewPanel : public PreviewPanel
	{
		DECLARE_INTEROP_DERIVED( TexturePreviewPanel, PreviewPanel );

	public:
		static constexpr Float s_minZoom{ 1.0f };
		static constexpr Float s_maxZoom{ 12800.0f };

		TexturePreviewPanel( RenderingWindowPtr renderingWindow, const PreviewPanelSharedDataPtr& sharedState );
		virtual ~TexturePreviewPanel();

		void SetController( const red::SharedPtr< TextureController >& controller );

		void InitializeZoom();
		void SetZoom( Float zoom );
		void ZoomToFit();
		void ZoomToCoordinates( Float multiplier, const Point& coordinates );

		const Rect GetTextureRect() const;
		const Point& GetTextureOffset() const { return m_textureOffset; }
		void SetTextureOffset( const Point& textureOffset );

		/// PreviewPanel interface
		virtual void OnRender( RenderContext& context ) override final;

	private:
		red::UniquePtr< TexturePreviewInputListener > m_texturePreviewInputListener;
		red::SharedPtr< TextureController > m_controller;

		Float m_zoom; // in percents
		Point m_textureOffset;

		interop::InteropNotifier m_zoomChanged;

		Bool m_isInitializeZoom_HACK; // used due to deferred proper calculation of viewport size
	};
} // tools