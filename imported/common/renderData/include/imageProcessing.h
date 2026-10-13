/**
* Copyright (c) 2016 CD Projekt Red. All Rights Reserved.
*/

#pragma once
#include "../../redMemory/include/uniqueBuffer.h"

#ifndef RED_PLATFORM_WINPC
#	define NO_TEXTURE_IMPORT
#endif



namespace GpuWrapApi
{

	struct RENDER_DATA_API TextureDataDesc
	{
		TextureDataDesc();

		size_t						width;
		size_t						height;
		size_t						depth;
		size_t						numSlices;
		eTextureFormat				format;
		size_t						rowPitch;
		size_t						slicePitch;
		void*						dataPtr;	//< address of the texture data
	};


	RENDER_DATA_API Bool	CompressImage( const GpuWrapApi::TextureDataDesc& srcImage, GpuWrapApi::eTextureFormat compressedFormat, GpuWrapApi::TextureDataDesc& compressedImage, red::UniqueBuffer &compressedBuffer, GpuWrapApi::EImageCompressionHint compressionHint, Float alphaThreshold = 0.5f );
	RENDER_DATA_API Bool	DecompressImage( const GpuWrapApi::TextureDataDesc& srcImage, GpuWrapApi::eTextureFormat decompressedFormat, GpuWrapApi::TextureDataDesc& decompressedImage, red::UniqueBuffer &compressedBuffer );
	RENDER_DATA_API Bool	ConvertBuffer( Uint32 width, Uint32 height, Uint32 depth, Uint32 sourcePitch, GpuWrapApi::eTextureFormat sourceFormat, const void* sourceData, Uint32 sourceDataSize, GpuWrapApi::eTextureFormat targetFormat, red::UniqueBuffer &outConvertedBuffer, GpuWrapApi::EImageCompressionHint compressionHint, Float alphaThreshold =0.5f );

	#ifndef RED_PLATFORM_CONSOLE
	RENDER_DATA_API Bool	SaveTextureToFile( const GpuWrapApi::TextureDataDesc& image, const Char* fileName, const GpuWrapApi::eTextureSaveFormat saveFormat );
	#endif

	#ifndef NO_TEXTURE_IMPORT
	RENDER_DATA_API Bool	ImportTexture( const red::AbsolutePath importPath, GpuWrapApi::eTextureImportFormat format, /*out*/red::UniqueBuffer &dst, /*out*/TextureDataDesc& desc, Bool shouldBePowerOf2 = true, Bool isImportingGenericData = false, Bool applyFlip = true );
	#endif

	RENDER_DATA_API Bool SaveImageAsEXR( const float *data, int width, int height, int components, const char *outfilename );

	RENDER_DATA_API const String GetLastErrorString();

} // end namespace GpuWrapApi