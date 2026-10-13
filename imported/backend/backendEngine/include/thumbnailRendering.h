/**
* Copyright (c) 2016 CDProjekt Red, Inc. All Rights Reserved.
*/

#pragma once

#include "../../../common/renderData/include/renderObjectPtr.h"

struct RenderProxyComponentInitData;
class CBitmapTexture;

namespace tools
{
	class EnvironmentPresetsManager;
}

namespace res
{
	struct BACKEND_ENGINE_API ThumbnailPresentation
	{
		static constexpr Float DEFAULT_CAMERA_FOV = 60.0f;
		Vector4		forward { 0.f, 1.f, 0.f };	//!< Camera direction, defaults to forward direction
		Vector4		position { 0.f, 0.f, 0.f };	//!< Camera position, defaults to zero vector
		Float		fov{ DEFAULT_CAMERA_FOV };	//!< Camera FOV ( in degrees ), a value of 0 indicates orthographic projection
		Float		zoom = 1.f;					//!< Camera zoom, Scales image results
		Vector4		up{ 0.f, 0.f, 1.f };		//!< Camera up direction, defaults to up direction

		void		Reset();
		Bool		IsOrthographic() const;
		Quaternion	GetCameraRotation() const;

		static Quaternion LookAt( const Vector3 &worldForward, const Vector3 &worldUp );
		static void SetupLookAtRegionCamera( const Box &region, ThumbnailPresentation &inOutCamera, bool maxRegion = true );

	};

	//////////////////////////////////////////////////////////////////////////
	// ThumbnailRenderer
	//////////////////////////////////////////////////////////////////////////
	class ThumbnailRenderer
	{
		RED_USE_MEMORY_POOL( red::PoolBackend );

	public:
		ThumbnailRenderer( Uint32 width, Uint32 height );
		~ThumbnailRenderer();

		RED_FORCE_INLINE Bool	HasAlphaCut() const { return m_useAlphaCut; }
		RED_FORCE_INLINE void	SetAlphaCut( Bool enable ) { m_useAlphaCut = enable; }

		RED_FORCE_INLINE void	SetClipArea( const math::Rect& rect ) { m_clipArea = rect; }

		RED_FORCE_INLINE Uint32	GetWidth() const { return m_width; }
		RED_FORCE_INLINE Uint32	GetHeight() const { return m_height; }

		// render a custom stuff into a offscreen image
		typedef std::function< void( CRenderFrameInfo& ) > RenderFunc;
		void Render( red::SharedPtr<rend::ColorResult>& colorResult, const ThumbnailPresentation& camera, RenderFunc func );

	private:
		Uint32		m_width;
		Uint32		m_height;
		Bool		m_useAlphaCut;

		math::Rect	m_clipArea;
	};

	//////////////////////////////////////////////////////////////////////////
	// ThumbnailRenderingManager
	//////////////////////////////////////////////////////////////////////////
	class BACKEND_ENGINE_API ThumbnailRenderingManager
	{
		RED_USE_MEMORY_POOL( red::PoolBackend );

	public:
		explicit ThumbnailRenderingManager( Uint32 size );
		~ThumbnailRenderingManager();

		IRenderScene* GetScene() const { return m_scene.Get(); }
		const TRenderPtr<IRenderScene>& GetScenePtr() const { return m_scene; }

		// render thumbnail
		void Render( red::SharedPtr<rend::ColorResult>& colorResult, tools::EnvironmentPresetsManager& envPresetManager );

		// generate render proxy
		void SetupProxy( RenderProxyComponentInitData& initData );

		// set 2D texture preview
		void SetupTexture( const CBitmapTexture* texture );;

		// set camera position and rotation
		void SetupCamera( const ThumbnailPresentation& camera );

		// set environment preset name
		void SetEnvironmentPreset( const String& presetName );

	private:
		// custom render step
		void OnRender( CRenderFrameInfo& frame, tools::EnvironmentPresetsManager& envPresetManager );

		// prepare environment (sunlight, skybox etc.)
		void SetupEnvironment( CRenderFrameInfo& frame, tools::EnvironmentPresetsManager& envPresetManager ) const;

		// calculate direction of the PBR sun
		Vector4 BuildCustomSunDirection( const EulerAngles& lightBaseAngles ) const;

		void ReleaseRenderProxy();

	private:
		red::UniquePtr< ThumbnailRenderer > m_renderer;
		TRenderPtr<IRenderScene> m_scene;					// render scene
		TRenderPtr<IRenderProxy> m_renderProxy;			// current render proxy

		ThumbnailPresentation	m_presentation;
		String					m_presetName;
		Bool					m_onlyOverlayMode;
	};
}