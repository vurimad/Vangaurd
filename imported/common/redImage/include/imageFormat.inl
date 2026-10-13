/**
* Copyright (c) 2016 CDProjekt Red, Inc. All Rights Reserved.
*/

/// Original code (C) RenderLab 2014 by Tomasz "RexDex" Jonarski
/// Copied with permission from author by author :)

#pragma once

#include "../../../common/redMath/include/float16compressor.h"

namespace img
{
	///--------------------------------------------------------------------------

	RED_FORCE_INLINE ImagePixel::ImagePixel()
	{
		// not initialized to save cycles
	}

	RED_FORCE_INLINE ImagePixel::ImagePixel( int )
	{
		for ( Uint32 i=0; i<MAX_CHANNELS; ++i )
			m_values[i] = (TPixelValue) 0;
	}

	RED_FORCE_INLINE ImagePixel::ImagePixel( const TPixelValue v )
	{
		for ( Uint32 i=0; i<MAX_CHANNELS; ++i )
			m_values[i] = v;
	}

	RED_FORCE_INLINE ImagePixel::ImagePixel( const TPixelValue* v )
	{
		for ( Uint32 i=0; i<MAX_CHANNELS; ++i )
			m_values[i] = v[i];
	}

	RED_FORCE_INLINE ImagePixel::ImagePixel( const ImagePixel& other )
	{
		for ( Uint32 i=0; i<MAX_CHANNELS; ++i )
			m_values[i] = other.m_values[i];
	}

	RED_FORCE_INLINE const ImagePixel::TPixelValue& ImagePixel::operator[]( const Uint32 index ) const
	{
		//RED_PARANOID( index < MAX_CHANNELS, TXT("Invalid component index") );
		return m_values[ index ];
	}

	RED_FORCE_INLINE ImagePixel::TPixelValue& ImagePixel::operator[]( const Uint32 index )
	{
		//RED_PARANOID( index < MAX_CHANNELS, TXT("Invalid component index") );
		return m_values[ index ];
	}

	RED_FORCE_INLINE ImagePixel& ImagePixel::Zero()
	{
		for ( Uint32 i=0; i<MAX_CHANNELS; ++i )
			m_values[i] = (TPixelValue) 0;
		return *this;
	}

	RED_FORCE_INLINE ImagePixel& ImagePixel::Set1( const TPixelValue v )
	{
		for ( Uint32 i=0; i<MAX_CHANNELS; ++i )
			m_values[i] = v;
		return *this;
	}

	RED_FORCE_INLINE ImagePixel& ImagePixel::Load( const TPixelValue* ptr )
	{
		for ( Uint32 i=0; i<MAX_CHANNELS; ++i )
			m_values[i] = (TPixelValue) ptr[i];
		return *this;
	}

	RED_FORCE_INLINE ImagePixel& ImagePixel::Store( TPixelValue* ptr )
	{
		for ( Uint32 i=0; i<MAX_CHANNELS; ++i )
			ptr[i] = (Float) m_values[i];
		return *this;
	}

	RED_FORCE_INLINE ImagePixel& ImagePixel::Set( const ImagePixel& other )
	{
		for ( Uint32 i=0; i<MAX_CHANNELS; ++i )
			m_values[i] = other.m_values[i];
		return *this;
	}

	RED_FORCE_INLINE ImagePixel ImagePixel::Lerp( const ImagePixel& a, const ImagePixel& b, const Float frac ) const
	{
		ImagePixel ret;
		for ( Uint32 i=0; i<MAX_CHANNELS; ++i )
			ret.m_values[i] = math::Lerp< TPixelValue >( frac, a.m_values[i], b.m_values[i] );
		return ret;
	}

	RED_FORCE_INLINE ImagePixel& ImagePixel::AccumulateInplace( const ImagePixel& value, const Float weight /*= 1.0f*/ )
	{
		for ( Uint32 i=0; i<MAX_CHANNELS; ++i )
			m_values[i] += value.m_values[i] * weight;
		return *this;
	}

	RED_FORCE_INLINE ImagePixel ImagePixel::Mul( const Float scalar ) const
	{
		ImagePixel ret;
		for ( Uint32 i=0; i<MAX_CHANNELS; ++i )
			ret.m_values[i] += m_values[i] * scalar;
		return ret;
	}

	RED_FORCE_INLINE ImagePixel& ImagePixel::MulInplace( const Float scalar )
	{
		for ( Uint32 i=0; i<MAX_CHANNELS; ++i )
			m_values[i] *= scalar;
		return *this;
	}

	RED_FORCE_INLINE ImagePixel& ImagePixel::SetMul( const ImagePixel& other, const Float scalar )
	{
		for ( Uint32 i=0; i<MAX_CHANNELS; ++i )
			m_values[i] = other.m_values[i] * scalar;
		return *this;
	}

	RED_FORCE_INLINE ImagePixel ImagePixel::Modulate( const ImagePixel& other ) const
	{
		ImagePixel ret;
		for ( Uint32 i=0; i<MAX_CHANNELS; ++i )
			ret.m_values[i] += m_values[i] * other.m_values[i];
		return ret;
	}

	RED_FORCE_INLINE ImagePixel& ImagePixel::ModulateInplace( const ImagePixel& other )
	{
		for ( Uint32 i=0; i<MAX_CHANNELS; ++i )
			m_values[i] *= other.m_values[i];
		return *this;
	}

	RED_FORCE_INLINE ImagePixel& ImagePixel::SetModulate( const ImagePixel& a, const ImagePixel& b )
	{
		for ( Uint32 i=0; i<MAX_CHANNELS; ++i )
			m_values[i] = a.m_values[i] * b.m_values[i];
		return *this;
	}

	RED_FORCE_INLINE ImagePixel& ImagePixel::GammaToLinearInplace( Float gammaExp ) 
	{
		// we're not touching alpha channel
		for ( Uint32 i=0; i<MAX_CHANNELS-1; ++i )
			m_values[i] = MPow(m_values[i], gammaExp );
		return *this;
	}

	RED_FORCE_INLINE ImagePixel& ImagePixel::LinearToGammaInplace( Float invGammaExp )
	{
		// we're not touching alpha channel
		for ( Uint32 i=0; i<MAX_CHANNELS-1; ++i )
			m_values[i] = MPow(m_values[i], invGammaExp);
		return *this;
	}


	RED_FORCE_INLINE ImagePixel& ImagePixel::PowInplace(Float exp)
	{
		// we're not touching alpha channel
		for ( Uint32 i=0; i<MAX_CHANNELS-1; ++i )
			m_values[i] = MPow(m_values[i], exp);
		return *this;
	}

	///--------------------------------------------------------------------------

	template< Uint32 NUM_CHANNELS >
	RED_FORCE_INLINE void ImageFormatUint8_UNORM<NUM_CHANNELS>::Unpack( const void* mem, ImagePixel& outPixel )
	{
		const Uint8* memB = (const Uint8*) mem;

		if ( NUM_CHANNELS >= 1 ) outPixel[0] = memB[0] / 255.0f;
		if ( NUM_CHANNELS >= 2 ) outPixel[1] = memB[1] / 255.0f;
		if ( NUM_CHANNELS >= 3 ) outPixel[2] = memB[2] / 255.0f;
		if ( NUM_CHANNELS >= 4 ) outPixel[3] = memB[3] / 255.0f;
	}

	template< Uint32 NUM_CHANNELS >
	RED_FORCE_INLINE void ImageFormatUint8_UNORM<NUM_CHANNELS>::Pack( void* mem, const ImagePixel& inPixel )
	{
		Uint8* memB = (Uint8*) mem;

		if ( NUM_CHANNELS >= 1 ) memB[0] = (Uint8) math::Clamp<Int32>( (Int32)(inPixel[0] * 255.0f), 0, 255 );
		if ( NUM_CHANNELS >= 2 ) memB[1] = (Uint8) math::Clamp<Int32>( (Int32)(inPixel[1] * 255.0f), 0, 255 );
		if ( NUM_CHANNELS >= 3 ) memB[2] = (Uint8) math::Clamp<Int32>( (Int32)(inPixel[2] * 255.0f), 0, 255 );
		if ( NUM_CHANNELS >= 4 ) memB[3] = (Uint8) math::Clamp<Int32>( (Int32)(inPixel[3] * 255.0f), 0, 255 );
	}

	///--------------------------------------------------------------------------

	template< Uint32 NUM_CHANNELS >
	RED_FORCE_INLINE void ImageFormatUint16_UNORM<NUM_CHANNELS>::Unpack( const void* mem, ImagePixel& outPixel )
	{
		const Uint16* memB = (const Uint16*) mem;

		if ( NUM_CHANNELS >= 1 ) outPixel[0] = memB[0] / 65535.0f;
		if ( NUM_CHANNELS >= 2 ) outPixel[1] = memB[1] / 65535.0f;
		if ( NUM_CHANNELS >= 3 ) outPixel[2] = memB[2] / 65535.0f;
		if ( NUM_CHANNELS >= 4 ) outPixel[3] = memB[3] / 65535.0f;
	}

	template< Uint32 NUM_CHANNELS >
	RED_FORCE_INLINE void ImageFormatUint16_UNORM<NUM_CHANNELS>::Pack( void* mem, const ImagePixel& inPixel )
	{
		Uint16* memB = (Uint16*) mem;

		if ( NUM_CHANNELS >= 1 ) memB[0] = (Uint16) math::Clamp<Uint32>( (Uint32)(inPixel[0] * 65535.0f), 0, 65535 );
		if ( NUM_CHANNELS >= 2 ) memB[1] = (Uint16) math::Clamp<Uint32>( (Uint32)(inPixel[1] * 65535.0f), 0, 65535 );
		if ( NUM_CHANNELS >= 3 ) memB[2] = (Uint16) math::Clamp<Uint32>( (Uint32)(inPixel[2] * 65535.0f), 0, 65535 );
		if ( NUM_CHANNELS >= 4 ) memB[3] = (Uint16) math::Clamp<Uint32>( (Uint32)(inPixel[3] * 65535.0f), 0, 65535 );
	}

	///--------------------------------------------------------------------------

	template< Uint32 NUM_CHANNELS >
	RED_FORCE_INLINE void ImageFormatFloat16<NUM_CHANNELS>::Unpack( const void* mem, ImagePixel& outPixel )
	{
		const Uint16* memB = (const Uint16*) mem;

		if ( NUM_CHANNELS >= 1 ) outPixel[0] = Float16Compressor::Decompress(memB[0]);
		if ( NUM_CHANNELS >= 2 ) outPixel[1] = Float16Compressor::Decompress(memB[1]);
		if ( NUM_CHANNELS >= 3 ) outPixel[2] = Float16Compressor::Decompress(memB[2]);
		if ( NUM_CHANNELS >= 4 ) outPixel[3] = Float16Compressor::Decompress(memB[3]);
	}

	template< Uint32 NUM_CHANNELS >
	RED_FORCE_INLINE void ImageFormatFloat16<NUM_CHANNELS>::Pack( void* mem, const ImagePixel& inPixel )
	{
		Uint16* memB = (Uint16*) mem;

		if ( NUM_CHANNELS >= 1 ) memB[0] = Float16Compressor::Compress( inPixel[0] );
		if ( NUM_CHANNELS >= 2 ) memB[1] = Float16Compressor::Compress( inPixel[1] );
		if ( NUM_CHANNELS >= 3 ) memB[2] = Float16Compressor::Compress( inPixel[2] );
		if ( NUM_CHANNELS >= 4 ) memB[3] = Float16Compressor::Compress( inPixel[3] );
	}

	///--------------------------------------------------------------------------

	template< Uint32 NUM_CHANNELS >
	RED_FORCE_INLINE void ImageFormat32_DIRECT<NUM_CHANNELS>::Unpack( const void* mem, ImagePixel& outPixel )
	{
		const Float* memB = (const Float*) mem;

		if ( NUM_CHANNELS >= 1 ) outPixel[0] = memB[0];
		if ( NUM_CHANNELS >= 2 ) outPixel[1] = memB[1];
		if ( NUM_CHANNELS >= 3 ) outPixel[2] = memB[2];
		if ( NUM_CHANNELS >= 4 ) outPixel[3] = memB[3];
	}

	template< Uint32 NUM_CHANNELS >
	RED_FORCE_INLINE void ImageFormat32_DIRECT<NUM_CHANNELS>::Pack( void* mem, const ImagePixel& inPixel )
	{
		Float* memB = (Float*) mem;

		if ( NUM_CHANNELS >= 1 ) memB[0] = inPixel[0];
		if ( NUM_CHANNELS >= 2 ) memB[1] = inPixel[1];
		if ( NUM_CHANNELS >= 3 ) memB[2] = inPixel[2];
		if ( NUM_CHANNELS >= 4 ) memB[3] = inPixel[3];
	}

	///--------------------------------------------------------------------------

} // img