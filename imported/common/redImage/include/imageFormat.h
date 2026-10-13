/**
* Copyright (c) 2016 CDProjekt Red, Inc. All Rights Reserved.
*/

/// Original code (C) RenderLab 2014 by Tomasz "RexDex" Jonarski
/// Copied with permission from author by author :)

#pragma once

namespace img
{
	/// Image formats (global)
	/// CAUTION: The name here is not entirely correct. PixelFormat is in fact a ChannelFormat. #erykdwornicki
	enum class PixelFormat : Uint8
	{
		Format_Uint8,
		Format_Uint16,
		Format_Float16,
		Format_Uint32,
		Format_Float32,
	};

	inline Uint32 GetBitsPerChannel( const PixelFormat format )
	{
		switch( format )
		{
		case img::PixelFormat::Format_Uint8: return 8;
		case img::PixelFormat::Format_Uint16:
		case img::PixelFormat::Format_Float16: return 16;
		case img::PixelFormat::Format_Uint32:
		case img::PixelFormat::Format_Float32: return 32;
		}
		return 1;
	}

	/// Unpacked pixel
	/// Always 4x float, order is not important in here
	class ImagePixel
	{
	public:
		typedef Float TPixelValue;

		RED_FORCE_INLINE ImagePixel(); // not initialized
		RED_FORCE_INLINE ImagePixel( int ); // initialized to zeros
		RED_FORCE_INLINE ImagePixel( const TPixelValue v ); // initialized to given value
		RED_FORCE_INLINE ImagePixel( const TPixelValue* v ); // copied from pointer
		RED_FORCE_INLINE ImagePixel( const ImagePixel& other ); // copied

		// get component
		RED_FORCE_INLINE const TPixelValue& operator[]( const Uint32 index ) const;

		// get component
		RED_FORCE_INLINE TPixelValue& operator[]( const Uint32 index );

		// clear value to zeros
		RED_FORCE_INLINE ImagePixel& Zero();

		// replicate single value
		RED_FORCE_INLINE ImagePixel& Set1( const TPixelValue v );

		// copy values from memory (always as floats)
		RED_FORCE_INLINE ImagePixel& Load( const Float* ptr );

		// store values in memory (always as floats)
		RED_FORCE_INLINE ImagePixel& Store( Float* ptr );

		// copy values
		RED_FORCE_INLINE ImagePixel& Set( const ImagePixel& other );

		// interpolate pixel values, returns new value
		RED_FORCE_INLINE ImagePixel Lerp( const ImagePixel& a, const ImagePixel& b, const Float frac ) const;

		// accumulate pixel values (with weight)
		RED_FORCE_INLINE ImagePixel& AccumulateInplace( const ImagePixel& value, const Float weight = 1.0f );

		// multiply by scalar, returns multiplied value
		RED_FORCE_INLINE ImagePixel Mul( const Float scalar ) const;

		// multiply by scalar in place
		RED_FORCE_INLINE ImagePixel& MulInplace( const Float scalar );

		// set to a value multiplied by scalar
		RED_FORCE_INLINE ImagePixel& SetMul( const ImagePixel& other, const Float scalar );

		// modulate with other color, returns modulated value
		RED_FORCE_INLINE ImagePixel Modulate( const ImagePixel& other ) const;

		// modulate with other pixel in place
		RED_FORCE_INLINE ImagePixel& ModulateInplace( const ImagePixel& other );

		// set to a result of modulation of two pixels
		RED_FORCE_INLINE ImagePixel& SetModulate( const ImagePixel& a, const ImagePixel& b );

		// convert value from gamma space to linear space
		RED_FORCE_INLINE ImagePixel& GammaToLinearInplace( Float gammaExp );

		// convert value from linear space to gamma space
		RED_FORCE_INLINE ImagePixel& LinearToGammaInplace( Float invGammaExp );

		// transform value to power inplace
		RED_FORCE_INLINE ImagePixel& PowInplace( Float exp );

	private:
		static const Uint32 MAX_CHANNELS = 4;

		Float	m_values[ MAX_CHANNELS ];
	};

	///--------------------------------------------------------------------------

	/// Byte format wrapper for generalized pixel access
	template< Uint32 NUM_CHANNELS >
	class ImageFormatUint8_UNORM
	{
	public:
		/// type name
		static const PixelFormat FORMAT = PixelFormat::Format_Uint8;
		static const Uint32 CHANNELS = NUM_CHANNELS;

		/// pack/unpack values
		RED_FORCE_INLINE static void Unpack( const void* mem, ImagePixel& outPixel );
		RED_FORCE_INLINE static void Pack( void* mem, const ImagePixel& inPixel );
	};

	///--------------------------------------------------------------------------

	/// Word format wrapper for generalized pixel access
	template< Uint32 NUM_CHANNELS >
	class ImageFormatUint16_UNORM
	{
	public:
		/// type name
		static const PixelFormat FORMAT = PixelFormat::Format_Uint16;
		static const Uint32 CHANNELS = NUM_CHANNELS;

		/// pack/unpack values
		RED_FORCE_INLINE static void Unpack( const void* mem, ImagePixel& outPixel );
		RED_FORCE_INLINE static void Pack( void* mem, const ImagePixel& inPixel );
	};

	///--------------------------------------------------------------------------

	/// Word format wrapper for generalized pixel access
	template< Uint32 NUM_CHANNELS >
	class ImageFormatFloat16
	{
	public:
		/// type name
		static const PixelFormat FORMAT = PixelFormat::Format_Float16;
		static const Uint32 CHANNELS = NUM_CHANNELS;

		/// pack/unpack values
		RED_FORCE_INLINE static void Unpack( const void* mem, ImagePixel& outPixel );
		RED_FORCE_INLINE static void Pack( void* mem, const ImagePixel& inPixel );
	};

	///--------------------------------------------------------------------------

	/// 32-bit format wrapper for generalized pixel access
	/// NOTE: this format does no conversion - just reinterprets the data
	template< Uint32 NUM_CHANNELS >
	class ImageFormat32_DIRECT
	{
	public:
		/// type name
		static const PixelFormat FORMAT = PixelFormat::Format_Uint32;
		static const Uint32 CHANNELS = NUM_CHANNELS;

		/// pack/unpack values
		RED_FORCE_INLINE static void Unpack( const void* mem, ImagePixel& outPixel );
		RED_FORCE_INLINE static void Pack( void* mem, const ImagePixel& inPixel );
	};

} // img

///--------------------------------------------------------------------------

#include "imageFormat.inl"

///--------------------------------------------------------------------------
