/**
* Copyright (c) 2007-2016 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"

#include "bitmapTextureCompilationSource.h"
#include "../../../common/resourceMaterial/include/bitmapTexture.h"
#include "../../../common/renderData/include/renderSimpleTypes.h"

CBitmapTextureCompilationSource::CBitmapTextureCompilationSource( CBitmapTexture* texture, ECookingPlatform platform )
	: ITextureCompilationSource( platform )
	, m_texture( texture )
{
	const auto& setup = m_texture->GetTextureSetup();

	if( setup.m_hasMipchain )
	{
		m_startMip		= (Uint8)texture->GetHighestMipForCookingPlatform( platform );
		m_maxMipCount	= m_texture->GetMaxMipCount();
		m_mipsCount		= m_maxMipCount - m_startMip;
	}
	else
	{
		m_startMip		= 0;
		m_mipsCount		= 1;
		m_maxMipCount	= 1;
	}
}

const res::ResourcePath& CBitmapTextureCompilationSource::GetTextureName() const
{
	return m_texture->GetPath();
}

GpuWrapApi::eTextureType CBitmapTextureCompilationSource::GetTextureType() const
{
	return GetBaseDepth() == 1 ? GpuWrapApi::TEXTYPE_2D : GpuWrapApi::TEXTYPE_3D;
}

const STextureGroupSetup& CBitmapTextureCompilationSource::GetTextureSetup() const
{
	return m_texture->GetTextureSetup();
}

img::ImagePtr CBitmapTextureCompilationSource::GetImage( Uint16 texSlice ) const 
{
	return m_texture->GetImage();
}

const MipMapGenParams& CBitmapTextureCompilationSource::GetMipMapGenParams() const
{
	return m_texture->GetMipMapGenParams();
}
Uint32 CBitmapTextureCompilationSource::GetDownscaleBias() const 
{
	return m_texture->GetDownscaleBiasForCookingPlatform( GetPlatform() );
}

Uint16 CBitmapTextureCompilationSource::GetBaseWidth() const 
{
	return m_texture->GetSizeDesc().width >> m_startMip;
}

Uint16 CBitmapTextureCompilationSource::GetBaseHeight() const 
{
	return m_texture->GetSizeDesc().height >> m_startMip;
}

Uint16 CBitmapTextureCompilationSource::GetBaseDepth() const
{
	return Max( 1ui16, m_texture->GetSizeDesc().depth >> m_startMip );
}

Uint8 CBitmapTextureCompilationSource::GetMaxMipCount() const
{
	return m_maxMipCount;
}

Uint8 CBitmapTextureCompilationSource::GetMipCount() const
{
	return m_mipsCount;
}

Uint8 CBitmapTextureCompilationSource::GetStartMip() const
{
	return m_startMip;
}