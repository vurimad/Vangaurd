/**
* Copyright (c) 2007-2016 CD Projekt Red. All Rights Reserved.
*/

#pragma once

#include "../../../common/renderBackend/include/renderTextureCompilationSource.h"

class CCubeTexture;

class CCubeTextureCompilationSource : public tools::ITextureCompilationSource, public red::NonCopyable
{
private:
	CCubeTexture*			m_texture;
	Uint8					m_startMip;
	Uint8					m_numMips;
	Uint8					m_maxMipCount;

public:
	CCubeTextureCompilationSource( CCubeTexture* texture, ECookingPlatform platform );

	virtual const res::ResourcePath& GetTextureName() const override;

	virtual Uint8 GetMaxMipCount() const override;
	virtual Uint8 GetMipCount() const override;
	virtual Uint8 GetStartMip() const override;

	virtual Uint16 GetBaseWidth() const override;
	virtual Uint16 GetBaseHeight() const override;
	virtual Uint16 GetBaseDepth() const override { return 1; }	// for 3D tex only

	virtual Uint32 GetDownscaleBias() const { return 0; }

	virtual Uint16 GetSliceCount() const override { return 6; }
	virtual img::ImagePtr GetImage( Uint16 texSlice ) const override;

	virtual void GetFlipSettings( Uint16 slice, Bool& flipX, Bool& flipY, Bool& rotate ) const override;

	virtual GpuWrapApi::eTextureType GetTextureType() const override;
	virtual const STextureGroupSetup& GetTextureSetup() const override;
};
