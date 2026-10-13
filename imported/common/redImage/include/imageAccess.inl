/**
* Copyright (c) 2016 CDProjekt Red, Inc. All Rights Reserved.
*/

/// Original code (C) RenderLab 2014 by Tomasz "RexDex" Jonarski
/// Copied with permission from author by author :)

#pragma once

namespace img
{

	template< typename T >
	RED_FORCE_INLINE T* OffsetPtr( T* ptr, const Int64 offset )
	{
		return (T*)( (Uint64)ptr + offset );
	}

	RED_FORCE_INLINE void* ImageAccess::GetPixelData( const ImageIterator& pos )
	{
		const Uint32 offset = m_layout.CalcPixelOffset( pos.GetX(), pos.GetY(), pos.GetZ() );
		return OffsetPtr( m_pixels, offset );
	}

	RED_FORCE_INLINE const void* ImageAccess::GetPixelData( const ImageIterator& pos ) const
	{
		const Uint32 offset = m_layout.CalcPixelOffset( pos.GetX(), pos.GetY(), pos.GetZ() );
		return OffsetPtr( m_pixels, offset );
	}

	RED_FORCE_INLINE void* ImageAccess::GetPixelData( const Uint32 x, const Uint32 y, const Uint32 z )
	{
		const Uint32 offset = m_layout.CalcPixelOffset( x, y, z );
		return OffsetPtr( m_pixels, offset );
	}

	RED_FORCE_INLINE const void* ImageAccess::GetPixelData( const Uint32 x, const Uint32 y, const Uint32 z ) const
	{
		const Uint32 offset = m_layout.CalcPixelOffset( x, y, z );
		return OffsetPtr( m_pixels, offset );
	}

	template< typename Format >
	RED_FORCE_INLINE void ImageAccess::Unpack( const ImageIterator& pos, ImagePixel& outPixel ) const
	{
		Format::Unpack( GetPixelData( pos ), outPixel );
	}

	template< typename Format >
	RED_FORCE_INLINE void ImageAccess::Unpack( const Uint32 x, const Uint32 y, const Uint32 z, ImagePixel& outPixel ) const
	{
		Format::Unpack( GetPixelData( x, y, z ), outPixel );
	}

	template< typename Format >
	RED_FORCE_INLINE void ImageAccess::Pack( const ImageIterator& pos, const ImagePixel& inPixel )
	{
		Format::Pack( GetPixelData( pos ), inPixel );
	}

	template< Int32 KX, Int32 KY, Int32 KZ, typename Format, typename Kernel >
	RED_FORCE_INLINE void ImageAccess::RunKernel( const ImageIterator& pos, ImagePixel& outPixel ) const
	{
		Float weightSum = 0.0f;

		// X iteration range
		const Int32 minX = math::Max< Int32 >( pos.GetX() - KX, pos.GetMinX() ) - pos.GetX();
		const Int32 maxX = math::Min< Int32 >( pos.GetX() + KX, pos.GetMaxX()-1 ) - pos.GetX();

		// Y iteration range
		const Int32 minY = math::Max< Int32 >( pos.GetY() - KY, pos.GetMinY() ) - pos.GetY();
		const Int32 maxY = math::Min< Int32 >( pos.GetY() + KY, pos.GetMaxY()-1 ) - pos.GetY();

		// Z iteration range
		const Int32 minZ = math::Max< Int32 >( pos.GetZ() - KZ, pos.GetMinZ() ) - pos.GetZ();
		const Int32 maxZ = math::Min< Int32 >( pos.GetZ() + KZ, pos.GetMaxZ()-1 ) - pos.GetZ();

		// Process pixels
		for ( Int32 dz = minZ; dz <= maxZ; ++dz )
		{
			for ( Int32 dy = minY; dy <= maxY; ++dy )
			{
				for ( Int32 dx = minX; dx <= maxX; ++dx )
				{
					// get displaced value
					const Uint32 offset = m_layout.CalcPixelOffset( pos.GetX() + dx, pos.GetY() + dy, pos.GetZ() + dz );

					// unpack pixel
					ImagePixel curPixel;
					Format::Unpack( OffsetPtr( m_pixels, offset ), curPixel );

					// accumulate
					weightSum += Kernel::Process( outPixel, curPixel, dx, dy, dz );
				}
			}
		}

		// Normalize result
		Kernel::Normalize( outPixel, weightSum );
	}

} // img