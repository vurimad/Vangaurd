/**
* Copyright (c) 2016 CDProjekt Red, Inc. All Rights Reserved.
*/

/// Original code (C) RenderLab 2014 by Tomasz "RexDex" Jonarski
/// Copied with permission from author by author :)

#include "build.h"

#include "image.h"
#include "imageAccess.h"

namespace img
{

	ImageAccess::ImageAccess()
		: m_format()
		, m_size{ 0, 0, 0, 0 }
		, m_layout{ 0, 0, 0, 0}
		, m_gammaExp( 1.0f )
		, m_invGammaExp( 1.0f )
		, m_pixels( nullptr )
	{
	}

	ImageAccess::ImageAccess( const ImageAccess& other )
		: m_format( other.m_format )
		, m_size( other.m_size )
		, m_layout( other.m_layout )
		, m_gammaExp( other.m_gammaExp )
		, m_invGammaExp( other.m_invGammaExp )
		, m_pixels( other.m_pixels )
	{
	}

	ImageAccess::ImageAccess( Image& image )
		: m_format( image.GetPixelFormat() )
		, m_size( image.GetSize() )
		, m_layout( image.GetLayout() )
		, m_gammaExp( image.GetGammaExp() )
		, m_invGammaExp( 1.0f / image.GetGammaExp() )
		, m_pixels( image.GetPixelBuffer() )
	{
	}

	ImageAccess::ImageAccess( const Image& image )
		: m_format( image.GetPixelFormat() )
		, m_size( image.GetSize() )
		, m_layout( image.GetLayout() )
		, m_gammaExp( image.GetGammaExp() )
		, m_invGammaExp( 1.0f / image.GetGammaExp() )
		, m_pixels( const_cast< void* >( image.GetPixelBuffer() ) )
	{
	}

} // img
