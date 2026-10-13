/**
* Copyright (c) 2016 CD Projekt Red. All Rights Reserved.
*/

#pragma once

#include "../../redImage/include/redImagePublic.h"
#include "../../resourceMaterial/include/mipMapGenParams.h"

struct STextureGroupSetup;

enum ECookingPlatform : Uint8;

namespace tools
{
	struct HistogramBias
	{
		Vector3 m_mulCoef;
		Vector3 m_addCoef;
	};

	class ITextureCompilationSource
	{
		RED_USE_MEMORY_POOL( red::PoolBackend );

		ECookingPlatform			m_platform;

	public:
		ITextureCompilationSource( ECookingPlatform platform )
			: m_platform( platform )
		{}

		virtual ~ITextureCompilationSource() {}

		// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
		// Accessors

		RED_FORCE_INLINE ECookingPlatform	GetPlatform() const { return m_platform; }

		// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
		// Interface

		virtual const res::ResourcePath& GetTextureName() const = 0;

		virtual Uint8 GetMaxMipCount() const = 0;
		virtual Uint8 GetMipCount() const = 0;
		virtual Uint8 GetStartMip() const = 0;

		virtual Uint16 GetBaseWidth() const = 0;
		virtual Uint16 GetBaseHeight() const = 0;
		virtual Uint16 GetBaseDepth() const = 0;

		virtual Uint32 GetDownscaleBias()  const = 0;

		virtual Uint16 GetSliceCount() const = 0;
		virtual img::ImagePtr GetImage( Uint16 texSlice ) const = 0;
		virtual const MipMapGenParams& GetMipMapGenParams() const { return MipMapGenParams::NullParams(); };

		virtual void GetFlipSettings( Uint16 slice, Bool& flipX, Bool& flipY, Bool& rotate ) const { flipX = false; flipY = false; rotate = false; };

		virtual GpuWrapApi::eTextureType GetTextureType() const = 0;
		virtual const STextureGroupSetup& GetTextureSetup() const = 0;

		// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
	};
}