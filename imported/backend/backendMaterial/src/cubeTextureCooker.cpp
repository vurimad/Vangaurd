/*
* Copyright (c) 2017 CD Projekt Red. All Rights Reserved.
*/

#pragma once

#include "build.h"

#include "../../../common/redReflection/include/util.h"
#include "../../../common/renderData/include/renderer.h"
#include "../../../common/renderData/include/renderTextureBlob.h"
#include "../../../common/resourceMaterial/include/cubeTexture.h"
#include "../../../common/renderBackend/include/renderTextureCompiler.h"
#include "../../../backend/backendMaterial/include/backendMaterialPublic.h"
#include "cubeTextureCompilationSource.h"
#include "cubeTextureCooker.h"

namespace tools
{

CubeTextureCooker::CubeTextureCooker( const CubeTextureCookerSetup& setup )
	: m_setup( setup )
{
}

Bool CubeTextureCooker::DoCook( const IGpuDataProcessor* processor /*= GRender*/ )
{
#ifdef NO_CONSOLE_SDK
	const ECookingPlatform platforms[] = { PLATFORM_PC, PLATFORM_WindowsServer };
#else
	const ECookingPlatform platforms[] = { PLATFORM_PC, PLATFORM_XboxOne, PLATFORM_PS4, PLATFORM_WindowsServer };
#endif
	const Uint32 numPlatforms = RED_ARRAY_COUNT_U32( platforms );

	red::DynArray< THandle< rend::IRenderTextureBlob > > blobs( numPlatforms );

	red::DynArray< red::UniquePtr< ITextureCompilationSource > > sources( numPlatforms );
	for ( Uint32 i = 0; i < numPlatforms; ++i )
	{
		sources[ i ] = red::CreateUniquePtr< CCubeTextureCompilationSource >( &m_setup.texture, platforms[ i ] );
	}
	if ( !tools::RenderTextureBlobCompiler::Compile( processor, sources, blobs ) )
	{
		return false;
	}

	for ( Uint32 i = 0; i < numPlatforms; ++i )
	{
		RED_FATAL_ASSERT( blobs[ i ].Get() && blobs[ i ]->m_textureData.Data().Data(), "Blobs for all platforms must be valid" );
		m_setup.texture.InitRenderResourceBlob( platforms[ i ], blobs[ i ] );
	}
	return true;
}

}
