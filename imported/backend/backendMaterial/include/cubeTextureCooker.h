/**
* Copyright (c) 2017 CD Projekt Red. All Rights Reserved.
*/

#pragma once

#include "../../../common/renderData/include/renderer.h"

class CCubeTexture;

namespace tools
{
struct CubeTextureCookerSetup
{
	CCubeTexture& texture;
};

// TODO: Rename to something other than "cooker", this is used to build the platform specific textures
class BACKEND_MATERIAL_API CubeTextureCooker
{
public:
	explicit CubeTextureCooker( const CubeTextureCookerSetup& setup );
	Bool DoCook( const IGpuDataProcessor* processor = GRender );

private:
	const CubeTextureCookerSetup& m_setup;
};
}//tools
