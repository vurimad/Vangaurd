/**
* Copyright (c) 2016 CDProjekt Red, Inc. All Rights Reserved.
*/

/// Original code (C) RenderLab 2014 by Tomasz "RexDex" Jonarski
/// Copied with permission from author by author :)

#pragma once

#include "imageAccess.h"

namespace img
{

	namespace utils
	{

		namespace prv
		{
			template< typename Func, typename Format >
			void ProcessImage_Internal2_Inplace( ImageAccess& image, ImageIterator& it, Func& func )
			{
				do 
				{			
					ImagePixel pixel;
					image.Unpack<Format>( it, pixel );
					if ( func( it, pixel ) )
						image.Pack<Format>( it, pixel );
				} while ( it.Next() );
			}

			template< typename Func, Uint32 NumChannels >
			void ProcessImage_Internal_Inplace( ImageAccess& image, ImageIterator& it, Func& func )
			{
				switch ( image.GetPixelFormat() )
				{
					case PixelFormat::Format_Uint8: ProcessImage_Internal2_Inplace< Func, ImageFormatUint8_UNORM<NumChannels> >( image, it, func ); break;
					case PixelFormat::Format_Uint16: ProcessImage_Internal2_Inplace< Func, ImageFormatUint16_UNORM<NumChannels> >( image, it, func ); break;
					case PixelFormat::Format_Float16: ProcessImage_Internal2_Inplace< Func, ImageFormatFloat16<NumChannels> >( image, it, func ); break;
					case PixelFormat::Format_Uint32: ProcessImage_Internal2_Inplace< Func, ImageFormat32_DIRECT<NumChannels> >( image, it, func ); break;
					case PixelFormat::Format_Float32: ProcessImage_Internal2_Inplace< Func, ImageFormat32_DIRECT<NumChannels> >( image, it, func ); break;
				}
			}

			template< typename Func, typename Format >
			void ProcessImage_Internal2_RO( const ImageAccess& image, ImageIterator& it, Func& func )
			{
				do 
				{			
					ImagePixel pixel;
					image.Unpack<Format>( it, pixel );
					func( it, pixel );
				} while ( it.Next() );
			}

			template< typename Func, Uint32 NumChannels >
			void ProcessImage_Internal_RO( const ImageAccess& image, ImageIterator& it, Func& func )
			{
				switch ( image.GetPixelFormat() )
				{
					case PixelFormat::Format_Uint8: ProcessImage_Internal2_RO< Func, ImageFormatUint8_UNORM<NumChannels> >( image, it, func ); break;
					case PixelFormat::Format_Uint16: ProcessImage_Internal2_RO< Func, ImageFormatUint16_UNORM<NumChannels> >( image, it, func ); break;
					case PixelFormat::Format_Float16: ProcessImage_Internal2_RO< Func, ImageFormatFloat16<NumChannels> >( image, it, func ); break;
					case PixelFormat::Format_Uint32: ProcessImage_Internal2_RO< Func, ImageFormat32_DIRECT<NumChannels> >( image, it, func ); break;
					case PixelFormat::Format_Float32: ProcessImage_Internal2_RO< Func, ImageFormat32_DIRECT<NumChannels> >( image, it, func ); break;
				}
			}

			template< typename Kernel, typename Format >
			void FilterImage_Internal2( const ImageAccess& image, ImageIterator& it )
			{
				do 
				{			
					Kernel::template Process<Format>(image, it);
				} while ( it.Next() );
			}

			template< typename Kernel, Uint32 NumChannels >
			void FilterImage_Internal( const ImageAccess& image, ImageIterator& it )
			{
				switch ( image.GetPixelFormat() )
				{
					case PixelFormat::Format_Uint8: FilterImage_Internal2< Kernel, ImageFormatUint8_UNORM<NumChannels> >( image, it ); break;
					case PixelFormat::Format_Uint16: FilterImage_Internal2< Kernel, ImageFormatUint16_UNORM<NumChannels> >( image, it  ); break;
					case PixelFormat::Format_Float16: FilterImage_Internal2< Kernel, ImageFormatFloat16<NumChannels> >( image, it  ); break;
					case PixelFormat::Format_Uint32: FilterImage_Internal2< Kernel, ImageFormat32_DIRECT<NumChannels> >( image, it ); break;
					case PixelFormat::Format_Float32: FilterImage_Internal2< Kernel, ImageFormat32_DIRECT<NumChannels> >( image, it ); break;
				}
			}

			template< typename Kernel, typename Format >
			void FilterImage_Internal2( const ImageAccess& src, ImageAccess& dest, ImageIterator& it )
			{
				do 
				{			
					Kernel::template Process<Format>(src, dest, it);
				} while ( it.Next() );
			}

			template< typename Kernel, Uint32 NumChannels >
			void FilterImage_Internal( const ImageAccess& src, ImageAccess& dest, ImageIterator& it )
			{
				switch ( src.GetPixelFormat() )
				{
					case PixelFormat::Format_Uint8: FilterImage_Internal2< Kernel, ImageFormatUint8_UNORM<NumChannels> >( src, dest, it ); break;
					case PixelFormat::Format_Uint16: FilterImage_Internal2< Kernel, ImageFormatUint16_UNORM<NumChannels> >( src, dest, it  ); break;
					case PixelFormat::Format_Float16: FilterImage_Internal2< Kernel, ImageFormatFloat16<NumChannels> >( src, dest, it  ); break;
					case PixelFormat::Format_Uint32: FilterImage_Internal2< Kernel, ImageFormat32_DIRECT<NumChannels> >( src, dest, it ); break;
					case PixelFormat::Format_Float32: FilterImage_Internal2< Kernel, ImageFormat32_DIRECT<NumChannels> >( src, dest, it ); break;
				}
			}

			template< typename Kernel, typename Format >
			void FilterImage_Internal2( const ImageAccess& src, ImageAccess& dest )
			{	
				Kernel::template Process<Format>(src, dest);
			}

			template< typename Kernel, Uint32 NumChannels >
			void FilterImage_Internal( const ImageAccess& src, ImageAccess& dest )
			{
				switch ( src.GetPixelFormat() )
				{
				case PixelFormat::Format_Uint8: FilterImage_Internal2< Kernel, ImageFormatUint8_UNORM<NumChannels> >( src, dest ); break;
				case PixelFormat::Format_Uint16: FilterImage_Internal2< Kernel, ImageFormatUint16_UNORM<NumChannels> >( src, dest ); break;
				case PixelFormat::Format_Float16: FilterImage_Internal2< Kernel, ImageFormatFloat16<NumChannels> >( src, dest ); break;
				case PixelFormat::Format_Uint32: FilterImage_Internal2< Kernel, ImageFormat32_DIRECT<NumChannels> >( src, dest ); break;
				case PixelFormat::Format_Float32: FilterImage_Internal2< Kernel, ImageFormat32_DIRECT<NumChannels> >( src, dest ); break;
				}
			}

		} // prv

		/// Run function on the whole read only image using given iterator
		template< typename Func >
		RED_FORCE_INLINE void ProcessImage( const ImageAccess& image, ImageIterator& it, Func& func )
		{
			switch ( image.GetSize().m_channels )
			{
				case 1: prv::ProcessImage_Internal_RO<Func,1>( image, it, func ); break;
				case 2: prv::ProcessImage_Internal_RO<Func,2>( image, it, func ); break;
				case 3: prv::ProcessImage_Internal_RO<Func,3>( image, it, func ); break;
				case 4: prv::ProcessImage_Internal_RO<Func,4>( image, it, func ); break;
			}
		}

		/// Run function on the whole image using given iterator
		template< typename Func >
		RED_FORCE_INLINE void ProcessImageInplace( ImageAccess& image, ImageIterator& it, Func& func )
		{
			switch ( image.GetSize().m_channels )
			{
				case 1: prv::ProcessImage_Internal_Inplace<Func,1>( image, it, func ); break;
				case 2: prv::ProcessImage_Internal_Inplace<Func,2>( image, it, func ); break;
				case 3: prv::ProcessImage_Internal_Inplace<Func,3>( image, it, func ); break;
				case 4: prv::ProcessImage_Internal_Inplace<Func,4>( image, it, func ); break;
			}
		}

		/// Run function on the whole image using given iterator
		template< typename Kernel >
		RED_FORCE_INLINE void FilterImageInplace( ImageAccess& image, ImageIterator& it )
		{
			switch ( image.GetSize().m_channels )
			{
				case 1: prv::FilterImage_Internal<Kernel,1>( image, it ); break;
				case 2: prv::FilterImage_Internal<Kernel,2>( image, it ); break;
				case 3: prv::FilterImage_Internal<Kernel,3>( image, it ); break;
				case 4: prv::FilterImage_Internal<Kernel,4>( image, it ); break;
			}
		}

		/// Run function on the whole image using given iterator
		template< typename Kernel >
		RED_FORCE_INLINE void FilterImage( const ImageAccess& src, ImageAccess& dest, ImageIterator& it )
		{
			switch ( src.GetSize().m_channels )
			{
				case 1: prv::FilterImage_Internal<Kernel,1>( src, dest, it ); break;
				case 2: prv::FilterImage_Internal<Kernel,2>( src, dest, it ); break;
				case 3: prv::FilterImage_Internal<Kernel,3>( src, dest, it ); break;
				case 4: prv::FilterImage_Internal<Kernel,4>( src, dest, it ); break;
			}
		}

		/// Run function on the whole image using given iterator
		template< typename Kernel >
		RED_FORCE_INLINE void FilterImage( const ImageAccess& src, ImageAccess& dest )
		{
			switch ( src.GetSize().m_channels )
			{
			case 1: prv::FilterImage_Internal<Kernel,1>( src, dest ); break;
			case 2: prv::FilterImage_Internal<Kernel,2>( src, dest ); break;
			case 3: prv::FilterImage_Internal<Kernel,3>( src, dest ); break;
			case 4: prv::FilterImage_Internal<Kernel,4>( src, dest ); break;
			}
		}

	} // utils

} // img