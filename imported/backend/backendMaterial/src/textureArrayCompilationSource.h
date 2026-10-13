/**
* Copyright (c) 2007-2016 CD Projekt Red. All Rights Reserved.
*/

#pragma once

#include "../../../common/renderBackend/include/renderTextureCompilationSource.h"

class CTextureArray;

class CTextureArrayCompilationSource : public tools::ITextureCompilationSource, public red::NonCopyable
{
private:
	const CTextureArray*					m_texture;		//! The texture array we're cooking.
	red::DynArray< THandle< CBitmapTexture > >	m_bitmaps;		//! Cached from m_texture, so we don't have to repeatedly call GetTextures()
	Uint8									m_mipCount;
	Uint8									m_startMip;
	Uint8									m_residentMipMap;
	Uint8									m_maxMipCount;

public:
	CTextureArrayCompilationSource( const CTextureArray* texture, ECookingPlatform platform, const red::DynArray< CBitmapTexture* >* bitmapOverrides = nullptr );

	virtual const res::ResourcePath& GetTextureName() const override;

	virtual Uint8 GetMaxMipCount() const override;
	virtual Uint8 GetMipCount() const override;
	virtual Uint8 GetStartMip() const override;

	virtual Uint16 GetBaseWidth() const override;
	virtual Uint16 GetBaseHeight() const override;
	virtual Uint16 GetBaseDepth() const override { return 1; }	// for 3D tex only

	virtual Uint32 GetDownscaleBias() const override;

	virtual Uint16 GetSliceCount() const override;
	virtual img::ImagePtr GetImage( Uint16 texSlice ) const override;

	virtual GpuWrapApi::eTextureType GetTextureType() const override;
	virtual const STextureGroupSetup& GetTextureSetup() const override;
};
