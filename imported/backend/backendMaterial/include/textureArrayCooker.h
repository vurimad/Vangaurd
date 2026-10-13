/**
* Copyright (c) 2017 CD Projekt Red. All Rights Reserved.
*/

#pragma once

#include "../../../common/renderData/include/renderer.h"

class CTextureArray;

namespace tools
{
struct TextureArrayCookerSetup
{
	CTextureArray& texture;
};

// TODO: Rename to something other than "cooker", this is used to build the platform specific textures
class BACKEND_MATERIAL_API TextureArrayCooker
{
public:
	explicit TextureArrayCooker( const TextureArrayCookerSetup& setup );
	Bool DoCook( const IGpuDataProcessor* processor = GRender );

private:
	const TextureArrayCookerSetup& m_setup;
};
}//tools
