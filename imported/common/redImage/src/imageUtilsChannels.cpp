/**
* Copyright (c) 2016 CDProjekt Red, Inc. All Rights Reserved.
*/

/// Original code (C) RenderLab 2014 by Tomasz "RexDex" Jonarski
/// Copied with permission from author by author :)

#include "build.h"

#include "image.h"
#include "imageIterator.h"
#include "imageAccess.h"
#include "imageUtils.h"
#include "../../redSystem/include/redThreadsThread.h"

namespace img
{
	namespace utils
	{
		static ImagePtr CreateCompatibleImage( const Image& source, const Uint32 numNewChannels )
		{
			Image::SizeInfo size = source.GetSize();
			size.m_channels = numNewChannels;

			return Image::CreateEmpty( source.GetPixelFormat(), size, source.GetGammaExp() );
		}

		static Bool IsIdentityMapping( const Uint32 numNewChannels, const Int32* newChannelMapping )
		{
			for ( Uint32 i=0; i<numNewChannels; ++i )
			{
				if ( newChannelMapping[i] != (Int32)i )
					return false;
			}

			return true;
		}

		class ChannelMapper
		{
		public:
			template< typename Format >
			RED_FORCE_INLINE static void Process( const ImageAccess& src, ImageAccess& dest, const ImageIterator& it )
			{
				ImagePixel srcPixel;
				src.Unpack< Format >( it, srcPixel );

				ImagePixel destPixel;

				if ( Format::CHANNELS >= 1 )
					destPixel[0] = ( st_newChannelMapping[0] < Format::CHANNELS ) ? srcPixel[ st_newChannelMapping[0] ] : (*st_defaultChannelValues)[0];

				if ( Format::CHANNELS >= 2 )
					destPixel[1] = ( st_newChannelMapping[1] < Format::CHANNELS ) ? srcPixel[ st_newChannelMapping[1] ] : (*st_defaultChannelValues)[1];

				if ( Format::CHANNELS >= 3 )
					destPixel[2] = ( st_newChannelMapping[2] < Format::CHANNELS ) ? srcPixel[ st_newChannelMapping[2] ] : (*st_defaultChannelValues)[2];

				if ( Format::CHANNELS >= 4)
					destPixel[3] = ( st_newChannelMapping[3] < Format::CHANNELS ) ? srcPixel[ st_newChannelMapping[3] ] : (*st_defaultChannelValues)[3];

				dest.Pack< Format >( it, destPixel );
			}

			static void Setup( const Int32* newChannelMapping, const ImagePixel& defaultChannelValues )
			{
				st_newChannelMapping = (const Uint32*) newChannelMapping;
				st_defaultChannelValues = &defaultChannelValues;
			}

		private:
			static const Uint32*		st_newChannelMapping;
			static const ImagePixel*	st_defaultChannelValues;
		};

		const Uint32* ChannelMapper::st_newChannelMapping = nullptr;
		const ImagePixel*	ChannelMapper::st_defaultChannelValues = nullptr;
		static red::Mutex st_convertChannelsMutex;

		REDIMAGE_API ImagePtr ConvertChannels( ImagePtr source, const Uint32 numNewChannels, const Int32* newChannelMapping, const ImagePixel& defaultChannelValues )
		{
			// no source
			if ( !source )
				return ImagePtr();

			// no channels in the target image
			if ( numNewChannels < 1 )
				return ImagePtr();

			// we don't support more than 4 channels ATM
			RED_ASSERT( numNewChannels <= 4, "Unsupported number of image channels" );
			if ( numNewChannels > 4 )
				return ImagePtr();

			// We're using static state variable for channel conversion :<
			red::ScopedLock< red::Mutex > lock( st_convertChannelsMutex );

			// identity mapping ? if so we have nothing to do
			if ( numNewChannels == source->GetSize().m_channels && IsIdentityMapping( numNewChannels, newChannelMapping ) )
				return source;

			// should we create new image ?
			const Uint32 numSourceChannels = source->GetSize().m_channels;
			const Bool createNewImage = ( numNewChannels != numSourceChannels );
			auto target = createNewImage ? CreateCompatibleImage( *source, numNewChannels ) : source;

			// create access points to image
			const ImageAccess srcAccess( *source );
			ImageAccess targetAccess( *target );

			// iterate over the whole image processing pixels
			// NOTE: images are compatible so we can use the same iterator
			ImageIterator it = source->GetIterator();
			ChannelMapper::Setup( newChannelMapping, defaultChannelValues );
			utils::FilterImage< ChannelMapper >( srcAccess, targetAccess, it );

			// return new (swapped) image
			return target;
		}

		REDIMAGE_API void FillAlpha( ImagePtr& source, const Float val )
		{
			if ( !source )
				return;

			RED_ASSERT( source->GetSize().m_channels < 4, "must have 4 channels for alpha to exist" );
			if ( source->GetSize().m_channels != 4 )
				return;

			ImageAccess src( *source );

			ImageIterator it = source->GetIterator();
			auto func =  [val](ImageIterator& it, ImagePixel& pixel)->Bool { pixel[3] = val; return true; };
			img::utils::ProcessImageInplace( src, it, func );
		}

		class GrayscalePropagator
		{
		public:
			template< typename Format >
			RED_FORCE_INLINE static void Process( const ImageAccess& src, ImageAccess& dest, const ImageIterator& it )
			{
				ImagePixel srcPixel;
				src.Unpack< Format >( it, srcPixel );

				ImagePixel dstPixel;
				dstPixel[0] = srcPixel[0];
				dstPixel[1] = srcPixel[0];
				dstPixel[2] = srcPixel[0];
				dstPixel[3] = 1.0f;

				dest.Pack< Format >( it, dstPixel );
			}
		};

		REDIMAGE_API void PropagateGrayscale( ImagePtr& dest, const ImagePtr& source )
		{
			if ( !source || !dest )
				return;

			const Image::SizeInfo& s = source->GetSize();
			const Image::SizeInfo& d = dest->GetSize();

			RED_ASSERT( s.m_channels == 1, "source must be grayscale" );
			if( s.m_channels != 1 )
				return;

			RED_ASSERT( s.m_width == d.m_width && s.m_height == d.m_height && s.m_depth == d.m_depth, "both images must be the same size" );
			if ( s.m_width != d.m_width || s.m_height != d.m_height || s.m_depth != d.m_depth )
				return;

			const ImageAccess src( *source );
			ImageAccess dst( *dest );

			ImageIterator it = source->GetIterator();
			img::utils::FilterImage<GrayscalePropagator>( src, dst, it );
		}

		REDIMAGE_API void TransformColorSpace( ImagePtr& source, Float csExp )
		{
			if ( !source )
				return;

			ImageAccess src( *source );

			// When we're transforming from gamma_X to gamma_Y we're practically doing operation
			// of going to linear -> rgb^X and then back to gamma -> rgb^(1.0/Y) ==> gamma_X -> gamma_Y <==> rgb^(X/Y)
			Float currentGammaExp = source->GetGammaExp();
			Float transformExp = currentGammaExp / csExp;

			ImageIterator it1 = source->GetIterator();
			auto func = [transformExp](ImageIterator& it, ImagePixel& pixel)->Bool
			{ 
				pixel.PowInplace( transformExp );
				return true; 
			};
			img::utils::ProcessImageInplace( src, it1, func );

			source->SetGammaExp( csExp );
		}

		class FormatConvert
		{
		public:
			template< typename Format >
			RED_FORCE_INLINE static void Process( const ImageAccess& src, ImageAccess& dest, const ImageIterator& it )
			{
				ImagePixel srcPixel;
				src.Unpack< Format >( it, srcPixel );

				switch( st_newFormat )
				{
				case PixelFormat::Format_Uint8:
					dest.Pack< ImageFormatUint8_UNORM<Format::CHANNELS> >( it, srcPixel );
					break;
				case PixelFormat::Format_Uint16:
					dest.Pack< ImageFormatUint16_UNORM<Format::CHANNELS> >( it, srcPixel );
					break;
				case PixelFormat::Format_Float16:
					dest.Pack< ImageFormatFloat16<Format::CHANNELS> >( it, srcPixel );
					break;
				case PixelFormat::Format_Uint32:
				case PixelFormat::Format_Float32:
					dest.Pack< ImageFormat32_DIRECT<Format::CHANNELS> >( it, srcPixel );
					break;
				}
			}

			static void Setup( const PixelFormat newFormat )
			{
				st_newFormat = newFormat;
			}

		private:
			static PixelFormat st_newFormat;
		};

		PixelFormat FormatConvert::st_newFormat = PixelFormat::Format_Float32;
		static red::Mutex st_convertFormatMutex;

		REDIMAGE_API ImagePtr ConvertFormat( const ImagePtr& source, const PixelFormat destFormat )
		{
			if ( !source )
				return ImagePtr();

			if( source->GetPixelFormat() == destFormat )
				return source;

			// We're using static state variable within the filter :<
			red::ScopedLock< red::Mutex > lock( st_convertFormatMutex );

			Image::SizeInfo size = source->GetSize();			
			auto target = Image::CreateEmpty( destFormat, size, source->GetGammaExp() );
			
			// create access points to image
			const ImageAccess srcAccess( *source );
			ImageAccess targetAccess( *target );

			ImageIterator it = source->GetIterator();
			FormatConvert::Setup( destFormat );
			utils::FilterImage< FormatConvert >( srcAccess, targetAccess, it );

			return target;
		}

		REDIMAGE_API float GetChannelCoverage(const ImagePtr& source, const Uint8 channel, const Float threshold)
		{
			if (!source)
				return 0.0f;

			Uint32 imgSize = source->GetSize().m_width * source->GetSize().m_height;
			if (imgSize == 0)
				return 0.0f;

			const Uint8 sourceImgChannels = source->GetSize().m_channels;
			if (channel >= sourceImgChannels)
			{
				RED_HALT("The source image has only %d channels and we requested GetChannelCoverage for channel %d", sourceImgChannels, channel);
				return 0.0f;
			}

			ImageAccess src(*source);

			ImageIterator it = source->GetIterator();

			//alpha sum, div all - this strategy seems best
			Float coverage = 0;
			auto func = [&coverage, channel, threshold](ImageIterator& it, ImagePixel& pixel)->Bool
			{
				if (pixel[channel] > threshold)
				{
					coverage += pixel[channel];
				}
				return true;
			};
			img::utils::ProcessImageInplace(src, it, func);
			return static_cast<Float>(coverage) / imgSize;
		}

		REDIMAGE_API float GetChannelMinValue( const ImagePtr& source, const Uint8 channel )
		{
			if ( !source )
				return 0.0f;

			Uint32 imgSize = source->GetSize().m_width * source->GetSize().m_height;
			if ( imgSize == 0 )
				return 0.0f;

			const Uint8 sourceImgChannels = source->GetSize().m_channels;
			if ( channel >= sourceImgChannels )
			{
				RED_HALT( "The source image has only %d channels and we requested GetChannelMinValue for channel %d", sourceImgChannels, channel );
				return 0.0f;
			}

			ImageAccess src( *source );

			ImageIterator it = source->GetIterator();

			Float minVal = 1.0f;
			auto func = [&minVal, channel]( ImageIterator& it, ImagePixel& pixel )->Bool
			{
				minVal = Min( minVal, pixel[channel] );
				return true;
			};
			img::utils::ProcessImageInplace( src, it, func );
			return minVal;
		}

		REDIMAGE_API void MultiplyChannelValue(const ImagePtr& source, const Uint8 channel, const Float factor)
		{
			if (!source || (factor >= 1.0f - 1e-5f && factor <= 1.0f + 1e-5f))
				return;

			const Uint8 sourceImgChannels = source->GetSize().m_channels;
			if (channel >= sourceImgChannels)
			{
				RED_HALT("The source image has only %d channels and we requested MultiplyChannelValue for channel %d", sourceImgChannels, channel);
				return;
			}

			ImageAccess src(*source);

			ImageIterator it = source->GetIterator();
			auto func = [channel, factor](ImageIterator& it, ImagePixel& pixel)->Bool
			{
				pixel[channel] = Min(pixel[channel] * factor, 1.0f );
				return true;
			};
			img::utils::ProcessImageInplace(src, it, func);
		}
	} // utils

} // img