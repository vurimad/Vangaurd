#pragma once

#include "thumbnailGenerator.h"

namespace res
{
	class ThumbnailRenderingManager;

	class TextureThumbnailGenerator : public IThumbnailGenerator
	{
		RTTI_DECLARE_TYPE( TextureThumbnailGenerator );

	public:
		TextureThumbnailGenerator();
		virtual void Generate( const THandle<CResource>& resource, ThumbGenContext& ctx, red::SharedPtr<rend::ColorResult>& result ) override;
		virtual void OnReset() override;
	};
}