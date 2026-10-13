/**
* Copyright (c) 2007-2016 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"

#include "cubeTextureCompilationSource.h"
#include "../../../common/resourceMaterial/include/bitmapTexture.h"
#include "../../../common/resourceMaterial/include/cubeTexture.h"

CCubeTextureCompilationSource::CCubeTextureCompilationSource( CCubeTexture* texture, ECookingPlatform platform )
	: ITextureCompilationSource( platform )
	, m_texture( texture )
{
	m_startMip			= 0; // (Uint8)texture->GetResidentMipForCookingPlatform( platform );
	m_maxMipCount		= texture->GetMaxMipCount();
	m_numMips			= m_maxMipCount;
}

const res::ResourcePath& CCubeTextureCompilationSource::GetTextureName() const
{
	return m_texture->GetPath();
}

img::ImagePtr CCubeTextureCompilationSource::GetImage( Uint16 texSlice ) const 
{
	return m_texture->GetImage( texSlice );
}

Uint16 CCubeTextureCompilationSource::GetBaseWidth() const 
{
	return (Uint16)Max( m_texture->GetSize() >> m_startMip, 1u );
}

Uint16 CCubeTextureCompilationSource::GetBaseHeight() const 
{
	return (Uint16)Max( m_texture->GetSize() >> m_startMip, 1u );
}

Uint8 CCubeTextureCompilationSource::GetMaxMipCount() const
{
	return m_maxMipCount;
}

Uint8 CCubeTextureCompilationSource::GetMipCount() const
{
	return m_numMips;
}

Uint8 CCubeTextureCompilationSource::GetStartMip() const
{
	return m_startMip;
}

void CCubeTextureCompilationSource::GetFlipSettings( Uint16 slice, Bool& flipX, Bool& flipY, Bool& rotate) const 
{
	Bool xHoriz = m_texture->GetSourceTextureType() == CST_CrossHorizontal;
	Bool xVert = m_texture->GetSourceTextureType() == CST_CrossVertical;
	Bool panor = m_texture->GetSourceTextureType() == CST_Panorama;
	switch( slice )
	{
		case 0:	flipX = false; flipY = false;					 rotate = true;		break; // right
		case 1:	flipX = true;  flipY = true;					 rotate = true;		break; // left
		case 2:	flipX = false; flipY = true;					 rotate = false;	break; // front
		case 3:	flipX = !xVert;flipY = xVert;					 rotate = false;	break; // back
		case 4:	flipX = panor; flipY = panor || xHoriz || xVert; rotate = panor;	break; // top
		case 5:	flipX = true;  flipY = panor;					 rotate = panor;	break; // bottom
	}
}

GpuWrapApi::eTextureType CCubeTextureCompilationSource::GetTextureType() const
{
	return GpuWrapApi::TEXTYPE_CUBE;
}

const STextureGroupSetup& CCubeTextureCompilationSource::GetTextureSetup() const
{
	return m_texture->GetTextureSetup();
}
