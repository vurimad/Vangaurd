/**
* Copyright (c) 2016 CD Projekt Red. All Rights Reserved.
*/

#pragma once

#include "renderTextureCompilationSource.h"
#include "renderTextureMipMapLevel.h"
#include "renderBackendApi.h"

enum ECookingPlatform : Uint8;

namespace rend
{
	class IRenderTextureBlob;
	struct RenderTextureBlobHeader;
}

// #define NO_CONSOLE_SDK

#ifdef NO_CONSOLE_SDK
namespace hacks
{
	void RENDERBACKEND_API ClonePCTextureToXboxAndPS4( rend::IRenderTextureBlob& pcTexture, THandle< rend::IRenderTextureBlob >& outXboxOneTexture, THandle< rend::IRenderTextureBlob >& outPS4Texture );
}
#endif

namespace tools
{
	class RENDERBACKEND_API RenderTextureBlobCompiler
	{
	public:

		// Compile
		static Bool Compile( const IGpuDataProcessor *gpuDataProcessor, const red::ArraySpan< const red::UniquePtr< ITextureCompilationSource > >& src, red::DynArray< THandle<rend::IRenderTextureBlob> >& outBlobs );

		static Bool CompileTexture( ECookingPlatform platform, const ITextureCompilationSource& textureSource, red::ArraySpan< MipArray > mipsArray, rend::RenderTextureBlobHeader& outHeader, red::UniqueBuffer& outBlobData );

		static Bool CompileTextureFunc_PC(const ITextureCompilationSource& textureSource, red::ArraySpan< MipArray > mipsArray, rend::RenderTextureBlobHeader& outHeader, red::UniqueBuffer& outBlobData);
		static Bool CompileTextureFunc_PS4(const ITextureCompilationSource& textureSource, red::ArraySpan< MipArray > mipsArray, rend::RenderTextureBlobHeader& header, red::UniqueBuffer& outBlobData);
		static Bool CompileTextureFunc_XBoxOne(const ITextureCompilationSource& textureSource, red::ArraySpan< MipArray > mipsArray, rend::RenderTextureBlobHeader& header, red::UniqueBuffer& outBlobData);

	private:

		static Uint32 CalcMipChainSize( const MipArray& mipChain );
		static Bool GenerateMipMaps( const IGpuDataProcessor *gpuDataProcessor, const ITextureCompilationSource& textureSource, Uint16 slice, MipArray& outMipMaps, HistogramBias& outHistogramBias );
	};
}//tools