/**
* Copyright (c) 2007 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"
#include "imageProcessing.h"
#include "gpuDataProcessor.h"
#include "renderSimpleTypes.h"

#ifdef RED_PLATFORM_WINPC
#include <fstream>
#if defined( VANGUARD_ASSET_HOST )
#ifndef assert
#define assert( expression ) RED_ASSERT( expression )
#define VANGUARD_ASSET_HOST_ASSERT_BRIDGE
#endif
#include "../../../external/DirectXTex/include/DirectXTex.h"
#else
#include "../../../../external/DirectXTex/include/DirectXTex.h"
#endif
#pragma comment (lib, "d3d11.lib")
#define TINYEXR_IMPLEMENTATION
#if defined( VANGUARD_ASSET_HOST )
#pragma warning( push )
#pragma warning( disable : 5208 )
#include "../../../external/tinyexr/tinyexr.h"
#pragma warning( pop )
#if defined( VANGUARD_ASSET_HOST_ASSERT_BRIDGE )
#undef assert
#undef VANGUARD_ASSET_HOST_ASSERT_BRIDGE
#endif
#else
#include "../../../../external/tinyexr/tinyexr.h"
#endif

// This hack is needed for Steam Overlay to work properly
#pragma comment( linker, "/include:D3D11CreateDeviceAndSwapChain" )

#endif // RED_PLATFORM_WINPC

namespace GpuWrapApi
{
	///////////////////////

	TextureDataDesc::TextureDataDesc()
		: width( 0 )
		, height( 0 )
		, numSlices( 0 )
		, format( GpuWrapApi::TEXFMT_R8G8B8A8_Unorm )
		, rowPitch( 0 )
		, slicePitch( 0 )
		, dataPtr( nullptr )
	{}

	///////////////////////

#ifdef RED_PLATFORM_WINPC
	RED_INLINE DXGI_FORMAT Map( GpuWrapApi::eTextureFormat format )
	{
		switch ( format )
		{
		case GpuWrapApi::TEXFMT_A8_Unorm:						return DXGI_FORMAT_A8_UNORM;
		case GpuWrapApi::TEXFMT_R8_Unorm:						return DXGI_FORMAT_R8_UNORM;
		case GpuWrapApi::TEXFMT_L8_Unorm:						return DXGI_FORMAT_R8_UNORM;
		case GpuWrapApi::TEXFMT_R8_Uint:				return DXGI_FORMAT_R8_UINT;
		case GpuWrapApi::TEXFMT_R8G8_Unorm:					return DXGI_FORMAT_R8G8_UNORM;
		case GpuWrapApi::TEXFMT_R8G8B8A8_Unorm:				return DXGI_FORMAT_R8G8B8A8_UNORM;
		case GpuWrapApi::TEXFMT_R8G8B8A8_Unorm_SRGB:			return DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
		case GpuWrapApi::TEXFMT_R8G8B8A8_Snorm:			return DXGI_FORMAT_R8G8B8A8_SNORM;
		case GpuWrapApi::TEXFMT_R8G8B8A8_Uint_Typeless:		return DXGI_FORMAT_R8G8B8A8_TYPELESS;
		case GpuWrapApi::TEXFMT_R10G10B10A2_Unorm:			return DXGI_FORMAT_R10G10B10A2_UNORM;
		case GpuWrapApi::TEXFMT_R8G8B8X8_Unorm:				return DXGI_FORMAT_R8G8B8A8_UNORM;
		case GpuWrapApi::TEXFMT_R16_Unorm:			return DXGI_FORMAT_R16_UNORM;
		case GpuWrapApi::TEXFMT_R16_Uint:				return DXGI_FORMAT_R16_UINT;
		case GpuWrapApi::TEXFMT_R32_Uint:				return DXGI_FORMAT_R32_UINT;
		case GpuWrapApi::TEXFMT_R16G16_Uint:			return DXGI_FORMAT_R16G16_UINT;
		case GpuWrapApi::TEXFMT_R16G16_Sint:			return DXGI_FORMAT_R16G16_SINT;
		case GpuWrapApi::TEXFMT_R32G32_Uint:			return DXGI_FORMAT_R32G32_UINT;
		case GpuWrapApi::TEXFMT_R32G32B32A32_Uint:		return DXGI_FORMAT_R32G32B32A32_UINT;
		case GpuWrapApi::TEXFMT_R16G16B16A16_Float:		return DXGI_FORMAT_R16G16B16A16_FLOAT;
		case GpuWrapApi::TEXFMT_R16G16B16A16_Unorm:		return DXGI_FORMAT_R16G16B16A16_UNORM;
		case GpuWrapApi::TEXFMT_R16G16B16A16_Uint:		return DXGI_FORMAT_R16G16B16A16_UINT;
		case GpuWrapApi::TEXFMT_R11G11B10_Float:		return DXGI_FORMAT_R11G11B10_FLOAT;
#ifdef RED_PLATFORM_DURANGO
		case GpuWrapApi::TEXFMT_R10G10B10_6E4_A2_Float:	return DXGI_FORMAT_R10G10B10_6E4_A2_FLOAT;
#endif
		case GpuWrapApi::TEXFMT_R16G16_Float:			return DXGI_FORMAT_R16G16_FLOAT;
			//dex++
		case GpuWrapApi::TEXFMT_R32G32_Float:			return DXGI_FORMAT_R32G32_FLOAT;
			//dex--
		case GpuWrapApi::TEXFMT_R32G32B32A32_Float:		return DXGI_FORMAT_R32G32B32A32_FLOAT;
		case GpuWrapApi::TEXFMT_R32_Float:				return DXGI_FORMAT_R32_FLOAT;
		case GpuWrapApi::TEXFMT_R16_Float:				return DXGI_FORMAT_R16_FLOAT;
		case GpuWrapApi::TEXFMT_BC1:					return DXGI_FORMAT_BC1_UNORM;
		case GpuWrapApi::TEXFMT_BC1_SRGB:				return DXGI_FORMAT_BC1_UNORM_SRGB;
		case GpuWrapApi::TEXFMT_BC2:					return DXGI_FORMAT_BC2_UNORM;
		case GpuWrapApi::TEXFMT_BC2_SRGB:				return DXGI_FORMAT_BC2_UNORM_SRGB;
		case GpuWrapApi::TEXFMT_BC3:					return DXGI_FORMAT_BC3_UNORM;
		case GpuWrapApi::TEXFMT_BC3_SRGB:				return DXGI_FORMAT_BC3_UNORM_SRGB;
		case GpuWrapApi::TEXFMT_BC4:					return DXGI_FORMAT_BC4_UNORM;
		case GpuWrapApi::TEXFMT_BC5:					return DXGI_FORMAT_BC5_UNORM;
		case GpuWrapApi::TEXFMT_BC6H_UNSIGNED:			return DXGI_FORMAT_BC6H_UF16;
		case GpuWrapApi::TEXFMT_BC6H_SIGNED:			return DXGI_FORMAT_BC6H_SF16;
		case GpuWrapApi::TEXFMT_BC7:					return DXGI_FORMAT_BC7_UNORM;
		case GpuWrapApi::TEXFMT_BC7_SRGB:				return DXGI_FORMAT_BC7_UNORM_SRGB;
		case GpuWrapApi::TEXFMT_D24S8:					return DXGI_FORMAT_R24G8_TYPELESS;
		case GpuWrapApi::TEXFMT_D16U:					return DXGI_FORMAT_R16_TYPELESS;
		case GpuWrapApi::TEXFMT_D32F:					return DXGI_FORMAT_R32_TYPELESS;
		default:										RED_FATAL( "invalid or not available" ); return DXGI_FORMAT_R8G8B8A8_UNORM;
		}
	}

	inline GpuWrapApi::eTextureFormat Map( DXGI_FORMAT format )
	{
		switch ( format )
		{
		case DXGI_FORMAT_A8_UNORM:				return GpuWrapApi::TEXFMT_A8_Unorm;
		case DXGI_FORMAT_R8_UNORM:				return GpuWrapApi::TEXFMT_L8_Unorm;
		case DXGI_FORMAT_R8_UINT:				return GpuWrapApi::TEXFMT_R8_Uint;
		case DXGI_FORMAT_R8G8_TYPELESS:			return GpuWrapApi::TEXFMT_R8G8_Unorm;
		case DXGI_FORMAT_R8G8_UNORM:			return GpuWrapApi::TEXFMT_R8G8_Unorm;
		case DXGI_FORMAT_R8G8B8A8_UNORM:		return GpuWrapApi::TEXFMT_R8G8B8A8_Unorm;
		case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:	return GpuWrapApi::TEXFMT_R8G8B8A8_Unorm_SRGB;
		case DXGI_FORMAT_R8G8B8A8_TYPELESS:		return GpuWrapApi::TEXFMT_R8G8B8A8_Uint_Typeless;
		case DXGI_FORMAT_R10G10B10A2_UNORM:		return GpuWrapApi::TEXFMT_R10G10B10A2_Unorm;
		case DXGI_FORMAT_R16_UNORM:				return GpuWrapApi::TEXFMT_R16_Unorm;
		case DXGI_FORMAT_R16_UINT:				return GpuWrapApi::TEXFMT_R16_Uint;
		case DXGI_FORMAT_R32_UINT:				return GpuWrapApi::TEXFMT_R32_Uint;
		case DXGI_FORMAT_R16G16_UINT:			return GpuWrapApi::TEXFMT_R16G16_Uint;
		case DXGI_FORMAT_R16G16_SINT:			return GpuWrapApi::TEXFMT_R16G16_Sint;
		case DXGI_FORMAT_R32G32_UINT:			return GpuWrapApi::TEXFMT_R32G32_Uint;
		case DXGI_FORMAT_R32G32B32A32_UINT:		return GpuWrapApi::TEXFMT_R32G32B32A32_Uint;
		case DXGI_FORMAT_R16G16B16A16_FLOAT:	return GpuWrapApi::TEXFMT_R16G16B16A16_Float;
		case DXGI_FORMAT_R16G16B16A16_UNORM:	return GpuWrapApi::TEXFMT_R16G16B16A16_Unorm;
		case DXGI_FORMAT_R16G16B16A16_UINT:		return GpuWrapApi::TEXFMT_R16G16B16A16_Uint;
		case DXGI_FORMAT_R11G11B10_FLOAT:		return GpuWrapApi::TEXFMT_R11G11B10_Float;
#ifdef RED_PLATFORM_DURANGO
		case DXGI_FORMAT_R10G10B10_6E4_A2_FLOAT:	return GpuWrapApi::TEXFMT_R10G10B10_6E4_A2_Float;
#endif
		case DXGI_FORMAT_R16G16_FLOAT:			return GpuWrapApi::TEXFMT_R16G16_Float;
		case DXGI_FORMAT_R32G32_FLOAT:			return GpuWrapApi::TEXFMT_R32G32_Float; //dex
		case DXGI_FORMAT_R32G32B32A32_FLOAT:	return GpuWrapApi::TEXFMT_R32G32B32A32_Float;
		case DXGI_FORMAT_R32_FLOAT:				return GpuWrapApi::TEXFMT_R32_Float;
		case DXGI_FORMAT_R16_FLOAT:				return GpuWrapApi::TEXFMT_R16_Float;
		case DXGI_FORMAT_R24G8_TYPELESS:		return GpuWrapApi::TEXFMT_D24S8;
		case DXGI_FORMAT_BC1_UNORM:				return GpuWrapApi::TEXFMT_BC1;
		case DXGI_FORMAT_BC1_UNORM_SRGB:		return GpuWrapApi::TEXFMT_BC1_SRGB;
		case DXGI_FORMAT_BC2_UNORM:				return GpuWrapApi::TEXFMT_BC2;
		case DXGI_FORMAT_BC2_UNORM_SRGB:		return GpuWrapApi::TEXFMT_BC2_SRGB;
		case DXGI_FORMAT_BC3_UNORM:				return GpuWrapApi::TEXFMT_BC3;
		case DXGI_FORMAT_BC3_UNORM_SRGB:		return GpuWrapApi::TEXFMT_BC3_SRGB;
		case DXGI_FORMAT_BC4_UNORM:				return GpuWrapApi::TEXFMT_BC4;
		case DXGI_FORMAT_BC5_UNORM:				return GpuWrapApi::TEXFMT_BC5;
		case DXGI_FORMAT_BC6H_UF16:				return GpuWrapApi::TEXFMT_BC6H_UNSIGNED;
		case DXGI_FORMAT_BC6H_SF16:				return GpuWrapApi::TEXFMT_BC6H_SIGNED;
		case DXGI_FORMAT_BC7_UNORM:				return GpuWrapApi::TEXFMT_BC7;
		case DXGI_FORMAT_BC7_UNORM_SRGB:		return GpuWrapApi::TEXFMT_BC7_SRGB;
		default:								RED_FATAL( "Invalid format or color space encoding: %d", format ); return GpuWrapApi::TEXFMT_NULL;
		}
	}

	constexpr Uint32 s_maxErrorStrLen = 1024;
	static char s_errorString[1024];
	static Uint32 s_errorStringLen = 0;

#if defined( VANGUARD_ASSET_HOST )
	static Uint32 s_vanguardAssetHostHardwareBC7CompressionCount = 0;

	extern "C" Uint32 VanguardAssetHost_GetHardwareBC7CompressionCount()
	{
		return s_vanguardAssetHostHardwareBC7CompressionCount;
	}
#endif

#define SET_ERROR_STRING( format, ...) s_errorStringLen = sprintf_s( s_errorString, s_maxErrorStrLen, format, ##__VA_ARGS__ )

	RENDER_DATA_API const String GetLastErrorString()
	{
		return String( s_errorString );
	}

	static Bool CompressBC6HBC7( /* in */ const GpuWrapApi::TextureDataDesc& srcImage, GpuWrapApi::eTextureFormat compressedFormat, GpuWrapApi::TextureDataDesc& compressedImage, red::UniqueBuffer &compressedBuffer)
	{
		// GPU processor has some problems with texture sizes 4x4 and below (yep 4x4 also). 
		// This happens for BC6 only. So in this case let's just fail and let software compressor 
		// do it's job. Note that I checked only square textures, but adding restriction also for rects.
		if ( (GpuWrapApi::TEXFMT_BC6H_UNSIGNED == compressedFormat || GpuWrapApi::TEXFMT_BC6H_SIGNED == compressedFormat) 
			 && (srcImage.width <= 4 || srcImage.height <= 4))
		{
			return false;
		}

		if (srcImage.width % 4 || srcImage.height % 4)
		{
			RED_LOG_WARNING("GpuApi: Image size is unsupported. Require 4 px alignment. Got %u x %u", srcImage.width, srcImage.height);
			return false;
		}

		ID3D11Device* device = nullptr;
		ID3D11DeviceContext* context = nullptr;
		D3D_FEATURE_LEVEL featureLevel;
		HRESULT hRet = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, nullptr, 0, D3D11_SDK_VERSION, &device, &featureLevel, &context);
		if (FAILED(hRet))
		{
			RED_HALT("Couldn't initialize DX11 device.");
			return false;
		}

		DirectX::Image image;
		image.format = Map(srcImage.format);
		image.width = srcImage.width;
		image.height = srcImage.height;
		image.pixels = static_cast<Uint8*>(srcImage.dataPtr);
		image.rowPitch = srcImage.rowPitch;
		image.slicePitch = srcImage.slicePitch;

		DirectX::TexMetadata metadata;
		metadata.arraySize = 1;
		metadata.depth = 1;
		metadata.dimension = DirectX::TEX_DIMENSION_TEXTURE2D;
		metadata.format = Map(srcImage.format);
		metadata.height = srcImage.height;
		metadata.width = srcImage.width;
		metadata.mipLevels = 1;
		metadata.miscFlags = 0;

		DirectX::ScratchImage bcImage;
		HRESULT hr = DirectX::Compress(device, image, Map(compressedFormat), 0, 1.0f, bcImage);
		if (SUCCEEDED(hr))
		{
			const Uint32 memoryNeeded = (Uint32)bcImage.GetPixelsSize();
			if (compressedBuffer.GetSize() < memoryNeeded)
			{
				compressedBuffer = red::CreateUniqueBuffer< rend::PoolImageProcessing >(memoryNeeded, 16);
			}

			Uint8 *bufferData = (Uint8*)compressedBuffer.Get();
			red::Memcpy(bufferData, bcImage.GetPixels(), bcImage.GetPixelsSize());
			compressedImage.dataPtr = bufferData;
			compressedImage.width = bcImage.GetMetadata().width;
			compressedImage.height = bcImage.GetMetadata().height;
			compressedImage.rowPitch = 4 * metadata.width * sizeof(Uint8);
			compressedImage.slicePitch = bcImage.GetPixelsSize();

			// AC: using given compressed format and skipping formats checks since there seems to be a bug in encoder.
			//     when targetFormat is bc6h then the encoded data is in given format, but cImage->format is set to bc7.
			compressedImage.format = compressedFormat; //Map( cImage->format );
										   // RED_FATAL_ASSERT( compressedFormat == Map( cImage->format ), "Compressed image format is different than requested" );

#if defined( VANGUARD_ASSET_HOST )
			if ( compressedFormat == GpuWrapApi::TEXFMT_BC7 || compressedFormat == GpuWrapApi::TEXFMT_BC7_SRGB )
			{
				++s_vanguardAssetHostHardwareBC7CompressionCount;
			}
#endif
		}

		// cleanup
		context->Release();
		device->Release();

		return SUCCEEDED(hr);
	}

#endif

	Bool CompressImage( const GpuWrapApi::TextureDataDesc& srcImage, GpuWrapApi::eTextureFormat compressedFormat, GpuWrapApi::TextureDataDesc& compressedImage, red::UniqueBuffer &compressedBuffer, GpuWrapApi::EImageCompressionHint compressionHint, Float alphaThreshold )
	{	
#ifdef RED_PLATFORM_WINPC

		if ( compressedFormat == GpuWrapApi::TEXFMT_BC6H_UNSIGNED || compressedFormat == GpuWrapApi::TEXFMT_BC6H_SIGNED || compressedFormat == GpuWrapApi::TEXFMT_BC7 || compressedFormat == GpuWrapApi::TEXFMT_BC7_SRGB )
		{
			if( CompressBC6HBC7( srcImage, compressedFormat, compressedImage, compressedBuffer ) )
			{
				return true;
			}
			// If the above (GPU side compression) did not succeeded - progress to software base. It will be far slower.
		}		

		// build flags
		DWORD compressionFlags = DirectX::TEX_COMPRESS_DEFAULT | DirectX::TEX_COMPRESS_PARALLEL;
		if ( GpuWrapApi::CIH_NormalmapRGB == compressionHint )
		{
			compressionFlags |= DirectX::TEX_COMPRESS_UNIFORM;
		}

		// feed the structure with source data
		DirectX::Image image;
		image.format		= Map( srcImage.format );
		image.width			= srcImage.width;
		image.height		= srcImage.height;
		image.pixels		= static_cast<Uint8*>( srcImage.dataPtr );
		image.rowPitch		= srcImage.rowPitch;
		image.slicePitch	= srcImage.slicePitch;

		red::UniqueBuffer tempBuffer;

#if 0 // This is actually not needed at all. Leaving for kinds from the future in case.

		// If the image size isn't a multiple of 4, allocate some temporary space for an image bumped up to a proper size.
		// Copy the source data into that, repeating the values at the edges, and then use that for compression instead.
		if ( IsTextureFormatDXT( compressedFormat ) && ( ( srcImage.width & 3 ) != 0 || ( srcImage.height & 3 ) != 0 ) )
		{
			const Uint32 paddedWidth	= red::AlignUp( (Uint32)srcImage.width, 4u );
			const Uint32 paddedHeight	= red::AlignUp( (Uint32)srcImage.height, 4u );

			const Uint32 paddedPitch	= CalculateTexturePitch( paddedWidth, srcImage.format );
			const Uint32 paddedTexSize	= CalculateTextureSize( paddedWidth, paddedHeight, srcImage.format );

			tempBuffer = red::CreateUniqueBuffer< rend::PoolImageProcessing >( paddedTexSize, 16 );
			const Uint8* srcData		= static_cast<Uint8*>(srcImage.dataPtr);
			Uint8* dstData				= static_cast<Uint8*>(tempBuffer.Get());

			const Uint32 texelSize		= CalculateTexturePitch( 1, srcImage.format );

			for ( Uint32 y = 0; y < srcImage.height; ++y )
			{
				Uint8* tempRow		= dstData + y * paddedPitch;
				const Uint8* srcRow = srcData + y * srcImage.rowPitch;
				red::Memcpy( tempRow, srcRow, srcImage.rowPitch );

				const Uint8* lastTexel = srcRow + srcImage.rowPitch - texelSize;

				// Repeat the last texel enough times to fill in the padding
				for ( Uint32 x = (Uint32)srcImage.rowPitch; ( x + texelSize ) <= paddedPitch; x += texelSize )
				{
					red::Memcpy( tempRow + x, lastTexel, texelSize );
				}
			}
			// Copy rows to fill out bottom padding.
			{
				const Uint8* tempRowLast = dstData + (srcImage.height-1) * paddedPitch;

				for ( Uint32 y = (Uint32)srcImage.height; y < paddedHeight; ++y )
				{
					red::Memcpy( dstData + y * paddedPitch, tempRowLast, paddedPitch );
				}
			}

			image.width			= paddedWidth;
			image.height		= paddedHeight;
			image.pixels		= dstData;
			image.rowPitch		= paddedPitch;
			image.slicePitch	= paddedTexSize;
		}

#endif

		// preform compression
		DirectX::ScratchImage tempImage;
		// TODO: integrate OpenMP and use DirectX::TEX_COMPRESS_PARALLEL
		HRESULT hr = DirectX::Compress( image, Map( compressedFormat ), compressionFlags, alphaThreshold, tempImage );

		if ( FAILED( hr ) )
		{
			RED_LOG_WARNING( "RenderData: Unable to compress image" );
			return false;
		}

		const DirectX::Image* img = tempImage.GetImage( 0, 0, 0 );

#if 0 // Helper for displaying intermediate textures
		{
			WCHAR buf[512];
			wsprintf( buf, L"C:\\dds\\dupa_%u_%u.dds", (Uint32)img->width, (Uint32)img->height );
			DirectX::SaveToDDSFile( *img, DirectX::DDS_FLAGS_NONE, buf );
		}
#endif

		compressedImage.width		= img->width;
		compressedImage.height		= img->height;
		compressedImage.rowPitch	= img->rowPitch;
		compressedImage.slicePitch	= img->slicePitch;
		compressedImage.format		= Map( img->format );
		RED_FATAL_ASSERT( compressedFormat == Map( img->format ), "Compressed image have different format than requested" );

		if( compressedBuffer.GetSize() < (Uint32)compressedImage.slicePitch )
		{
			compressedBuffer = red::CreateUniqueBuffer< rend::PoolImageProcessing >( (Uint32)compressedImage.slicePitch, 16 );
		}

		compressedImage.dataPtr = compressedBuffer.Get();

		red::Memcpy( compressedImage.dataPtr, img->pixels, img->slicePitch );

		return true;
#else
		return false;
#endif
	}

	Bool DecompressImage( const GpuWrapApi::TextureDataDesc& srcImage, GpuWrapApi::eTextureFormat decompressedFormat, GpuWrapApi::TextureDataDesc& decompressedImage, red::UniqueBuffer &compressedBuffer )
	{
#ifdef RED_PLATFORM_WINPC
		DirectX::Image image;
		image.format		= Map( srcImage.format );
		image.width			= srcImage.width;
		image.height		= srcImage.height;
		image.pixels		= static_cast<Uint8*>( srcImage.dataPtr );
		image.rowPitch		= srcImage.rowPitch;
		image.slicePitch	= srcImage.slicePitch;

		const Bool needMul4Fixup = IsTextureFormatDXT( srcImage.format ) && ( ( srcImage.width & 3 ) != 0 || ( srcImage.height & 3 ) != 0 );

		// If the image size isn't a multiple of 4, bump it up to a proper multiple and decompress that.
		if ( needMul4Fixup )
		{
			image.width = red::AlignUp( image.width, 4u );
			image.height = red::AlignUp( image.height, 4u );
		}

		DirectX::ScratchImage tempImage;
		HRESULT hr = DirectX::Decompress( image, Map( decompressedFormat ), tempImage );
		if ( FAILED( hr ) )
		{
			RED_LOG_WARNING( "RenderData: Unable to compress image" );
			return false;
		}

		const DirectX::Image* img		= tempImage.GetImage(0, 0, 0);
		decompressedImage.width			= img->width;
		decompressedImage.height		= img->height;
		decompressedImage.format		= Map( img->format );
		decompressedImage.rowPitch		= img->rowPitch;
		decompressedImage.slicePitch	= img->slicePitch;
		RED_FATAL_ASSERT( decompressedFormat == Map( img->format ), "Decompressed image have different format than requested" );

		// If we bumped up the resolution to a multiple of 4, now we need to undo that. So only copy the "unpadded" region
		// of the decompressed image to the output.
		if ( needMul4Fixup )
		{
			const Uint32 unpaddedWidth = (Uint32)srcImage.width;
			const Uint32 unpaddedHeight = (Uint32)srcImage.height;
			const Uint32 unpaddedPitch = CalculateUnpaddedTexturePitch( unpaddedWidth, decompressedImage.format );
			const Uint32 unpaddedTexSize = CalculateUnpaddedTextureSize( unpaddedWidth, unpaddedHeight, decompressedImage.format );

			decompressedImage.width = unpaddedWidth;
			decompressedImage.height = unpaddedHeight;
			decompressedImage.rowPitch = unpaddedPitch;
			decompressedImage.slicePitch = unpaddedTexSize;
			{
				Uint32 neededMemory = unpaddedTexSize;
				if ( compressedBuffer.GetSize() < neededMemory )
				{
					compressedBuffer = red::CreateUniqueBuffer< rend::PoolImageProcessing >( neededMemory, 16 );
				}
				decompressedImage.dataPtr = compressedBuffer.Get();
			}
			
			Uint8* destData = static_cast<Uint8*>(decompressedImage.dataPtr);
			for ( Uint32 y = 0; y < unpaddedHeight; ++y )
			{
				const Uint8* tempRow = &img->pixels[y*img->rowPitch];
				Uint8* destRow = &destData[y*unpaddedPitch];
				red::Memcpy( destRow, tempRow, unpaddedPitch );
			}
		}
		else
		{
			Uint32 neededMemory = (Uint32)img->slicePitch;
			if ( compressedBuffer.GetSize() < neededMemory )
			{
				compressedBuffer = red::CreateUniqueBuffer< rend::PoolImageProcessing >( neededMemory, 16 );
			}
			decompressedImage.dataPtr = compressedBuffer.Get();

			red::Memcpy( decompressedImage.dataPtr, img->pixels, img->slicePitch );
		}

		return true;
#else
		return false;
#endif
	}

	Bool ConvertBuffer( Uint32 width, Uint32 height, Uint32 depth, Uint32 sourcePitch, GpuWrapApi::eTextureFormat sourceFormat, const void* sourceData, Uint32 sourceDataSize, GpuWrapApi::eTextureFormat targetFormat, red::UniqueBuffer &outConvertedBuffer, GpuWrapApi::EImageCompressionHint compressionHint, Float alphaThreshold )
	{
		if ( width == 0 || height == 0 || depth == 0 || sourcePitch == 0 || nullptr == sourceData )
		{
			RED_HALT( "Invalid parameters to ConvertBuffer" );
			return false;
		}

		Bool ret = false;

		if ( sourceFormat == targetFormat )
		{
			const Uint32 targetPitch = GpuWrapApi::CalculateUnpaddedTexturePitch( width, targetFormat );
			const Uint32 rows = GpuWrapApi::IsTextureFormatDXT( sourceFormat ) ? Max( ( height + 3 ) / 4, 1u ) : height;

			Uint32 neededMemory = depth * rows * targetPitch;
			if ( outConvertedBuffer.GetSize() < neededMemory )
			{
				outConvertedBuffer = red::CreateUniqueBuffer< rend::PoolImageProcessing >( neededMemory, 16 );
			}

			for( Uint32 i = 0; i < depth; ++i )
			{
				Uint32 sourceLayerPitch = i * rows * sourcePitch;
				Uint32 targetLayerPitch = i * rows * targetPitch;
				for( Uint32 j = 0; j < rows; ++j )
				{
					const void* src = red::OffsetPtr( sourceData, sourceLayerPitch + j * sourcePitch );
					void* dst = red::OffsetPtr( outConvertedBuffer.Get(), targetLayerPitch + j * targetPitch );
					red::Memcpy( dst, src, targetPitch );
				}
			}
			
			ret = true;
		}

		// decompression
		else if ( GpuWrapApi::IsTextureFormatDXT( sourceFormat ) && GpuWrapApi::TEXFMT_R8G8B8A8_Unorm == targetFormat )
		{
			GpuWrapApi::TextureDataDesc imageToDecompress;
			imageToDecompress.dataPtr	= (Uint8*)sourceData;
			imageToDecompress.slicePitch = sourceDataSize;
			imageToDecompress.width		= width;
			imageToDecompress.height	= height;
			imageToDecompress.rowPitch	= sourcePitch;
			imageToDecompress.format	= sourceFormat;

			RED_ASSERT( GpuWrapApi::CIH_None == compressionHint );
			GpuWrapApi::TextureDataDesc decompressedImage;
			ret = GpuWrapApi::DecompressImage( imageToDecompress, targetFormat, decompressedImage, outConvertedBuffer );
		}

		// compression from RGBA or greyscale
		if ( GpuWrapApi::IsTextureFormatDXT( targetFormat ) && (
			GpuWrapApi::TEXFMT_R8G8B8A8_Unorm == sourceFormat || 
			GpuWrapApi::TEXFMT_R8G8B8A8_Unorm_SRGB == sourceFormat ||
			GpuWrapApi::TEXFMT_R32G32B32A32_Float == sourceFormat || 
			GpuWrapApi::TEXFMT_R16G16B16A16_Float == sourceFormat || 
			GpuWrapApi::TEXFMT_R16G16B16A16_Unorm == sourceFormat ||
			GpuWrapApi::TEXFMT_R8_Unorm == sourceFormat || 
			GpuWrapApi::TEXFMT_L8_Unorm == sourceFormat || 
			GpuWrapApi::TEXFMT_A8_Unorm == sourceFormat || 
			GpuWrapApi::TEXFMT_R16_Float == sourceFormat || 
			GpuWrapApi::TEXFMT_R32_Float == sourceFormat ) )
		{
			GpuWrapApi::TextureDataDesc imageToCompress;
			imageToCompress.dataPtr		= (Uint8*)sourceData;
			imageToCompress.slicePitch	= sourceDataSize;
			imageToCompress.width		= width;
			imageToCompress.height		= height;
			imageToCompress.rowPitch	= sourcePitch;
			imageToCompress.format		= sourceFormat;

			GpuWrapApi::TextureDataDesc compressedImage;
			ret = GpuWrapApi::CompressImage( imageToCompress, targetFormat, compressedImage, outConvertedBuffer, compressionHint, alphaThreshold );
		}

		return ret;
	}

#ifdef RED_PLATFORM_WINPC
	Bool ShouldDecompressBeforeSaving( const GpuWrapApi::eTextureFormat format, const GpuWrapApi::eTextureSaveFormat saveFormat )
	{
		Bool shouldDecompressBecauseOfSaveFormat = saveFormat == GpuWrapApi::SAVE_FORMAT_BMP || saveFormat == GpuWrapApi::SAVE_FORMAT_JPG || saveFormat == GpuWrapApi::SAVE_FORMAT_PNG || saveFormat == GpuWrapApi::SAVE_FORMAT_TGA;

		// list of formats that are savable to WIC in current DirectX Tex version
		// this can be determined by looking into _DXGIToWIC function in DirectXTex lib
		DXGI_FORMAT savableFormats[ ] = 
		{ 
			DXGI_FORMAT_R8G8B8A8_UNORM_SRGB,
			DXGI_FORMAT_D32_FLOAT,
			DXGI_FORMAT_D16_UNORM,
			DXGI_FORMAT_B8G8R8A8_UNORM_SRGB,
			DXGI_FORMAT_B8G8R8X8_UNORM_SRGB,
			DXGI_FORMAT_R32G32B32_FLOAT,
			DXGI_FORMAT_R32G32B32A32_FLOAT,
			DXGI_FORMAT_R16G16B16A16_FLOAT,
			DXGI_FORMAT_R16G16B16A16_UNORM,
			DXGI_FORMAT_R8G8B8A8_UNORM,
			DXGI_FORMAT_B8G8R8A8_UNORM,
			DXGI_FORMAT_B8G8R8X8_UNORM,
			DXGI_FORMAT_R10G10B10_XR_BIAS_A2_UNORM,
			DXGI_FORMAT_R10G10B10A2_UNORM,
			DXGI_FORMAT_R9G9B9E5_SHAREDEXP,
			DXGI_FORMAT_B5G5R5A1_UNORM,
			DXGI_FORMAT_B5G6R5_UNORM,
			DXGI_FORMAT_R32_FLOAT,
			DXGI_FORMAT_R16_FLOAT,
			DXGI_FORMAT_R16_UNORM,
			DXGI_FORMAT_R8_UNORM,
			DXGI_FORMAT_A8_UNORM,
			DXGI_FORMAT_R1_UNORM,
		};

		DXGI_FORMAT dxgiFormat = Map( format );

		Bool shouldDecompressBecauseOfTextureFormat = true;
		for ( Uint32 i = 0; i < RED_ARRAY_COUNT(savableFormats); ++i )
		{
			if ( dxgiFormat == savableFormats[i] )
			{
				// texture format can be saved directly, no need to decompress
				shouldDecompressBecauseOfTextureFormat = false;
				break;
			}
		}

		return shouldDecompressBecauseOfSaveFormat && shouldDecompressBecauseOfTextureFormat;
	}

	Bool SaveTextureToFile( const GpuWrapApi::TextureDataDesc& image, const Char* fileName, const GpuWrapApi::eTextureSaveFormat saveFormat )
	{
		Bool shouldDecompressBeforeSaving = ShouldDecompressBeforeSaving( image.format, saveFormat );
		red::UniqueBuffer decompressedData;

		DirectX::Image img;
		img.width = image.width;
		img.height = image.height;

		if ( shouldDecompressBeforeSaving )
		{
			GpuWrapApi::TextureDataDesc decompressedImage;
			bool decompOK = DecompressImage( image, GpuWrapApi::TEXFMT_R8G8B8A8_Unorm, decompressedImage, decompressedData );
			RED_ASSERT( decompOK, "Error decompressing texture before saving." );
			img.pixels = static_cast<Uint8*>( decompressedData.Get() );
			img.rowPitch = decompressedImage.rowPitch;
			img.slicePitch = decompressedImage.slicePitch;
			img.format = Map( decompressedImage.format );
		}
		else
		{
			// use given data
			img.rowPitch = image.rowPitch;
			img.slicePitch = image.slicePitch;
			img.format = Map( image.format );
			img.pixels = static_cast<Uint8*>( image.dataPtr );
		}

		HRESULT hr = S_FALSE;
		switch ( saveFormat )
		{
		case GpuWrapApi::SAVE_FORMAT_DDS:
			hr = DirectX::SaveToDDSFile( img, DirectX::DDS_FLAGS_NONE, fileName );
			break;
		case GpuWrapApi::SAVE_FORMAT_BMP:
			hr = DirectX::SaveToWICFile( img, DirectX::WIC_FLAGS_NONE, DirectX::GetWICCodec(DirectX::WIC_CODEC_BMP), fileName );
			break;
		case GpuWrapApi::SAVE_FORMAT_JPG:
			hr = DirectX::SaveToWICFile( img, DirectX::WIC_FLAGS_NONE, DirectX::GetWICCodec(DirectX::WIC_CODEC_JPEG), fileName );
			break;
		case GpuWrapApi::SAVE_FORMAT_PNG:
			hr = DirectX::SaveToWICFile( img, DirectX::WIC_FLAGS_NONE, DirectX::GetWICCodec(DirectX::WIC_CODEC_PNG), fileName );
			break;
		case GpuWrapApi::SAVE_FORMAT_TGA:
			hr = DirectX::SaveToTGAFile( img, fileName );
			break;
		}		

		if ( FAILED( hr ) )
		{
			if ( hr == ERROR_NOT_SUPPORTED )
			{
				RED_ASSERT( false, "Error exporting texture: format not supported." );
			}
			else
			{
				RED_ASSERT( false, "Error exporting texture." );
			}
			return false;
		}

		return true;
	}
#endif

#ifndef NO_TEXTURE_IMPORT

	class CubeLutLoader
	{
	public:
		static HRESULT Load( red::DynArray<Float>& outLutData, Uint32& outSize, const char* filePath )
		{
			std::ifstream infile;
			infile.open( filePath );

			// read file data line by line
			const char NewlineCharacter = '\n';
			char lineSeparator = NewlineCharacter;

			// sniff use of legacy lineSeparator
			const char CarriageReturnCharacter = '\r';
			for ( int i = 0; i < 255; i++ )
			{
				char inc = infile.get();
				if ( inc == NewlineCharacter ) break;

				if ( inc == CarriageReturnCharacter )
				{
					if ( infile.get() == NewlineCharacter )
					{
						break;
					}
					lineSeparator = CarriageReturnCharacter;
					RED_LOG_WARNING( "CubeLutLoader: File '%s' uses non-compliant line separator \\r (0x0D)", filePath );
					break;
				}
				if ( i > 250 )
				{
					RED_LOG_ERROR( "CubeLutLoader: File '%s' is currupted", filePath );
					return E_FAIL;
				}
			}
			infile.seekg( 0 );
			infile.clear();

			std::string title;
			Vector3 domainMin( 0.0f, 0.0f, 0.0f );
			Vector3 domainMax( 1.0f, 1.0f, 1.0f );

			int N = 0, CntTitle = 0, CntSize = 0, CntMin = 0, CntMax = 0;

			for ( ;; )
			{
				bool eof = false;

				std::string lineOfText;

				//Skip empty lines and comments
				const char CommentMarker = '#';
				while ( lineOfText.size() == 0 || lineOfText[ 0 ] == CommentMarker )
				{
					if ( infile.eof() )
					{
						eof = true;
						break;
					}

					getline( infile, lineOfText, lineSeparator );
					if ( infile.fail() )
					{
						eof = true;
						break;
					}
				}

				if ( eof )
				{
					break;
				}

				// Parse keywords and parameters
				std::istringstream line( lineOfText );
				std::string keyword;
				line >> keyword;
				
				if ( !keyword.empty() && keyword[ 0 ] >= '0' && keyword[ 0 ] <= '9' )
				{
					line.seekg( 0 );

					if ( N == 0 )
					{
						RED_LOG_ERROR( "CubeLutLoader: File '%s' is currupted: LUT data must come after LUT_3D_SIZE marker", filePath );
						return E_FAIL;
					}

					if ( outLutData.Size() >= outLutData.Capacity() )
					{
						RED_LOG_ERROR( "CubeLutLoader: File '%s' is currupted: Too many LUT data entries", filePath );
						return E_FAIL;
					}

					for ( Uint32 i = 0; i < 3; ++i )
					{
						Float value;
						line >> value;
						if ( line.fail() )
						{
							RED_LOG_ERROR( "CubeLutLoader: Failed to read LUT data from '%s'", filePath );
							return E_FAIL;
						}

						outLutData.PushBack( value );
					}
				}
				else if ( keyword == "TITLE" && CntTitle++ == 0 )
				{
					const char QUOTE = '"';
					char startOfTitle;
					line >> startOfTitle;
					if ( startOfTitle != QUOTE )
					{
						RED_LOG_ERROR( "CubeLutLoader: File '%s' is currupted: Missing title quote", filePath );
						return E_FAIL;
					}
					getline( line, title, QUOTE );
				}
				else if ( keyword == "DOMAIN_MIN" && CntMin++ == 0 )
				{
					line >> domainMin[ 0 ] >> domainMin[ 1 ] >> domainMin[ 2 ];
				}
				else if ( keyword == "DOMAIN_MAX" && CntMax++ == 0 )
				{
					line >> domainMax[ 0 ] >> domainMax[ 1 ] >> domainMax[ 2 ];
				}
				else if ( keyword == "LUT_1D_SIZE" && CntSize++ == 0 )
				{
					RED_LOG_ERROR( "CubeLutLoader: File '%s' is uses 1D LUT which is not supported", filePath );
					return E_FAIL;
				}
				else if ( keyword == "LUT_3D_SIZE" && CntSize++ == 0 )
				{
					line >> N;
					if ( N < 2 || N > 256 )
					{
						RED_LOG_ERROR( "CubeLutLoader: File '%s' is currupted: Lut size out of range", filePath );
						return E_FAIL;
					}
					outLutData.Reserve( N * N * N * 3 );
				}
				else
				{
					RED_LOG_ERROR( "CubeLutLoader: File '%s' is currupted: Unknown or repeated symbol", filePath );
					return E_FAIL;
				}

				if ( line.fail() )
				{
					RED_LOG_ERROR( "CubeLutLoader: Failed to read from '%s'", filePath );
					return E_FAIL;
				}
			}

			if ( CntSize == 0 )
			{
				RED_LOG_ERROR( "CubeLutLoader: File '%s' is currupted: Lut size out of range", filePath );
				return E_FAIL;
			}
			if ( domainMin[ 0 ] >= domainMax[ 0 ] || domainMin[ 1 ] >= domainMax[ 1 ] || domainMin[ 2 ] >= domainMax[ 2 ] )
			{
				RED_LOG_ERROR( "CubeLutLoader: File '%s' is currupted: Domain bounds are reversed", filePath );
				return E_FAIL;
			}

			if ( outLutData.Size() < outLutData.Capacity() )
			{
				RED_LOG_ERROR( "CubeLutLoader: File '%s' is currupted: Not enough LUT data entries", filePath );
				return E_FAIL;
			}

			outSize = N;
			return NOERROR;
		}

	private:
		static bool ReadLine( std::ifstream& infile, char lineSeparator, std::string& outLine )
		{
			//Skip empty lines and comments
			const char CommentMarker = '#';
			outLine.clear();
			while ( outLine.size() == 0 || outLine[ 0 ] == CommentMarker )
			{
				if ( infile.eof() )
				{
					return false;
				}
				getline( infile, outLine, lineSeparator );
				if ( infile.fail() )
				{
					return false;
				}
			}
			return true;
		}
	};

	Bool ImportTexture( const red::AbsolutePath importPath, GpuWrapApi::eTextureImportFormat format, /*out*/red::UniqueBuffer &dst, /*out*/GpuWrapApi::TextureDataDesc& desc, Bool shouldBePowerOf2 /*=true*/, Bool isImportingGenericData /*=false*/, Bool applyFlip /*=true*/)
	{
		// Read bitmap info
		DirectX::TexMetadata metadata;
		red::Memset( &metadata, 0, sizeof( metadata ) );

		HRESULT hr = S_FALSE;
		switch ( format )
		{
		case GpuWrapApi::TIF_DDS:
			hr = DirectX::GetMetadataFromDDSFile( importPath.ToUtf16String().AsChar(), DirectX::DDS_FLAGS_NONE, metadata );
			break;
		case GpuWrapApi::TIF_TGA:
			hr = DirectX::GetMetadataFromTGAFile( importPath.ToUtf16String().AsChar(), metadata );
			break;
		case GpuWrapApi::TIF_WIC:
			// PNGs (and possibly others?) can be imported in BGR order, but WIC_FLAGS_FORCE_RGB forces it into RGB, which is good for us!
			hr = DirectX::GetMetadataFromWICFile( importPath.ToUtf16String().AsChar(), DirectX::WIC_FLAGS_FORCE_RGB|DirectX::WIC_FLAGS_ALL_FRAMES, metadata );
			break;
		case GpuWrapApi::TIF_EXR:
		case GpuWrapApi::TIF_CUBE:
			metadata.format = DXGI_FORMAT_R32G32B32A32_FLOAT;
			break;
		}

		if (hr == HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND))
		{
			SET_ERROR_STRING( "File not found.");
			return false;
		}

		Bool isColorSpaceEncodingSupported = Map( metadata.format ) != GpuWrapApi::TEXFMT_NULL;
		if ( FAILED( hr ) || !isColorSpaceEncodingSupported )
		{			
			SET_ERROR_STRING( "Unknown texture import format or unsupported colour space encoding!" );
			return false;
		}

		// Texture size should be power of 2 !
		Bool isPow2Width = red::IsPowerOf2( static_cast< Int32 >( metadata.width ) );
		Bool isPow2Height = red::IsPowerOf2( static_cast< Int32 >( metadata.height ) );

		if ( format == GpuWrapApi::TIF_DDS )
		{
			if ( metadata.width < 1 || metadata.height < 1 )
			{
				SET_ERROR_STRING( "DDS and/or EXR Texture size should be a positive value." );
				return false;
			}

			if ( !isImportingGenericData )
			{
				if ( (metadata.width & 3) != 0 || (metadata.height & 3 ) != 0 )
				{
					SET_ERROR_STRING( "DDS and/or EXR Texture size should be a multiple of four" );
					return false;
				}
			}
		}
		else if ( format == GpuWrapApi::TIF_EXR )
		{
			// empty
		}
		else if ( format == GpuWrapApi::TIF_CUBE )
		{
			// empty
		}
		else if ( shouldBePowerOf2 && ( !isPow2Width || !isPow2Height ) )
		{
			RED_LOG_WARNING( "Texture size should be power of two and it is [%d x %d]", metadata.width, metadata.height );
		}

		DirectX::ScratchImage image;
		Float* exrImage;
		red::DynArray<Float> cubeLutData{ rend::PoolImageProcessing() };
		hr = S_FALSE;
		switch ( format )
		{
		case GpuWrapApi::TIF_DDS:
			hr = DirectX::LoadFromDDSFile( importPath.ToUtf16String().AsChar(), DirectX::DDS_FLAGS_NONE, NULL, image );
			break;
		case GpuWrapApi::TIF_TGA:
			hr = DirectX::LoadFromTGAFile( importPath.ToUtf16String().AsChar(), NULL, image );
			break;
		case GpuWrapApi::TIF_WIC:
			hr = DirectX::LoadFromWICFile( importPath.ToUtf16String().AsChar(), DirectX::WIC_FLAGS_FORCE_RGB|DirectX::WIC_FLAGS_ALL_FRAMES, NULL, image );
			break;
		case GpuWrapApi::TIF_EXR:
			{
				Int32 width, height;
				const char* err;
				hr = LoadEXR( &exrImage, &width, &height, importPath.AsChar(), &err );
				metadata.width = width;
				metadata.height = height;
				metadata.depth = 1;
			}
			break;
		case GpuWrapApi::TIF_CUBE:
			{
				Uint32 size = 0;
				hr = CubeLutLoader::Load( cubeLutData, size, importPath.AsChar() );
				if ( SUCCEEDED( hr ) )
				{
					metadata.width = metadata.height = metadata.depth = size;
				}
			}
			break;
		} 

		if ( FAILED( hr ) )
		{
			SET_ERROR_STRING( "Unable to create texture from file" );
			return false;
		}

		const Bool isExr = format == GpuWrapApi::TIF_EXR;
		const Bool isCubeLut = format == GpuWrapApi::TIF_CUBE;
		const Bool isCompressed = GpuWrapApi::IsTextureFormatDXT( Map( metadata.format ) );

		// Why flip the textures vertically?
		// this is the MS coords: https://msdn.microsoft.com/en-us/library/windows/desktop/bb206245(v=vs.85).aspx
		//
		// 0,0--------1,0
		// |
		// |
		// |
		// |
		// 0,1--------1,1
		//
		// this is how ALL the 3d packages (Max, Maya) does UVs:
		// 0,1--------1,1
		// |
		// |
		// |
		// |
		// 0,0--------1,0

		DirectX::ScratchImage vertFlippedImage;
		if ( !isExr && !isCubeLut )
		{
			// This can stay like this only until the proper multilayer masks resave. After that the applyFlip parameter has to be removed and all images
			// have to be always flipped.
			if ( !isCompressed && applyFlip )
			{
				hr = DirectX::FlipRotate( image.GetImages(), image.GetImageCount(), image.GetMetadata(), DirectX::TEX_FR_FLIP_VERTICAL, vertFlippedImage );
				if ( FAILED( hr ) )
				{
					SET_ERROR_STRING( "Unable to flip the texture" );
					return false;
				}
			}
			else
			{
				std::swap( vertFlippedImage, image );
			}
		}
		
		desc.width		= static_cast< Uint32 >( metadata.width );
		desc.height		= static_cast< Uint32 >( metadata.height );
		desc.depth		= static_cast< Uint32 >( metadata.depth );
		desc.numSlices	= isExr ? 1 : static_cast< Uint32 >( vertFlippedImage.GetImageCount() );
		desc.format		= Map( metadata.format );

		if ( format == TIF_EXR )
		{
			Int32 size = static_cast< Uint32 >( 4 * metadata.width * metadata.height * sizeof( Float ) );

			RED_ASSERT( !dst, "Destination memory already allocated" );
			dst = red::CreateUniqueBuffer< rend::PoolImageProcessing >( size, 16 );
			RED_ASSERT( dst, "Destination memory can't be allocated" );

			desc.rowPitch	= static_cast< Uint32 >( 4 * metadata.width * sizeof( Float ) );
			desc.slicePitch = static_cast< Uint32 >( size );
			desc.dataPtr	= dst.Get();

			red::Memcpy( dst.Get(), exrImage, static_cast< Uint32 >( size ) );

			free( exrImage );
		}
		else if ( format == TIF_CUBE )
		{
			RED_ASSERT( !dst, "Destination memory already allocated" );
			dst = red::CreateUniqueBuffer< rend::PoolImageProcessing >( static_cast<Uint32>( metadata.width * metadata.height * metadata.depth * 4 * sizeof(Float) ), 16 );
			RED_ASSERT( dst, "Destination memory can't be allocated" );

			// convert from RGB to RGBA and transpose
			for ( Uint32 b = 0; b < metadata.depth; ++b )
			{
				for ( Uint32 g = 0; g < metadata.height; ++g )
				{
					for ( Uint32 r = 0; r < metadata.width; ++r )
					{
						Float* destBuffer = (Float*)dst.Get() + 4 * ( metadata.width * metadata.height * r + metadata.width * g + b );
						destBuffer[ 0 ] = cubeLutData[ 3 * (Uint32)( metadata.width * metadata.height * r + metadata.width * g + b ) + 0 ];
						destBuffer[ 1 ] = cubeLutData[ 3 * (Uint32)( metadata.width * metadata.height * r + metadata.width * g + b ) + 1 ];
						destBuffer[ 2 ] = cubeLutData[ 3 * (Uint32)( metadata.width * metadata.height * r + metadata.width * g + b ) + 2 ];
						destBuffer[ 3 ] = 1.0f;
					}
				}
			}

			desc.rowPitch = static_cast<Uint32>( 4 * metadata.width * sizeof( Float ) );
			desc.slicePitch = static_cast<Uint32>( 4 * metadata.width * metadata.height * sizeof( Float ) );
			desc.dataPtr = dst.Get();
		}
		else if ( format == TIF_DDS )
		{
			// #TODO: implement a codepath where we are importing a DDS texture with full mipmap chain and we just load the data as-is, without any decompression and compression in the process.

			const DirectX::Image* loadedImage = vertFlippedImage.GetImage(0, 0, 0);

			RED_ASSERT( !dst, "Destination memory already allocated" );
			dst = red::CreateUniqueBuffer< rend::PoolImageProcessing >( static_cast< Uint32 >( loadedImage->slicePitch ), 16 );
			RED_ASSERT( dst, "Destination memory can't be allocated" );

			desc.rowPitch	= static_cast< Uint32 >( loadedImage->rowPitch );
			desc.slicePitch	= static_cast< Uint32 >( loadedImage->slicePitch );
			desc.dataPtr	= dst.Get();

			red::Memcpy( dst.Get(), loadedImage->pixels, static_cast< Uint32 >( loadedImage->slicePitch ) );

			if ( isCompressed )
			{
				// decompress before returning data, further calls will take care of compressing the mips
				TextureDataDesc decompressedDesc;
				red::UniqueBuffer decompressedBuffer;
				if ( DecompressImage( desc, TEXFMT_R8G8B8A8_Unorm, decompressedDesc, decompressedBuffer ) )
				{
					desc = decompressedDesc;
					dst.Swap( decompressedBuffer );
					decompressedBuffer.Reset();				
				}
				else
				{
					SET_ERROR_STRING( "Error decompressing DDS texture data." );
					return false;
				}
			}
		}
		else
		{
			const DirectX::Image* loadedImage = vertFlippedImage.GetImage(0, 0, 0);

			RED_ASSERT(!dst, "Destination memory already allocated");
			dst = red::CreateUniqueBuffer< rend::PoolImageProcessing >((Uint32)(loadedImage->slicePitch*vertFlippedImage.GetImageCount()), 16);
			RED_ASSERT(dst, "Destination memory can't be allocated");

			desc.rowPitch = static_cast<Uint32>(loadedImage->rowPitch);
			desc.slicePitch = static_cast<Uint32>(loadedImage->slicePitch);
			desc.dataPtr = dst.Get();

			red::Memcpy(dst.Get(), loadedImage->pixels, static_cast<Uint32>(loadedImage->slicePitch*vertFlippedImage.GetImageCount()));
		}

		return true;
	}

	RENDER_DATA_API Bool SaveImageAsEXR(const float *data, int width, int height, int components, const char *outfilename)
	{
		return ( TINYEXR_SUCCESS == SaveEXR( data, width, height, components, outfilename ) );
	}

#endif

} // end namespace GpuWrapApi
