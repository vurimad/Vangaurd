/**
* Copyright (c) 2016 CDProjekt Red, Inc. All Rights Reserved.
*/

/// Original code (C) RenderLab 2014 by Tomasz "RexDex" Jonarski
/// Copied with permission from author by author :)

#pragma once

namespace img
{

	class Image;
	typedef red::SharedPtr<Image> ImagePtr;

	class ImageAccess;
	class ImagePixel;
	class ImageIterator;

	namespace utils
	{
		enum FilteringMode
		{
			FM_Clamp,
			FM_Wrap
		};

		/// Unpack single pixel, ultra slow
		extern REDIMAGE_API void UnpackPixel_SLOW( const Image& image, const ImageAccess& access, const ImageIterator& it, ImagePixel& outPixel );

		/// Pack single pixel, ultra slow
		extern REDIMAGE_API void PackPixel_SLOW( Image& image, ImageAccess& access, const ImageIterator& it, const ImagePixel& pixel );

		/// Swap/Copy/Discard channels
		/// numNewChannels specify new number of channels in the image and the newChannelMapping table specified the remap table.
		/// If the value in the remap table is negative (<0 i.e -1) then the channel is reset to it's default value taken from the defaultChannelValues pixel
		/// NOTE: if the number of channels stays the same then the image memory is NOT reallocated and the same image is returned, if it's changed then a new image is created
		extern REDIMAGE_API ImagePtr ConvertChannels( ImagePtr source, const Uint32 numNewChannels, const Int32* newChannelMapping, const ImagePixel& defaultChannelValues );

		// Fill alpha channel with value
		extern REDIMAGE_API void FillAlpha( ImagePtr& source, const Float val );

		/// Run function on the whole image using given iterator
		template< typename Func >
		RED_FORCE_INLINE void ProcessImageInplace( ImageAccess& image, ImageIterator& it, Func& func );

		/// Run function on the whole read only image using given iterator
		template< typename Func >
		RED_FORCE_INLINE void ProcessImage( const ImageAccess& image, ImageIterator& it, Func& func );

		/// Run kernel on the whole read only image using given iterator
		template< typename Kernel >
		RED_FORCE_INLINE void FilterImageInplace( ImageAccess& image, ImageIterator& it );

		/// Run kernel on the whole read only image using given iterator
		template< typename Kernel >
		RED_FORCE_INLINE void FilterImage( const ImageAccess& src, ImageAccess& dest, ImageIterator& it );

		/// Scale image down twice (mipmap filter)
		/// NOTE: all colors are filtered
		/// NOTE: all image dimensions are downscaled (this is a true mip map, so a 3D image will be dowscaled in 3 dimensions)
		extern REDIMAGE_API ImagePtr DownscaleBox( const ImagePtr& source, FilteringMode filerMode = FM_Clamp );

		/// Scale image down twice (mipmap filter)
		/// NOTE: all colors are filtered
		/// NOTE: all image dimensions are downscaled (this is a true mip map, so a 3D image will be dowscaled in 3 dimensions)
		extern REDIMAGE_API ImagePtr DownscaleGuass( const ImagePtr& source, FilteringMode filerMode = FM_Clamp );

		/// Resample bicubic
		extern REDIMAGE_API ImagePtr ResampleBicubic( const ImagePtr& source, Uint32 width, Uint32 height );

		// Clips image to some size
		extern REDIMAGE_API ImagePtr Clip( const ImagePtr& source, Uint32 offsetX, Uint32 offsetY, Uint32 width, Uint32 height );

		/// Propagate dest grayscale color to all color channels of source
		extern REDIMAGE_API void PropagateGrayscale( ImagePtr& dest, const ImagePtr& source );

		/// Transform image's color space inplace
		extern REDIMAGE_API void TransformColorSpace( ImagePtr& source, Float csExp );

		/// Stretch source texture's color range Inplace
		extern REDIMAGE_API void StretchHistogram( ImagePtr& source, math::Vector3& outHistBiasMulCoef, math::Vector3& outHistBiasAddCoef );
		
		/// Flip cube face
		extern REDIMAGE_API ImagePtr FlipRect( const ImagePtr& source, Bool faceFlipX, Bool faceFlipY, Bool faceRotate );
		extern REDIMAGE_API ImagePtr Flip( const ImagePtr& source, Bool flipX, Bool flipY );

		/// Transform image's color space inplace
		extern REDIMAGE_API ImagePtr ConvertFormat( const ImagePtr& source, const PixelFormat destFormat );

		/// Get coverage of image channel values that are greater than threshold
		extern REDIMAGE_API float GetChannelCoverage(const ImagePtr& source, const Uint8 channel, const Float threshold);

		/// Get min stored value of specific channel
		extern REDIMAGE_API float GetChannelMinValue( const ImagePtr& source, const Uint8 channel );

		/// Used for boosting alpha channel in subsequent mipmaps to preserve alpha coverage from base mipmap
		extern REDIMAGE_API void MultiplyChannelValue(const ImagePtr& source, const Uint8 channel, const Float factor);

		/// Map equirectangular panorama source to rectangular cube face
		extern REDIMAGE_API ImagePtr Equirectangular2Cube( const ImagePtr& source, Uint16 face, Uint32 destWidth, Uint32 destHeight );
	} // utils

} // img

#include "imageUtils.inl"