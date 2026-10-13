/**
* Copyright (c) 2020 CD Projekt Red. All Rights Reserved.
*/

#pragma once

#include "renderDynamicTextureDesc.h"

class IImageTextureGenerator : public IDynamicTextureGenerator
{
public:
	virtual Bool RequestUploadPNGFromMemory( const red::DynArray< Uint8 >& pngMemory ) = 0;
};
