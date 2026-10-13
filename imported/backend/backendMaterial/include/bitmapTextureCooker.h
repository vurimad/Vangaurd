/**
* Copyright (c) 2017 CD Projekt Red. All Rights Reserved.
*/

#pragma once

#if defined( VANGUARD_ASSET_HOST )
class IGpuDataProcessor;
#else
#include "../../../common/renderData/include/renderer.h"
#endif

class CBitmapTexture;

namespace tools
{
struct BitmapTextureCookerSetup
{
	CBitmapTexture& texture;
	Bool			cookPC{ true };
	Bool			cookDURANGO{ true };
	Bool			cookORBIS{ true };
	Bool			cookWINDOWS_SERVER{ true };

	BitmapTextureCookerSetup( CBitmapTexture& texture ) : texture( texture ), cookPC(true), cookDURANGO(true), cookORBIS(true), cookWINDOWS_SERVER(true) {}
};

// TODO: Rename to something other than "cooker", this is used to build the platform specific textures
class BACKEND_MATERIAL_API BitmapTextureCooker
{
public:
	explicit BitmapTextureCooker( const BitmapTextureCookerSetup& setup );
#if defined( VANGUARD_ASSET_HOST )
		Bool DoCook( const IGpuDataProcessor* processor = nullptr );
#else
		Bool DoCook( const IGpuDataProcessor* processor = GRender );
#endif

private:
	const BitmapTextureCookerSetup& m_setup;
};
}//tools
