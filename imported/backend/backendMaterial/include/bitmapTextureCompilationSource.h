/**
* Copyright (c) 2007-2016 CD Projekt Red. All Rights Reserved.
*/

#pragma once

#include "../../../common/renderBackend/include/renderTextureCompilationSource.h"

class CBitmapTexture;

class BACKEND_MATERIAL_API CBitmapTextureCompilationSource : public tools::ITextureCompilationSource, public red::NonCopyable
{
private:
	CBitmapTexture*	m_texture;
	Uint8			m_mipsCount;
	Uint8			m_startMip;
	Uint8			m_maxMipCount;

public:
	CBitmapTextureCompilationSource( CBitmapTexture* texture, ECookingPlatform platform );

	virtual const res::ResourcePath& GetTextureName() const override;

	virtual Uint8 GetMaxMipCount() const override;
	virtual Uint8 GetMipCount() const override;
	virtual Uint8 GetStartMip() const override;

	virtual Uint16 GetBaseWidth() const override;
	virtual Uint16 GetBaseHeight() const override;
	virtual Uint16 GetBaseDepth() const override;

	virtual Uint32 GetDownscaleBias() const override;

	virtual Uint16 GetSliceCount() const override { return 1; }
	virtual img::ImagePtr GetImage( Uint16 texSlice ) const override;
	virtual const MipMapGenParams& GetMipMapGenParams() const override;

	virtual GpuWrapApi::eTextureType GetTextureType() const override;
	virtual const STextureGroupSetup& GetTextureSetup() const override;
};
