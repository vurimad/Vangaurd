/**
* Copyright (c) 2007 CD Projekt Red. All Rights Reserved.
*/
#pragma once

//******************************************************************************************************//
//******************************************************************************************************//

struct RENDER_DATA_API ShadowCascadeConfig
{
	RTTI_DECLARE_TYPE( ShadowCascadeConfig );

	Float			m_range;
	Float			m_filterSize;
	Float			m_blendRange;
	Float			m_biasOffset;
};

//******************************************************************************************************//
//******************************************************************************************************//

struct RENDER_DATA_API ContactShadowsConfig
{
	RTTI_DECLARE_TYPE( ContactShadowsConfig );

	Float			m_range;
	Float			m_rangeLimit;

	Float			m_screenEdgeFadeRange;

	Float			m_distanceFadeLimit;
	Float			m_distanceFadeRange;

	ContactShadowsConfig();

	// Operators for Lerp-ing
	ContactShadowsConfig operator*( const Float t ) const;
	ContactShadowsConfig operator+( const ContactShadowsConfig& t ) const;
};

//******************************************************************************************************//
//******************************************************************************************************//

struct RENDER_DATA_API FoliageShadowConfig
{
	RTTI_DECLARE_TYPE( FoliageShadowConfig );

	Float			m_foliageShadowCascadeGradient;
	Float			m_foliageShadowCascadeFilterScale;
	Float			m_foliageShadowCascadeGradientDistanceRange;

	FoliageShadowConfig();
};

//******************************************************************************************************//
//******************************************************************************************************//

// Global world shadow config ( for cascades )
class RENDER_DATA_API WorldShadowConfig
{
	RTTI_DECLARE_TYPE( WorldShadowConfig );

public:
	ContactShadowsConfig	m_contactShadows;

	Uint32					m_distantShadowsNumLevels;					//!< Number of levels in distant shadows
	Float					m_distantShadowsBaseLevelRadius;			//!< Size of closest distant shadows level

	FoliageShadowConfig		m_foliageShadowConfig;

public:
	WorldShadowConfig();
};

//******************************************************************************************************//
//******************************************************************************************************//

class RENDER_DATA_API WorldLightingConfig
{
	RTTI_DECLARE_TYPE( WorldLightingConfig );

public:
	Float					m_lightAttenuationClamp;					//!< Sets max light brightness when used with per-light clamp toggle

public:
	WorldLightingConfig();

};

//******************************************************************************************************//
//******************************************************************************************************//
