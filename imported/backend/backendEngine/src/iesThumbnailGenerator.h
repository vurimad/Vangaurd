#pragma once

#include "thumbnailGenerator.h"

//*********************************************************************************************************************************************//
//*********************************************************************************************************************************************//

namespace res
{
	class IESThumbnailGenerator : public IThumbnailGenerator
	{
		RTTI_DECLARE_TYPE( IESThumbnailGenerator );

	public:
		IESThumbnailGenerator();
		virtual void Generate( const THandle<CResource>& resource, ThumbGenContext& ctx, red::SharedPtr<rend::ColorResult>& result ) override;
		virtual void OnReset() override;
	};
}

//*********************************************************************************************************************************************//
//*********************************************************************************************************************************************//
