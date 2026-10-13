/*
* Copyright (c) 2017 CD Projekt Red. All Rights Reserved.
*/

#pragma once

#include "build.h"

#include "../../../common/redReflection/include/util.h"
#if defined( VANGUARD_ASSET_HOST )
#include "../../../common/renderData/include/gpuDataProcessor.h"
#else
#include "../../../common/renderData/include/renderer.h"
#endif
#include "../../../common/renderData/include/renderTextureBlob.h"
#include "../../../common/resourceMaterial/include/bitmapTexture.h"
#include "../../../common/renderBackend/include/renderTextureCompiler.h"
#if !defined( VANGUARD_ASSET_HOST )
#include "../../../backend/backendMaterial/include/backendMaterialPublic.h"
#endif
#include "../../../backend/backendMaterial/include/bitmapTextureCompilationSource.h"
#include "bitmapTextureCooker.h"

namespace tools
{

#if defined( VANGUARD_ASSET_HOST )
namespace
{
	ECookingPlatform s_bitmapTextureImportCookingPlatform = PLATFORM_None;
}

Bool SetBitmapTextureImportCookingPlatform( ECookingPlatform platform )
{
	switch ( platform )
	{
	case PLATFORM_PC:
	case PLATFORM_WindowsServer:
#ifndef NO_CONSOLE_SDK
	case PLATFORM_XboxOne:
	case PLATFORM_PS4:
#endif
		s_bitmapTextureImportCookingPlatform = platform;
		return true;
	default:
		return false;
	}
}

ECookingPlatform GetBitmapTextureImportCookingPlatform()
{
	return s_bitmapTextureImportCookingPlatform;
}
#endif

BitmapTextureCooker::BitmapTextureCooker( const BitmapTextureCookerSetup& setup )
	: m_setup( setup )
{
}

Bool BitmapTextureCooker::DoCook( const IGpuDataProcessor* processor /*= GRender*/ )
{
	red::StaticArray< ECookingPlatform, 4 > platforms;

#if defined( VANGUARD_ASSET_HOST )
	// AssetHost is a single-target authoring/build process. Do not eagerly build
	// render blobs for platforms that are not the active data-system target.
	const ECookingPlatform importPlatform = GetBitmapTextureImportCookingPlatform();
	if ( importPlatform == PLATFORM_None )
	{
		return false;
	}
	platforms.PushBack( importPlatform );
#else
	if( m_setup.cookPC || m_setup.cookWINDOWS_SERVER ) platforms.PushBack( PLATFORM_PC );
#ifndef NO_CONSOLE_SDK
	if( m_setup.cookDURANGO )	platforms.PushBack( PLATFORM_XboxOne );
	if( m_setup.cookORBIS )		platforms.PushBack( PLATFORM_PS4 );
#endif
#endif

	if( platforms.Empty() )
	{
		return false;
	}

	red::DynArray< THandle< rend::IRenderTextureBlob > > blobs( platforms.Size() );

	red::DynArray< red::UniquePtr< ITextureCompilationSource > > sources( platforms.Size() );

	for ( Uint32 i = 0; i < platforms.Size(); ++i )
	{
		sources[i] = red::CreateUniquePtr< CBitmapTextureCompilationSource >( &m_setup.texture, platforms[i] );
	}

	if ( !tools::RenderTextureBlobCompiler::Compile( processor, sources, blobs ) )
	{
		return false;
	}

	for ( Uint32 i = 0; i < platforms.Size(); ++i )
	{
		RED_FATAL_ASSERT( blobs[i].Get() && blobs[i]->m_textureData.Data().Data(), "Blobs for all platforms must be valid" );
		m_setup.texture.InitRenderResourceBlob( platforms[i], blobs[i] );
	}

	return true;
}

}

#if defined( VANGUARD_ASSET_HOST )
extern "C" Bool VanguardAssetHost_SetBitmapTextureImportCookingPlatform( Uint32 platform )
{
	return tools::SetBitmapTextureImportCookingPlatform( static_cast< ECookingPlatform >( platform ) );
}
#endif
