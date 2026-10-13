/**
* Copyright (c) 2007-2016 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"

#include "bitmapTextureCompilationSource.h"
#include "textureArrayCompilationSource.h"
#include "../../../common/resourceMaterial/include/textureArray.h"
#include "../../../common/resourceMaterial/include/bitmapTexture.h"
#include "../../../common/redReflection/include/util.h"

using red::DynArray;

CTextureArrayCompilationSource::CTextureArrayCompilationSource( const CTextureArray* texture, ECookingPlatform platform, const DynArray< CBitmapTexture* >* bitmapOverrides /*=nullptr*/ )
	: ITextureCompilationSource( platform )
	, m_texture( texture )
{
	RED_WARNING( m_texture != nullptr, "No texture!" );

	const STextureGroupSetup& setup = m_texture->GetTextureSetup();

	if( setup.m_hasMipchain )
	{
		m_startMip			= 0;//(Uint8)texture->GetHighestMipForCookingPlatform( platform );
		m_maxMipCount		= m_texture->GetMaxMipCount();
		m_mipCount			= m_maxMipCount - m_startMip;
	}
	else
	{
		m_residentMipMap	= 0;
		m_startMip			= 0;
		m_maxMipCount		= 1;
		m_mipCount			= 1;
	}

	const Uint32 numTextures = m_texture->GetSizeDesc().depth;

	m_bitmaps.Resize( numTextures );
	for ( Uint32 i = 0; i < numTextures; ++i )
	{
		THandle< CBitmapTexture > tex = m_texture->GetTexture( i );

		if ( bitmapOverrides && (*bitmapOverrides)[i] )
		{
			tex = HandleFromPtr( (*bitmapOverrides)[i] );
		}

		if ( tex == nullptr )
		{
			//RED_LOG_ERROR( "Engine: Failed to open %ls for %hs slice %u"), m_texture->GetTexturePath( i ).AsChar(), m_texture->GetFriendlyName().AsChar(), i );
		}

		m_bitmaps[i] = tex;
	}
}

const res::ResourcePath& CTextureArrayCompilationSource::GetTextureName() const
{
	return m_texture->GetPath();
}

GpuWrapApi::eTextureType CTextureArrayCompilationSource::GetTextureType() const
{
	return GpuWrapApi::TEXTYPE_ARRAY;
}

const STextureGroupSetup& CTextureArrayCompilationSource::GetTextureSetup() const
{
	return m_texture->GetTextureSetup();
}

img::ImagePtr CTextureArrayCompilationSource::GetImage( Uint16 texSlice ) const 
{
	if ( !m_bitmaps[texSlice] )
		return img::ImagePtr();

	return m_bitmaps[texSlice]->GetImage();
}

Uint32 CTextureArrayCompilationSource::GetDownscaleBias( ) const 
{
	RED_FATAL_ASSERT( m_texture );

	const auto& setup = m_texture->GetTextureSetup();

	switch( GetPlatform() )
	{
	case PLATFORM_PC:
	case PLATFORM_WindowsServer:	return setup.m_platformMipBiasPC;
	case PLATFORM_PS4:		return setup.m_platformMipBiasConsole;
	case PLATFORM_XboxOne:	return setup.m_platformMipBiasConsole;
	default:				RED_FATAL( "Unknown platform" );
	}
	return 0;
}

Uint16 CTextureArrayCompilationSource::GetSliceCount() const
{
	RED_FATAL_ASSERT( m_texture );
	return (Uint16)m_texture->GetSizeDesc().depth;
}

Uint8 CTextureArrayCompilationSource::GetMaxMipCount() const
{
	return m_maxMipCount;
}

Uint8 CTextureArrayCompilationSource::GetMipCount() const
{
	return m_mipCount;
}

Uint16 CTextureArrayCompilationSource::GetBaseWidth() const
{
	return Max( m_texture->GetSizeDesc().width >> m_startMip, 1ui16 );
}

Uint16 CTextureArrayCompilationSource::GetBaseHeight() const
{
	return Max( m_texture->GetSizeDesc().height >> m_startMip, 1ui16 );
}

Uint8 CTextureArrayCompilationSource::GetStartMip() const
{
	return m_startMip;
}
