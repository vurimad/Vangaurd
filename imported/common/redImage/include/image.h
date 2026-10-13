/**
* Copyright (c) 2016 CDProjekt Red, Inc. All Rights Reserved.
*/

/// Original code (C) RenderLab 2014 by Tomasz "RexDex" Jonarski
/// Copied with permission from author by author :)

#pragma once

#include "imageFormat.h"
#include "imageIterator.h"

namespace img
{
	class Image;
	typedef red::SharedPtr<Image> ImagePtr;

	/// General image wrapper
	class REDIMAGE_API Image
	{
		RED_USE_MEMORY_POOL( red::PoolEngine );

	public:
		~Image();

		// general size information
		struct REDIMAGE_API SizeInfo
		{
			Uint8		m_channels;

			Uint16		m_width;
			Uint16		m_height;
			Uint16		m_depth;

			SizeInfo( const Uint8 channels=1, const Uint16 width=1, const Uint16 height=1, const Uint16 depth=1 );

			bool operator==( const SizeInfo & other );

			const Bool IsValid() const;
		};

		// image navigation
		struct MemoryLayout
		{
			Uint32		m_pixelPitch;	// in bytes
			Uint32		m_rowPitch;		// in bytes
			Uint32		m_layerPitch;	// in bytes

			Uint32		m_rowAlignment; // required basic row alignment

			// calculate offset to given pixel, this does not depend on the format
			RED_FORCE_INLINE const Uint32 CalcPixelOffset( const Uint32 x, const Uint32 y, const Uint32 z ) const
			{
				Uint32 ret = z * m_layerPitch;
				ret += y * m_rowPitch;
				ret += x * m_pixelPitch;
				return ret;
			}
		};

		// Get image size information
		RED_FORCE_INLINE const SizeInfo& GetSize() const { return m_size; }

		// Get image memory layout
		RED_FORCE_INLINE const MemoryLayout& GetLayout() const { return m_layout; }

		// Get image format
		RED_FORCE_INLINE const PixelFormat GetPixelFormat() const { return m_format; }

		// Get & Set gamma exp
		RED_FORCE_INLINE const void SetGammaExp( Float gammaExp ) { m_gammaExp = gammaExp; }
		RED_FORCE_INLINE const Float GetGammaExp() const { return m_gammaExp; }

		// Is in gamma space
		RED_FORCE_INLINE const Bool IsInGammaSpace() const { return m_gammaExp != 1.0f; }

		// Get image data buffer
		RED_FORCE_INLINE const void* GetPixelBuffer() const { return m_pixels; }

		// Get image data buffer
		RED_FORCE_INLINE void* GetPixelBuffer() { return m_pixels; }

		// Get image data buffer size
		RED_FORCE_INLINE Uint32 GetPixelBufferSize() const { return CalcImageSize(m_size, m_layout); }

		// Get image iterator for the whole image data
		ImageIterator GetIterator() const;

		RED_FORCE_INLINE Int64 GetUserData() const { return m_userData; }

		RED_FORCE_INLINE void SetUserData( Int64 userData ) { m_userData = userData; }

	public:
		// Create image with empty buffer
		// NOTE: not fast enough to be called at runtime
		static ImagePtr CreateEmpty( PixelFormat format, const SizeInfo& size, Float gammaExp = 1.0f );

	private:
		Image();

		void*			m_pixels;
		MemoryLayout	m_layout;

		PixelFormat		m_format;
		SizeInfo		m_size;
		Float			m_gammaExp;
		Int64			m_userData;

		static const Bool CalcFormatData( const PixelFormat format, Uint32& outPixelSize, Uint32& outFormatAlignment );
		static const Bool CalcMemoryLayout( const PixelFormat format, const SizeInfo& size, MemoryLayout& outLayout );
		static const Uint32 CalcImageSize( const SizeInfo& size, const MemoryLayout& memoryLayout );
	};

	/// register load/save file formats
	extern REDIMAGE_API void RegisterFormats();

} // red