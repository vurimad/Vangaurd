/**
* Copyright (c) 2017 CD Projekt Red. All Rights Reserved.
*/

#pragma once
#include "../../common/redImage/include/imageSaver.h"

namespace tools
{
	namespace utilities
	{
		class BACKEND_ENGINE_API Screenshot
		{
		public:

			static const Uint64 DEFAULT_CAMERA_NAME = 1066;

			static void RenderFrame( CRenderFrameInfo& frameInfo, const CRenderCamera& renderCamera, const SCameraRenderViewport& cameraRenderViewport, const Uint64 cameraName = DEFAULT_CAMERA_NAME );
			static img::ImagePtr ToImagePtr( const rend::ColorResult& source, Uint32 width, Uint32 height );
			static Bool Save( const red::AbsolutePath& path, img::ImagePtr image );
		};
	}
}
