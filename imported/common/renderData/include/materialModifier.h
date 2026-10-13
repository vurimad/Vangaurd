/*
 * Copyright (c) 2010 CD Projekt Red. All Rights Reserved.
 */
#pragma once

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

//
#if defined( VANGUARD_ASSET_HOST )
#include "../../../engine/shaders/include/materialModifiersConstantBuffers.fx"
#else
#include "../../../../../engine/shaders/include/materialModifiersConstantBuffers.fx"
#endif

enum EMaterialModifier : Int32
{
	EMATMOD_HitProxy			= MM_HitProxy,
	EMATMOD_WindData			= MM_WindData,
	EMATMOD_ParticleParams		= MM_ParticleParams,
	EMATMOD_RemoteCamera		= MM_RemoteCamera,
	EMATMOD_Mirror				= MM_Mirror,
	EMATMOD_CustomStructBuffer	= MM_CustomStructBuffer,
	EMATMOD_EffectParams		= MM_EffectParams,
	EMATMOD_MotionMatrix		= MM_MotionMatrix,
	EMATMOD_ColorAndTexture		= MM_ColorAndTexture,
	EMATMOD_MaterialParams		= MM_MaterialParams,
	EMATMOD_Eye					= MM_Eye,
	EMATMOD_Skin				= MM_Skin,
	EMATMOD_VehicleParams		= MM_VehicleParams,
	EMATMOD_Dismemberment		= MM_Dismemberment,
	EMATMOD_Garments			= MM_Garments,
	EMATMOD_ShadowsDebugParams  = MM_ShadowsDebugParams,
	EMATMOD_MultilayeredDebug   = MM_MultilayeredDebug,
	EMATMOD_ParallaxParams      = MM_ParallaxParams,
	EMATMOD_HighlightsParams    = MM_HighlightsParams,
	EMATMOD_DebugColoring       = MM_DebugColoring,
	EMATMOD_DrawBufferMask		= MM_DrawBufferMask,
	EMATMOD_AutoSpawnData		= MM_AutoSpawnData,
	EMATMOD_DestructionRegions	= MM_DestructionRegions,
	EMATMOD_FloatTracks			= MM_FloatTracks,
	EMATMOD_AutoHideDistance	= MM_AutoHideDistance,
	EMATMOD_Rain				= MM_Rain,
	EMATMOD_PlanarReflections	= MM_PlanarReflections,
	EMATMOD_MAX
};
RTTI_DECLARE_ENUM( EMaterialModifier );

typedef red::BitSet<EMATMOD_MAX> MaterialModifierMask;

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

struct RENDER_DATA_API MaterialModifierDesc
{
	RED_USE_MEMORY_POOL( rend::PoolMaterialModifier );

	struct Attribute
	{
		RED_USE_MEMORY_POOL( rend::PoolMaterialModifier );

		CName	m_name;
		Int8	m_value;
	};

	EMaterialModifier					m_type;
	red::DynArray< Attribute >			m_attributes{ rend::PoolMaterialModifier() };

	Int8 GetValue( CName attrName ) const
	{
		for ( const Attribute& attr : m_attributes )
		{
			if ( attrName == attr.m_name )
			{
				return attr.m_value;
			}
		}
		return -1;
	}
};
typedef red::DynArray< MaterialModifierDesc > MaterialModifierDescArray;

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

struct RENDER_DATA_API SMatModifierDataBase
{
	// empty struct just to provide memory pool
	RED_USE_MEMORY_POOL( rend::PoolMaterialModifier );
};

struct RENDER_DATA_API SMatModifierDataHitProxy : public SMatModifierDataBase
{
	static const EMaterialModifier ModType = EMATMOD_HitProxy;

	static SMatModifierDataHitProxy Create( const MaterialModifierDesc& modifierDesc )
	{
		return SMatModifierDataHitProxy( modifierDesc.GetValue( RED_NAME_CONSTEXPR( "MatMod_HitProxyIDSlot" ) ), modifierDesc.GetValue( RED_NAME_CONSTEXPR( "MatMod_SelectionAreaSlot" ) ) );
	}

	SMatModifierDataHitProxy( Int8 idSlot, Int8 selectionAreaSlot )
		: m_idSlot( idSlot )
		, m_selectionAreaSlot( selectionAreaSlot )
	{}

	Bool IsValid() const { return m_idSlot != -1 && m_selectionAreaSlot != -1; }

	Int8 m_idSlot;
	Int8 m_selectionAreaSlot;
};

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

struct RENDER_DATA_API SMatModifierDataDrawBufferMask : public SMatModifierDataBase
{
	static const EMaterialModifier ModType = EMATMOD_DrawBufferMask;

	static SMatModifierDataDrawBufferMask Create( const MaterialModifierDesc& modifierDesc )
	{
		return SMatModifierDataDrawBufferMask(
			modifierDesc.GetValue( RED_NAME_CONSTEXPR( "MatMod_MaskTexture0" ) ),
			modifierDesc.GetValue( RED_NAME_CONSTEXPR( "MatMod_MaskTexture1" ) ),
			modifierDesc.GetValue( RED_NAME_CONSTEXPR( "MatMod_MaskRect0" ) ),
			modifierDesc.GetValue( RED_NAME_CONSTEXPR( "MatMod_MaskRect1" ) ),
			modifierDesc.GetValue( RED_NAME_CONSTEXPR( "MatMod_MaskCoords0" ) ),
			modifierDesc.GetValue( RED_NAME_CONSTEXPR( "MatMod_MaskCoords1" ) ),
			modifierDesc.GetValue( RED_NAME_CONSTEXPR( "MatMod_MaskParams" ) )
		);
	}

	SMatModifierDataDrawBufferMask( Int8 idMaskTexture0, Int8 idMaskTexture1, Int8 idMaskRect0, Int8 idMaskRect1, Int8 idMaskCoords0, Int8 idMaskCoords1, Int8 idMaskParams )
		: m_idMaskTextures{ idMaskTexture0, idMaskTexture1 }
		, m_idMaskRects{ idMaskRect0, idMaskRect1 }
		, m_idMaskCoords{ idMaskCoords0, idMaskCoords1 }
		, m_idMaskParams( idMaskParams )
	{
	}

	Bool IsValid() const
	{
		return
			m_idMaskTextures[ 0 ] != -1 || m_idMaskTextures[ 1 ] != -1 ||
			m_idMaskRects[ 0 ] != -1 || m_idMaskRects[ 1 ] != -1 ||
			m_idMaskCoords[ 0 ] != -1 || m_idMaskCoords[ 1 ] != -1 ||
			m_idMaskParams != -1;
	}


	// TODO move this to structured buffer to support more masks and parameters
	Int8 m_idMaskTextures[ 3 ];
	Int8 m_idMaskRects[ 3 ];
	Int8 m_idMaskCoords[ 3 ];
	Int8 m_idMaskParams;
};

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

struct RENDER_DATA_API SMatModifierDataEffectParams : public SMatModifierDataBase
{
	static const EMaterialModifier ModType = EMATMOD_EffectParams;

	static SMatModifierDataEffectParams Create( const MaterialModifierDesc& modifierDesc )
	{
		return SMatModifierDataEffectParams(
			modifierDesc.GetValue( RED_NAME_CONSTEXPR( "MatMod_EffectTexture" ) ),
			modifierDesc.GetValue( RED_NAME_CONSTEXPR( "MatMod_EffectRect" ) ),
			modifierDesc.GetValue( RED_NAME_CONSTEXPR( "MatMod_EffectCoords" ) ),
			modifierDesc.GetValue( RED_NAME_CONSTEXPR( "MatMod_EffectParams" ) )
		);
	}

	SMatModifierDataEffectParams( Int8 idEffectTexture, Int8 idEffectRect, Int8 idTextureCoords, Int8 idEffectParams )
		: m_idTexture( idEffectTexture )
		, m_idRect ( idEffectRect )
		, m_idTextureCoords( idTextureCoords )
		, m_idParams( idEffectParams )
	{
	}

	Bool IsValid() const
	{
		return
			m_idTexture != -1 ||
			m_idRect != -1 || 
			m_idTextureCoords != -1 || 
			m_idParams != -1;
	}


	// TODO move this to structured buffer to support more masks and parameters
	Int8 m_idTexture;
	Int8 m_idRect;
	Int8 m_idTextureCoords;
	Int8 m_idParams;
};

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

struct RENDER_DATA_API SMatModifierDataColorAndTexture : public SMatModifierDataBase
{
	static const EMaterialModifier ModType = EMATMOD_ColorAndTexture;

	static SMatModifierDataColorAndTexture Create( const MaterialModifierDesc& modifierDesc )
	{
		return SMatModifierDataColorAndTexture(
			modifierDesc.GetValue( RED_NAME_CONSTEXPR( "MatMod_Color" ) ),
			modifierDesc.GetValue( RED_NAME_CONSTEXPR( "MatMod_Texture" ) ),
			modifierDesc.GetValue( RED_NAME_CONSTEXPR( "MatMod_Texture2" ) ),
			modifierDesc.GetValue( RED_NAME_CONSTEXPR( "MatMod_CustomParams0" ) ),
			modifierDesc.GetValue( RED_NAME_CONSTEXPR( "MatMod_CustomParams1" ) ),
			modifierDesc.GetValue( RED_NAME_CONSTEXPR( "MatMod_CustomParams2" ) )
		);
	}

	SMatModifierDataColorAndTexture( Int8 idColor, Int8 idTexture, Int8 idTexture2, Int8 idCustomParams0, Int8 idCustomParams1, Int8 idCustomParams2 )
		: m_idColor( idColor )
		, m_idTexture( idTexture )
		, m_idTexture2( idTexture2 )
		, m_customParams{ idCustomParams0, idCustomParams1, idCustomParams2 }
	{}

	Bool IsValid() const
	{
		return m_idColor != -1 || m_idTexture != -1 || m_idTexture2 != -1 || m_customParams[ 0 ] != -1 || m_customParams[ 1 ] != -1 || m_customParams[ 2 ] != -1;
	}

	Int8 m_idColor;
	Int8 m_idTexture;
	Int8 m_idTexture2;
	Int8 m_customParams[ 3 ];
};

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

struct RENDER_DATA_API SMatModifierDataWindData : public SMatModifierDataBase
{
	static const EMaterialModifier ModType = EMATMOD_WindData;

	static SMatModifierDataWindData Create( const MaterialModifierDesc& modifierDesc )
	{
		return SMatModifierDataWindData( modifierDesc.GetValue( RED_NAME_CONSTEXPR( "MatMod_WindDataCBSlot" ) ) );
	}

	SMatModifierDataWindData( Int8 windDataCBSlot )
		: m_windDataCBSlot( windDataCBSlot )
	{}

	Bool IsValid() const { return m_windDataCBSlot != -1; }

	Int8 m_windDataCBSlot;
};

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

struct RENDER_DATA_API SMatModifierDataAutoSpawnData : public SMatModifierDataBase
{
	static const EMaterialModifier ModType = EMATMOD_AutoSpawnData;

	static SMatModifierDataAutoSpawnData Create( const MaterialModifierDesc& modifierDesc )
	{
		return SMatModifierDataAutoSpawnData( modifierDesc.GetValue( RED_NAME_CONSTEXPR( "MatMod_AutoSpawnCBSlot" ) ),
											  modifierDesc.GetValue( RED_NAME_CONSTEXPR( "MatMod_AutoSpawnInstanceBufferSlot" ) ),
											  modifierDesc.GetValue( RED_NAME_CONSTEXPR( "MatMod_AutoSpawnInstanceBucketsSlot" ) ) );
	}

	SMatModifierDataAutoSpawnData( Int8 autoSpawnCBSlot, Int8 autoSpawnInstanceBufferSlot, Int8 autoSpawnInstanceBucketsSlot )
		: m_autoSpawnCBSlot( autoSpawnCBSlot ),
		m_autoSpawnInstanceBufferSlot( autoSpawnInstanceBufferSlot ),
		m_autoSpawnInstanceBucketsSlot( autoSpawnInstanceBucketsSlot )

	{}

	Bool IsValid() const { return m_autoSpawnCBSlot != -1 && m_autoSpawnInstanceBufferSlot != -1 && m_autoSpawnInstanceBucketsSlot != -1; }

	Int8 m_autoSpawnCBSlot;
	Int8 m_autoSpawnInstanceBufferSlot;
	Int8 m_autoSpawnInstanceBucketsSlot;
};

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

struct RENDER_DATA_API SMatModifierDataAutoHideDistance : public SMatModifierDataBase
{
	static const EMaterialModifier ModType = EMATMOD_AutoHideDistance;

	static SMatModifierDataAutoHideDistance Create( const MaterialModifierDesc& modifierDesc )
	{
		return SMatModifierDataAutoHideDistance( modifierDesc.GetValue( RED_NAME_CONSTEXPR( "MatMod_AutoHideDistance" ) ) );
	}

	SMatModifierDataAutoHideDistance( Int8 autoHideDistanceSlot )
		: m_autoHideDistanceSlot( autoHideDistanceSlot )

	{}

	Bool IsValid() const { return m_autoHideDistanceSlot != -1; }

	Int8 m_autoHideDistanceSlot;
};

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

struct RENDER_DATA_API SMatModifierGarments : public SMatModifierDataBase
{
	static const EMaterialModifier ModType = EMATMOD_Garments;

	static SMatModifierGarments Create(const MaterialModifierDesc& modifierDesc)
	{
		SMatModifierGarments matmod;
		matmod.m_morphOffsetScalesTextureSlot = modifierDesc.GetValue( RED_NAME_CONSTEXPR( "MatMod_GarmentMorphOffsetScales" ) );
		matmod.m_morphOffsetScalesTextureIsBoundSlot = modifierDesc.GetValue( RED_NAME_CONSTEXPR( "MatMod_GarmentMorphOffsetScalesIsBound" ) );
		return matmod;
	}

	Bool IsValid() const { return m_morphOffsetScalesTextureSlot != -1 || m_morphOffsetScalesTextureIsBoundSlot != -1; }

	Int8 m_morphOffsetScalesTextureSlot = -1;
	Int8 m_morphOffsetScalesTextureIsBoundSlot = -1;
};

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

struct RENDER_DATA_API SMatModifierDataParticleParams : public SMatModifierDataBase
{
	static const EMaterialModifier ModType = EMATMOD_ParticleParams;

	static SMatModifierDataParticleParams Create( const MaterialModifierDesc& modifierDesc )
	{
		return SMatModifierDataParticleParams( modifierDesc.GetValue( RED_NAME_CONSTEXPR( "MatMod_TransparencySlot" ) ), modifierDesc.GetValue( RED_NAME_CONSTEXPR( "MatMod_BBoxInfoSlot" ) ), modifierDesc.GetValue( RED_NAME_CONSTEXPR( "MatMod_MotionBlurSlot" ) ), modifierDesc.GetValue(RED_NAME_CONSTEXPR( "MatMod_OrientationUSlot" ) ), modifierDesc.GetValue( RED_NAME_CONSTEXPR( "MatMod_OrientationVSlot" ) ), modifierDesc.GetValue( RED_NAME_CONSTEXPR( "MatMod_DPLParticleDataSlot" ) ) );
	}

	SMatModifierDataParticleParams( Int8 transparencyParamsSlot, Int8 bboxInfoSlot, Int8 motionBlurParamsSlot, Int8 orientationUSlot, Int8 orientationVSlot, Int8 dplParticleDataSlot )
		: m_transparencyParamsSlot( transparencyParamsSlot )
		, m_bboxInfoSlot( bboxInfoSlot )
		, m_motionBlurParamsSlot( motionBlurParamsSlot )
		, m_orientationUSlot( orientationUSlot )
		, m_orientationVSlot( orientationVSlot )
		, m_dplParticleDataSlot( dplParticleDataSlot )
	{}

	Bool IsValid() const { return m_transparencyParamsSlot!= -1 || m_bboxInfoSlot != -1 || m_motionBlurParamsSlot != -1 || m_orientationUSlot != -1 || m_orientationVSlot != -1 || m_dplParticleDataSlot != -1; }

	Int8 m_transparencyParamsSlot;
	Int8 m_bboxInfoSlot;
	Int8 m_motionBlurParamsSlot;
	Int8 m_orientationUSlot;
	Int8 m_orientationVSlot;
	Int8 m_dplParticleDataSlot;
};

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

struct RENDER_DATA_API SMatModifierDataShadowsDebugParams : public SMatModifierDataBase
{
	static const EMaterialModifier ModType = EMATMOD_ShadowsDebugParams;

	static SMatModifierDataShadowsDebugParams Create(const MaterialModifierDesc& modifierDesc)
	{
		return SMatModifierDataShadowsDebugParams(modifierDesc.GetValue(RED_NAME_CONSTEXPR("MatMod_ShadowsFlagsSlot")));
	}

	SMatModifierDataShadowsDebugParams(Int8 shadowsFlagsSlot)
		: m_shadowsFlagsSlot(shadowsFlagsSlot)
	{
	
	}

	Bool IsValid() const { return m_shadowsFlagsSlot != -1; }

	Int8 m_shadowsFlagsSlot;
};

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

struct RENDER_DATA_API SMatModifierDataMultilayeredDebug : public SMatModifierDataBase
{
	static const EMaterialModifier ModType = EMATMOD_MultilayeredDebug;

	static SMatModifierDataMultilayeredDebug Create( const MaterialModifierDesc& modifierDesc )
	{
		SMatModifierDataMultilayeredDebug matmod;
		matmod.m_debugModeSlot = modifierDesc.GetValue( RED_NAME_CONSTEXPR( "MatMod_MLDebug_Mode" ) );
		matmod.m_colorSlot = modifierDesc.GetValue( RED_NAME_CONSTEXPR( "MatMod_MLDebug_Color" ) );
		matmod.m_proxyColorRoughnessSlot = modifierDesc.GetValue( RED_NAME_CONSTEXPR( "MatMod_MLDebug_ProxyColorRoughness" ) );
		matmod.m_proxyNormalMetalnessSlot = modifierDesc.GetValue( RED_NAME_CONSTEXPR( "MatMod_MLDebug_ProxyNormalMetalness" ) );
		return matmod;
	}

	Bool IsValid() const { return m_debugModeSlot != -1; }

	Int8 m_debugModeSlot;
	Int8 m_colorSlot;
	Int8 m_proxyColorRoughnessSlot;
	Int8 m_proxyNormalMetalnessSlot;
};

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

struct RENDER_DATA_API SMatModifierParallaxParams: public SMatModifierDataBase
{
	static const EMaterialModifier ModType = EMATMOD_ParallaxParams;

	static SMatModifierParallaxParams Create( const MaterialModifierDesc& modifierDesc )
	{
		return SMatModifierParallaxParams(
			modifierDesc.GetValue( RED_NAME_CONSTEXPR( "MatMod_ProxyTranslationSlot" ) ),
			modifierDesc.GetValue( RED_NAME_CONSTEXPR( "MatMod_ProxyLocalToWorldSlot" ) ),
			modifierDesc.GetValue( RED_NAME_CONSTEXPR( "MatMod_ProxyWorldToLocalSlot" ) )
		);
	}

	SMatModifierParallaxParams( Int8 proxyTranslationSlot, Int8 localToWorldSlot, Int8 worldToLocalSlot )
		: m_proxyTranslationSlot( proxyTranslationSlot )
		, m_localToWorldSlot( localToWorldSlot )
		, m_worldToLocalSlot( worldToLocalSlot )
	{

	}

	Bool IsValid() const {
		return m_proxyTranslationSlot != -1
			|| m_localToWorldSlot != -1
			|| m_worldToLocalSlot != -1;
	}

	Int8 m_proxyTranslationSlot;
	Int8 m_localToWorldSlot;
	Int8 m_worldToLocalSlot;
};

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

struct RENDER_DATA_API SMatModifierHighlightsParams : public SMatModifierDataBase
{
	static const EMaterialModifier ModType = EMATMOD_HighlightsParams;

	static SMatModifierHighlightsParams Create(const MaterialModifierDesc& modifierDesc)
	{
		return SMatModifierHighlightsParams( modifierDesc.GetValue( RED_NAME_CONSTEXPR( "MatMod_FadeValueSlot" ) ) );
	}

	SMatModifierHighlightsParams( Int8 fadeValueSlot )
		: m_fadeValueSlot(fadeValueSlot )
	{}

	Bool IsValid() const { return m_fadeValueSlot != -1; }

	Int8 m_fadeValueSlot;

};

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

struct RENDER_DATA_API SMatModifierDebugColoringParams : public SMatModifierDataBase
{
	static const EMaterialModifier ModType = EMATMOD_DebugColoring;

	static SMatModifierDebugColoringParams Create( const MaterialModifierDesc& modifierDesc )
	{
		return SMatModifierDebugColoringParams( modifierDesc.GetValue( RED_NAME_CONSTEXPR( "MatMod_DebugColorSlot" ) ) );
	}

	SMatModifierDebugColoringParams( Int8 debugColorSlot )
		: m_debugColorSlot( debugColorSlot )
	{
	}

	RED_FORCE_INLINE Bool IsValid() const { return m_debugColorSlot != -1; }

	Int8 m_debugColorSlot;
};

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

struct RENDER_DATA_API SMatModifierVehicleParams : public SMatModifierDataBase
{
	static const EMaterialModifier ModType = EMATMOD_VehicleParams;

	static SMatModifierVehicleParams Create( const MaterialModifierDesc& modifierDesc )
	{
		return SMatModifierVehicleParams(
			modifierDesc.GetValue( RED_NAME_CONSTEXPR( "MatMod_VehicleMeshPivotInGridSpace" ) ),
			modifierDesc.GetValue( RED_NAME_CONSTEXPR( "MatMod_VehicleGridCorners" ) )
		);
	}

	SMatModifierVehicleParams( Int8 meshPivotInGridSpaceSlot, Int8 gridCornersSlot )
		: m_meshPivotInGridSpaceSlot( meshPivotInGridSpaceSlot )
		, m_gridCornersSlot( gridCornersSlot )
	{
	}

	Bool IsValid() const
	{
		return m_meshPivotInGridSpaceSlot != -1
			   || m_gridCornersSlot != -1;
	}

	Int8 m_meshPivotInGridSpaceSlot;
	Int8 m_gridCornersSlot;
};

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

struct RENDER_DATA_API SMatModifierData_RemoteCamera : public SMatModifierDataBase
{
	static const EMaterialModifier ModType = EMATMOD_RemoteCamera;

	static SMatModifierData_RemoteCamera Create( const MaterialModifierDesc& modifierDesc )
	{
		SMatModifierData_RemoteCamera matmod;
		matmod.m_textureSlot = modifierDesc.GetValue( RED_NAME_CONSTEXPR( "MatMod_RemoteCameraTextureSlot" ) );
		matmod.m_textureIsBoundSlot = modifierDesc.GetValue( RED_NAME_CONSTEXPR( "MatMod_RemoteCameraTextureIsBound" ) );
		matmod.m_texSizeSlot = modifierDesc.GetValue( RED_NAME_CONSTEXPR( "MatMod_RemoteCameraTextureSize" ) );
		return matmod;
	}

	Bool IsValid() const { return m_textureSlot != -1; }

	Int8 m_textureSlot;
	Int8 m_textureIsBoundSlot;
	Int8 m_texSizeSlot;
};

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

struct RENDER_DATA_API SMatModifierData_Mirror : public SMatModifierDataBase
{
	static const EMaterialModifier ModType = EMATMOD_Mirror;

	static SMatModifierData_Mirror Create( const MaterialModifierDesc& modifierDesc )
	{
		SMatModifierData_Mirror matmod;
		matmod.m_textureSlot = modifierDesc.GetValue( RED_NAME_CONSTEXPR( "MatMod_MirrorTextureSlot" ) );
		matmod.m_textureIsBoundSlot = modifierDesc.GetValue( RED_NAME_CONSTEXPR( "MatMod_MirrorTextureIsBound" ) );
		matmod.m_texSizeSlot = modifierDesc.GetValue( RED_NAME_CONSTEXPR( "MatMod_MirrorTextureSize" ) );
		matmod.m_reflectionViewToScreenSlot = modifierDesc.GetValue( RED_NAME_CONSTEXPR( "MatMod_MirrorReflectionViewToScreen" ) );
		return matmod;
	}

	Bool IsValid() const { return m_textureSlot != -1 && m_textureIsBoundSlot != -1 && m_texSizeSlot != -1 && m_reflectionViewToScreenSlot != -1; }

	Int8 m_textureSlot;
	Int8 m_textureIsBoundSlot;
	Int8 m_texSizeSlot;
	Int8 m_reflectionViewToScreenSlot;
};

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

struct RENDER_DATA_API SMatModifierDataCustomStructBuffer : public SMatModifierDataBase
{
	static const EMaterialModifier ModType = EMATMOD_CustomStructBuffer;

	static SMatModifierDataCustomStructBuffer Create( const MaterialModifierDesc& modifierDesc )
	{
		return SMatModifierDataCustomStructBuffer( modifierDesc.GetValue( RED_NAME_CONSTEXPR( "MatMod_CustomStructBufferSlot" ) ) );
	}

	SMatModifierDataCustomStructBuffer( Int8 slot )
		: m_slot( slot )
	{}

	Bool IsValid() const { return m_slot!= -1; }

	Int8 m_slot;
};

struct RENDER_DATA_API SMatModifierDestructionRegions : public SMatModifierDataBase
{
	static const EMaterialModifier ModType = EMATMOD_DestructionRegions;

	static SMatModifierDestructionRegions Create(const MaterialModifierDesc& modifierDesc)
	{
		return SMatModifierDestructionRegions( modifierDesc.GetValue( RED_NAME_CONSTEXPR("MatMod_DestructionRegionsBufferSlot") ), modifierDesc.GetValue( RED_NAME_CONSTEXPR("MatMod_DestructionRegionsCountSlot") ) );
	}

	SMatModifierDestructionRegions(Int8 slot, Int8 count)
		: m_slot(slot)
		, m_count(count)
	{}

	Bool IsValid() const { return m_slot != -1 && m_count != -1; }

	Int8 m_slot;
	Int8 m_count;
};

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

struct RENDER_DATA_API SMatModifierDataMotionMatrixParams : public SMatModifierDataBase
{
	static const EMaterialModifier ModType = EMATMOD_MotionMatrix;

	static SMatModifierDataMotionMatrixParams Create( const MaterialModifierDesc& modifierDesc )
	{
		return SMatModifierDataMotionMatrixParams( modifierDesc.GetValue( RED_NAME_CONSTEXPR("MatMod_MotionMatrix") ) );
	}

	SMatModifierDataMotionMatrixParams( Int8 slot )
		: m_slot( slot )
	{}

	Bool IsValid() const { return m_slot != -1; }

	Int8 m_slot;
};

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

struct RENDER_DATA_API SMatModifierDataMaterialParams : public SMatModifierDataBase
{
	static const EMaterialModifier ModType = EMATMOD_MaterialParams;

	static SMatModifierDataMaterialParams Create(const MaterialModifierDesc& modifierDesc)
	{
		return SMatModifierDataMaterialParams( 
			modifierDesc.GetValue(RED_NAME_CONSTEXPR("MatMod_MaterialParam0IDSlot")), 
			modifierDesc.GetValue(RED_NAME_CONSTEXPR("MatMod_MaterialParam1IDSlot")), 
			modifierDesc.GetValue(RED_NAME_CONSTEXPR("MatMod_MaterialParam2IDSlot")),
			modifierDesc.GetValue(RED_NAME_CONSTEXPR("MatMod_MaterialParam3IDSlot")),
			modifierDesc.GetValue(RED_NAME_CONSTEXPR("MatMod_MaterialParam4IDSlot")),
			modifierDesc.GetValue(RED_NAME_CONSTEXPR("MatMod_MaterialParam5IDSlot")),
			modifierDesc.GetValue(RED_NAME_CONSTEXPR("MatMod_MaterialParam6IDSlot")) );
	}

	SMatModifierDataMaterialParams(Int8 slot0, Int8 slot1, Int8 slot2, Int8 slot3, Int8 slot4, Int8 slot5, Int8 slot6)
	{
		m_slot[0] = slot0;
		m_slot[1] = slot1;
		m_slot[2] = slot2;
		m_slot[3] = slot3;
		m_slot[4] = slot4;
		m_slot[5] = slot5;
		m_slot[6] = slot6;
	}

	Bool IsValid() const { return m_slot[0] != -1 || m_slot[1] != -1 || m_slot[2] != -1 || m_slot[3] != -1 || m_slot[4] != -1 || m_slot[5] != -1 || m_slot[6] != -1; }

	Int8 m_slot[7];
};

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

struct RENDER_DATA_API SMatModifierEyeParams : public SMatModifierDataBase
{
	static const EMaterialModifier ModType = EMATMOD_Eye;

	static SMatModifierEyeParams Create( const MaterialModifierDesc& modifierDesc )
	{
		return SMatModifierEyeParams( modifierDesc.GetValue( RED_NAME_CONSTEXPR("MatMod_EyeMatrix_Left") ), modifierDesc.GetValue( RED_NAME_CONSTEXPR("MatMod_EyeMatrix_Right") ) );
	}

	SMatModifierEyeParams( Int8 slotLeft, Int8 slotRight )
		: m_slotLeft( slotLeft )
		, m_slotRight( slotRight )
	{}

	Bool IsValid() const { return m_slotLeft!= -1 && m_slotRight != -1; }

	Int8 m_slotLeft;
	Int8 m_slotRight;
};

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

struct RENDER_DATA_API SMatModifierSkinParams : public SMatModifierDataBase
{
	static const EMaterialModifier ModType = EMATMOD_Skin;

	static SMatModifierSkinParams Create( const MaterialModifierDesc& modifierDesc )
	{
		Int8 textureRegionsSlot = modifierDesc.GetValue( RED_NAME_CONSTEXPR("MatMod_TextureRegionsCB") );
		Int8 floatTracksSlot = modifierDesc.GetValue( RED_NAME_CONSTEXPR("MatMod_FloatTracksDataCB") );
		Int8 wrinklesSlot = modifierDesc.GetValue( RED_NAME_CONSTEXPR("MatMod_WrinklesParams") );

		return SMatModifierSkinParams( textureRegionsSlot, floatTracksSlot, wrinklesSlot );
	}

	SMatModifierSkinParams( Int8 regionsCBSlot, Int8 floatTracksCBSlot, Int8 wrinklesParamsSlot )
		: m_regionsCBSlot( regionsCBSlot )
		, m_floatTracksCBSlot( floatTracksCBSlot )
		, m_wrinklesParamsSlot( wrinklesParamsSlot )
	{}

	Bool IsValid() const { return m_regionsCBSlot!= -1 && m_floatTracksCBSlot != -1 && m_wrinklesParamsSlot != -1; }

	Int8 m_regionsCBSlot;
	Int8 m_floatTracksCBSlot;
	Int8 m_wrinklesParamsSlot;
};

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
struct RENDER_DATA_API SMatModifierDismembermentParams
{
	static const EMaterialModifier ModType = EMATMOD_Dismemberment;

	static SMatModifierDismembermentParams Create(const MaterialModifierDesc& modifierDesc)
	{
		const Int8 cullObjectCBSlot = modifierDesc.GetValue(RED_NAME_CONSTEXPR("MatMod_CullObjectsCB"));
		const Int8 dismParamsSlot	= modifierDesc.GetValue(RED_NAME_CONSTEXPR("MatMod_DismParams"));

		return SMatModifierDismembermentParams(cullObjectCBSlot, dismParamsSlot );
	}

	SMatModifierDismembermentParams(Int8 cullObjectCBSlot, Int8 dismParamsSlot )
		: m_cullObjectsCBSlot(cullObjectCBSlot)
		, m_dismParamsSlot(dismParamsSlot)
	{}

	Bool IsValid() const { return m_cullObjectsCBSlot != -1 && m_dismParamsSlot != -1; }

	Int8 m_cullObjectsCBSlot;
	Int8 m_dismParamsSlot;
};

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

struct RENDER_DATA_API SMatModifierFloatTracksParams : public SMatModifierDataBase
{
	static const EMaterialModifier ModType = EMATMOD_FloatTracks;

	static SMatModifierFloatTracksParams Create( const MaterialModifierDesc& modifierDesc )
	{
		Int8 floatTracksSlot = modifierDesc.GetValue( RED_NAME_CONSTEXPR( "MatMod_FloatTracksDataCB" ) );

		return SMatModifierFloatTracksParams( floatTracksSlot );
	}

	SMatModifierFloatTracksParams( Int8 floatTracksCBSlot )
		: m_floatTracksCBSlot( floatTracksCBSlot )
	{
	}

	Bool IsValid() const { return m_floatTracksCBSlot != -1; }

	Int8 m_floatTracksCBSlot;
};

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

struct RENDER_DATA_API SMatModifierRainParams : public SMatModifierDataBase
{
	static const EMaterialModifier ModType = EMATMOD_Rain;

	static SMatModifierRainParams Create( const MaterialModifierDesc& modifierDesc )
	{
		Int8 rainSlot = modifierDesc.GetValue( RED_NAME_CONSTEXPR( "MatMod_RainParams" ) );

		return SMatModifierRainParams( rainSlot );
	}

	SMatModifierRainParams( Int8 rainCBSlot )
		: m_rainCBSlot( rainCBSlot )
	{
	}

	Bool IsValid() const { return m_rainCBSlot != -1;	}

	Int8 m_rainCBSlot;
};

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

struct RENDER_DATA_API SMatModifierPlanarReflectionsParams : public SMatModifierDataBase
{
	static const EMaterialModifier ModType = EMATMOD_PlanarReflections;

	static SMatModifierPlanarReflectionsParams Create( const MaterialModifierDesc& modifierDesc )
	{
		SMatModifierPlanarReflectionsParams ret;

		ret.m_resultTexSlot	= modifierDesc.GetValue( RED_NAME_CONSTEXPR( "MatMod_PlanarSSR_Result_TextureSlot" ) );
		ret.m_fadeTexSlot	= modifierDesc.GetValue( RED_NAME_CONSTEXPR( "MatMod_PlanarSSR_Fade_TextureSlot" ) );

		return ret;
	}

	Bool IsValid() const { return true;	}

	Int8 m_resultTexSlot;
	Int8 m_fadeTexSlot;
};

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

struct RENDER_DATA_API SMaterialModifierBufferHeader
{
	RED_USE_MEMORY_POOL( rend::PoolMaterialModifier );

	Uint32 m_numModifiers;
	MaterialModifierMask m_modifiersMask;
};

struct RENDER_DATA_API SMaterialModifierEntryHeader
{
	RED_USE_MEMORY_POOL( rend::PoolMaterialModifier );

	Uint32 m_type;
	Uint32 m_size;
};

struct RENDER_DATA_API SMatModifierBuilder
{
	RED_USE_MEMORY_POOL( rend::PoolMaterialModifier );

	SMatModifierBuilder()
	{}

	template< typename Mod >
	SMatModifierBuilder& Add( const Mod &mod )
	{
		return AddImpl( Mod::ModType, sizeof(mod), &mod );
	}

	SMatModifierBuilder& AddImpl( EMaterialModifier modType, Uint32 modDataSize, const void *modData );
	RED_FORCE_INLINE void Clear() { m_data.Clear(); }

	red::DynArray< Uint8 > m_data{ rend::PoolMaterialModifier() };
};

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

struct RENDER_DATA_API SMaterialModifierSpanBuffer
{
	RED_USE_MEMORY_POOL( rend::PoolMaterialModifier );

	SMaterialModifierSpanBuffer ()
		: m_bufferSize( 0 )
		, m_bufferData( nullptr )
	{}

	SMaterialModifierSpanBuffer ( Uint32 bufferSize, const void *bufferData )
	{
		Init( bufferSize, bufferData );
	}

	void Init( Uint32 bufferSize, const void *bufferData )
	{
		m_bufferSize = bufferSize;
		m_bufferData = bufferData;
	}

	void Reset()
	{
		Init( 0, nullptr );
	}

	Bool IsEmpty() const
	{
		RED_FATAL_ASSERT( 0 == m_bufferSize || m_bufferSize >= sizeof(SMaterialModifierBufferHeader), "" );
		return 0 == m_bufferSize;
	}

	const void *m_bufferData;
	Uint32 m_bufferSize;
};

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

RENDER_DATA_API RED_INLINE const SMaterialModifierBufferHeader* DecodeMaterialModifierBufferHeader( Uint32 matModBufferSize, const void *matModBufferData )
{
	if ( 0 == matModBufferSize )
		return nullptr;
	RED_FATAL_ASSERT( matModBufferSize >= sizeof(SMaterialModifierBufferHeader), "Invalid matMod data" );
	return static_cast<const SMaterialModifierBufferHeader*>(matModBufferData);
}

RENDER_DATA_API RED_INLINE const SMaterialModifierBufferHeader* DecodeMaterialModifierBufferHeader( const SMaterialModifierSpanBuffer &spanBuff )
{
	return DecodeMaterialModifierBufferHeader( spanBuff.m_bufferSize, spanBuff.m_bufferData );
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

struct RENDER_DATA_API MaterialModifierBufferIterator
{
	RED_USE_MEMORY_POOL( rend::PoolMaterialModifier );

public:
	MaterialModifierBufferIterator ()
		: m_numModLeft( 0 )
		, m_currMod( nullptr )
	{}

	MaterialModifierBufferIterator ( Uint32 matModBufferSize, const void *matModBufferData )
	{
		Init( matModBufferSize, matModBufferData );
	}

	MaterialModifierBufferIterator ( const SMaterialModifierSpanBuffer &spanBuffer )
	{
		Init( spanBuffer );
	}

	void Init( Uint32 matModBufferSize, const void *matModBufferData )
	{
		const SMaterialModifierBufferHeader *header = DecodeMaterialModifierBufferHeader( matModBufferSize, matModBufferData );		
		if ( header  )
		{
			m_numModLeft = header->m_numModifiers;
			m_currMod = static_cast<const Uint8*>(matModBufferData) + sizeof(SMaterialModifierBufferHeader);
		}
		else
		{
			m_numModLeft = 0;
			m_currMod = nullptr;
		}
	}

	void Init( const SMaterialModifierSpanBuffer &spanBuffer )
	{
		Init( spanBuffer.m_bufferSize, spanBuffer.m_bufferData );
	}

	RED_FORCE_INLINE Bool IsValid() const
	{
		return m_numModLeft > 0;
	}

	RED_FORCE_INLINE EMaterialModifier GetType() const
	{
		RED_FATAL_ASSERT( IsValid(), "Invalid iterator" );
		const SMaterialModifierEntryHeader *header = static_cast<const SMaterialModifierEntryHeader*>( m_currMod );
		RED_FATAL_ASSERT( header->m_type < EMATMOD_MAX, "" );
		return (EMaterialModifier)header->m_type;
	}

	RED_FORCE_INLINE const SMaterialModifierEntryHeader* GetHeader() const
	{
		RED_FATAL_ASSERT( IsValid(), "Invalid iterator" );
		const SMaterialModifierEntryHeader *header = static_cast<const SMaterialModifierEntryHeader*>(m_currMod);
		return header;
	}

	RED_FORCE_INLINE const void* GetData() const
	{
		RED_FATAL_ASSERT( IsValid(), "Invalid iterator" );
		return static_cast<const Uint8*>(m_currMod) + sizeof(SMaterialModifierEntryHeader);
	}

	RED_FORCE_INLINE void Next()
	{
		RED_FATAL_ASSERT( IsValid(), "Invalid iterator" );
		const SMaterialModifierEntryHeader *header = static_cast<const SMaterialModifierEntryHeader*>( m_currMod );
		RED_FATAL_ASSERT( header->m_type < EMATMOD_MAX, "" );
		m_currMod = static_cast<const Uint8*>(m_currMod) + sizeof(SMaterialModifierEntryHeader) + header->m_size;
		--m_numModLeft;
	}

private:
	Uint32 m_numModLeft;
	const void *m_currMod;
};

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
