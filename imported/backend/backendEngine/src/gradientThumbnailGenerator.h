#pragma once

#include "thumbnailGenerator.h"

//*********************************************************************************************************************************************//
//*********************************************************************************************************************************************//

namespace res
{
	class GradientThumbnailGenerator : public IThumbnailGenerator
	{
		RTTI_DECLARE_TYPE( GradientThumbnailGenerator );

	public:
		GradientThumbnailGenerator();
		virtual void Generate( const THandle<CResource>& resource, ThumbGenContext& ctx, red::SharedPtr<rend::ColorResult>& result ) override;
		virtual void OnReset() override;
	};
}

//*********************************************************************************************************************************************//
//*********************************************************************************************************************************************//
