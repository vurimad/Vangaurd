/**
* Copyright (c) 2007 CD Projekt Red. All Rights Reserved.
*/
#pragma once

#include "textureEnums.h"
#include "renderTextureBlob.h"

//*****************************************************************************************************************************//
//*****************************************************************************************************************************//

enum class ETexturePlatform
{
	PC,
	PS4,
	XBOX,
	MAX
};

enum ETextureChannel : Uint8
{
	TextureChannel_R = RED_FLAG( 0 ),
	TextureChannel_G = RED_FLAG( 1 ),
	TextureChannel_B = RED_FLAG( 2 ),
	TextureChannel_A = RED_FLAG( 3 )
};

/// Texture group information
struct RENDER_DATA_API STextureGroupSetup
{
	RTTI_DECLARE_TYPE( STextureGroupSetup );

	ETextureCompression			m_compression;		//!< Texture compression to use
	ETextureRawFormat			m_rawFormat;
	GpuWrapApi::eTextureGroup	m_group;			//!< Name of the group
	Uint8						m_platformMipBiasPC;
	Uint8						m_platformMipBiasConsole;
	Bool						m_isStreamable;		//!< Can this type of textures be streamed
	Bool						m_hasMipchain;		//!< Whether we should generate a full mipchain for the texture
	Bool						m_isGamma;			//!< Texture is encoded in gamma space
#ifndef RED_CONFIGURATION_FINAL
	Bool						m_stretchHistogram;	//!< Whether to stretch histogram
	Uint8						m_alphaToCoverageThreshold; // Whether to boost alpha channel in mipmaps to preserve alpha coverage from the original image (matters in shaders that discard pixels based on some cutoff alpha). 0 means no boosting.
	Uint8						m_alphaToCoverageThreshold_Channel;
#endif // RED_CONFIGURATION_FINAL
	bool						m_allowTextureDowngrade; // ctremblay: temp hack to remove downgrade for some texture

	STextureGroupSetup();
	~STextureGroupSetup();

	RED_FORCE_INLINE const Uint8 GetPlatformMipBias( ETexturePlatform platform ) const
	{
		switch( platform )
		{
		case ETexturePlatform::PC:		return m_platformMipBiasPC;
		case ETexturePlatform::PS4:		return m_platformMipBiasConsole;
		case ETexturePlatform::XBOX:	return m_platformMipBiasConsole;
		}
		return 0;
	}

};

//*****************************************************************************************************************************//
//*****************************************************************************************************************************//


namespace TexGroupHelpers
{
	// Recieve matching texture format from filenam eextension
	RENDER_DATA_API GpuWrapApi::eTextureImportFormat FileExtensionToFormat( const red::StringView extension );

	// Recieve pixel size and format info from GPU texture type
	RENDER_DATA_API void MapGpuToEngineTextureFormat( const GpuWrapApi::eTextureFormat texFormat, ETextureRawFormat& outRawFormat, ETextureCompression& outCompression, Uint32& outPixelSize );

	// Creates most matching texture group setup from GPU texture type
	RENDER_DATA_API void CreateBaseTextureGroup( const GpuWrapApi::eTextureFormat texFormat, STextureGroupSetup& outSetup );

	// Creates most matching texture group setup from GPU texture type
	RENDER_DATA_API void CreateBaseTextureGroup( const GpuWrapApi::eTextureGroup textureGroup, STextureGroupSetup& outSetup );

};

//*****************************************************************************************************************************//
//*****************************************************************************************************************************//
