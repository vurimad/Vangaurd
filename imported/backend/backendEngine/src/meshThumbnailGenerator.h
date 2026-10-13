#pragma once

#include "thumbnailGenerator.h"

class CMesh;

namespace res
{
	class ThumbnailRenderingManager;

	class MeshThumbnailGenerator : public IThumbnailGenerator
	{
		RTTI_DECLARE_TYPE( MeshThumbnailGenerator );

	public:
		MeshThumbnailGenerator();
		virtual ThumbnailPresentation PresentationDefaults() const override;
		virtual void Generate( const THandle<CResource>& resource, ThumbGenContext& ctx, red::SharedPtr<rend::ColorResult>& result ) override;
		virtual void OnReset() override;

	private:
		void SetupProxy( const THandle< CMesh > mesh, const ThumbGenContext& ctx, ThumbnailRenderingManager* renderingManager );
		void SetupCamera( const THandle< CMesh > mesh, const ThumbGenContext& ctx, ThumbnailRenderingManager* renderingManager );
	};
}