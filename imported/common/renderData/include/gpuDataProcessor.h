/**
* Copyright (c) 2011 CD Projekt Red. All Rights Reserved.
*/
#pragma once
#include "../../redMemory/include/uniqueBuffer.h"

namespace img
{
	class Image;
	typedef red::SharedPtr<Image> ImagePtr;
}

namespace GpuWrapApi
{
	struct TextureDataDesc;
}

class RENDER_DATA_API IGpuDataProcessor
{
public:

	typedef std::function< void ( img::ImagePtr& imgPtr )>			TGenerateMipmap;

	virtual ~IGpuDataProcessor() {}

public:

	// Get custom mipmap generation function. By default disabled
	virtual TGenerateMipmap	GetMipmapGenFunc() const { return TGenerateMipmap(); }
};
