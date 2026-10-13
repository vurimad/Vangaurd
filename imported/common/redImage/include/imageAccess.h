/**
* Copyright (c) 2016 CDProjekt Red, Inc. All Rights Reserved.
*/

/// Original code (C) RenderLab 2014 by Tomasz "RexDex" Jonarski
/// Copied with permission

#pragma once

#include "image.h"
#include "imageIterator.h"
#include "imageFormat.h"

namespace img
{

	/// Intelligent access to image, allows to iterate through image
	class REDIMAGE_API ImageAccess
	{
	public:
		ImageAccess();
		ImageAccess( const ImageAccess& other );
		ImageAccess( Image& image );
		ImageAccess( const Image& image );

		/// Get pixel format
		RED_FORCE_INLINE const PixelFormat GetPixelFormat() const { return m_format; }

		/// Get image size
		RED_FORCE_INLINE const Image::SizeInfo& GetSize() const { return m_size; }

		/// Get gamma epxonents
		RED_FORCE_INLINE Float GetGammaExp() const { return m_gammaExp; }
		RED_FORCE_INLINE Float GetInvGammaExp() const { return m_invGammaExp; }

		/// Get raw pixel memory
		RED_FORCE_INLINE void* GetPixelData( const ImageIterator& pos );

		/// Get raw pixel memory (read only)
		RED_FORCE_INLINE const void* GetPixelData( const ImageIterator& pos ) const;

		/// Get raw pixel memory
		RED_FORCE_INLINE void* GetPixelData( const Uint32 x, const Uint32 y, const Uint32 z );

		/// Get raw pixel memory (read only)
		RED_FORCE_INLINE const void* GetPixelData( const Uint32 x, const Uint32 y, const Uint32 z ) const;

		/// Is in gamma space
		RED_FORCE_INLINE const Bool IsInGammaSpace() const { return m_gammaExp != 1.0f; }

		/// Unpack pixel value at given position
		template< typename Format >
		RED_FORCE_INLINE void Unpack( const ImageIterator& pos, ImagePixel& outPixel ) const;

		/// Unpack pixel value at given position
		template< typename Format >
		RED_FORCE_INLINE void Unpack( const Uint32 x, const Uint32 y, const Uint32 z, ImagePixel& outPixel ) const;

		/// Pack pixel value at given position
		template< typename Format >
		RED_FORCE_INLINE void Pack( const ImageIterator& pos, const ImagePixel& inPixel ); 

		/// Run symmetric kernel of given size [KX,KY,KZ] at current pixel, returns computed value in the outPixel
		/// NOTE: optional weight calculation function can be provided
		template< Int32 KX, Int32 KY, Int32 KZ, typename Format, typename Kernel >
		RED_FORCE_INLINE void RunKernel( const ImageIterator& pos, ImagePixel& outPixel ) const;

	private:
		PixelFormat					m_format;
		Image::SizeInfo				m_size;
		Image::MemoryLayout			m_layout;
		Float						m_gammaExp;
		Float						m_invGammaExp;
		void*						m_pixels; // all pixels
	};

} // red

#include "imageAccess.inl"
