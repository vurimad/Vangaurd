/**
* Copyright (c) 2017 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"
#include "texture.h"
#include "../../redReflection/include/gatheredResource.h"
#include "../../renderData/include/textureEnums.h"
#if defined( VANGUARD_ASSET_HOST )
#include "../../renderData/include/renderResource.h"
#else
#include "../../renderData/include/renderer.h"
#endif

res::GatheredResource resDefaultTexture( RED_CONST_RESOURCEPATH( "engine\\textures\\editor\\debug.xbm" ), GRF_Fallback | GRF_Startup );

//***************************************************************************************************************************//
//***************************************************************************************************************************//

RTTI_BEGIN_ABSTRACT_TYPE( ITexture );
{
	RTTI_PARENT_TYPE(CResource);
}
RTTI_END_TYPE();

//***************************************************************************************************************************//
//***************************************************************************************************************************//

ITexture::ITexture()
{
}

res::GatheredResource * ITexture::GetDefaultResource() const
{
	return &resDefaultTexture;
}

Uint8 ITexture::CalcMipCount( Uint32 width, Uint32 height, Uint32 depth, Bool hasMipChain )
{
	if( !hasMipChain )
		return 1;

	Uint32 maxDimension = Max( width, height, depth );
	Uint32 numMips = 0;
	while ( maxDimension > 0 )
	{
		++numMips;
		maxDimension >>= 1;
	}
	return numMips;
}

res::TextureGroup ITexture::GetTextureGroup( GpuWrapApi::eTextureGroup group )
{
	static_assert( GpuWrapApi::TEXG_System_End - GpuWrapApi::TEXG_System_Begin == 19, "Add missing TEX_SYSTEM groups" );
	static_assert( GpuWrapApi::TEXG_Generic_End - GpuWrapApi::TEXG_Generic_Begin == 7, "Add missing TEX_GENERIC groups" );
	static_assert( GpuWrapApi::TEXG_Multilayer_End - GpuWrapApi::TEXG_Multilayer_Begin == 4, "Add missing TEX_MULTILAYER groups" );
	static_assert( GpuWrapApi::TEXG_MAX == 34, "Add missing groups" );

	switch ( group )
	{
	case GpuWrapApi::eTextureGroup::TEXG_Generic_Color:
	case GpuWrapApi::eTextureGroup::TEXG_Generic_Grayscale:
	case GpuWrapApi::eTextureGroup::TEXG_Generic_Normal:
	case GpuWrapApi::eTextureGroup::TEXG_Generic_Data:
	case GpuWrapApi::eTextureGroup::TEXG_Generic_UI:
	case GpuWrapApi::eTextureGroup::TEXG_Generic_Font:
	case GpuWrapApi::eTextureGroup::TEXG_Generic_LUT:
	case GpuWrapApi::eTextureGroup::TEXG_Generic_MorphBlend:
		return res::TG_Generic;
		break;

	case GpuWrapApi::eTextureGroup::TEXG_Multilayer_Color:
	case GpuWrapApi::eTextureGroup::TEXG_Multilayer_Normal:
	case GpuWrapApi::eTextureGroup::TEXG_Multilayer_Grayscale:
	case GpuWrapApi::eTextureGroup::TEXG_Multilayer_Microblend:
	case GpuWrapApi::eTextureGroup::TEXG_Multilayer_MLMask:
		return res::TG_Multilayer;
		break;

	case GpuWrapApi::eTextureGroup::TEXG_System_Generic:
	case GpuWrapApi::eTextureGroup::TEXG_System_RenderTarget:
	case GpuWrapApi::eTextureGroup::TEXG_System_RenderTargetFlowAllocator:
	case GpuWrapApi::eTextureGroup::TEXG_System_RenderTargetUI_Atlas:
	case GpuWrapApi::eTextureGroup::TEXG_System_RenderTargetUI_Pool:
	case GpuWrapApi::eTextureGroup::TEXG_System_RenderTargetUI_Manager:
	case GpuWrapApi::eTextureGroup::TEXG_System_RenderTargetUI_HUDLayer:
	case GpuWrapApi::eTextureGroup::TEXG_System_RenderTargetUI_CacheWidget:
	case GpuWrapApi::eTextureGroup::TEXG_System_RayTracing:
	case GpuWrapApi::eTextureGroup::TEXG_System_GameplayFX:
	case GpuWrapApi::eTextureGroup::TEXG_System_PostProcess:
	case GpuWrapApi::eTextureGroup::TEXG_System_Environment:
	case GpuWrapApi::eTextureGroup::TEXG_System_EnvProbe:
	case GpuWrapApi::eTextureGroup::TEXG_System_Shadows:
	case GpuWrapApi::eTextureGroup::TEXG_System_Terrain:
	case GpuWrapApi::eTextureGroup::TEXG_System_GI:
	case GpuWrapApi::eTextureGroup::TEXG_System_MultilayerProxy:
	case GpuWrapApi::eTextureGroup::TEXG_System_Video:
	case GpuWrapApi::eTextureGroup::TEXG_System_Garment:
	case GpuWrapApi::eTextureGroup::TEXG_System_MorphTargets:
		return res::TG_System;
		break;

	default:
		return res::TG_Unknown;
		break;
	}
}

const ITexture::TResourcePtr ITexture::CloneRenderResourceWithDynamicAssignment() const
{
#if defined( VANGUARD_ASSET_HOST )
	return nullptr;
#else
	if ( !GRender || GRender->IsDeviceLost() )
	{
		return nullptr;
	}

	// Clone rendering resource
	if ( const ITexture::TResourcePtr& renderResource = GetRenderResource() )
	{
		return GRender->CloneTexture( renderResource );
	}
	return nullptr;
#endif
}

static std::function< void(ITexture *) > s_recompileMaterialCallback;

void SetRecompileMaterialsCallback(const std::function< void(ITexture *) > & callback)
{
	s_recompileMaterialCallback = callback;
}

void RecompileMaterials(ITexture * texture)
{
	if( s_recompileMaterialCallback )
	{
		s_recompileMaterialCallback( texture );
	}
}

//***************************************************************************************************************************//
//***************************************************************************************************************************//
