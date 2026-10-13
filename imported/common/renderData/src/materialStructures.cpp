/**
* Copyright (c) 2007-2016 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"
#include "materialStructures.h"

RTTI_BEGIN_ENUM( ETextureFilteringMin );
	RTTI_ENUM_OPTION( TFMin_Point );
	RTTI_ENUM_OPTION( TFMin_Linear );
	RTTI_ENUM_OPTION( TFMin_Anisotropic );
	RTTI_ENUM_OPTION( TFMin_AnisotropicLow );
RTTI_END_ENUM();

RTTI_BEGIN_ENUM( ETextureFilteringMag );
	RTTI_ENUM_OPTION( TFMag_Point );
	RTTI_ENUM_OPTION( TFMag_Linear );
RTTI_END_ENUM();

RTTI_BEGIN_ENUM( ETextureFilteringMip );
	RTTI_ENUM_OPTION( TFMip_None );
	RTTI_ENUM_OPTION( TFMip_Point );
	RTTI_ENUM_OPTION( TFMip_Linear );
RTTI_END_ENUM();

RTTI_BEGIN_ENUM( ETextureAddressing );
	RTTI_ENUM_OPTION( TA_Wrap );
	RTTI_ENUM_OPTION( TA_Mirror );
	RTTI_ENUM_OPTION( TA_Clamp );
	RTTI_ENUM_OPTION( TA_MirrorOnce );
	RTTI_ENUM_OPTION( TA_Border );
RTTI_END_ENUM();

RTTI_BEGIN_ENUM( ETextureComparisonFunction );
	RTTI_ENUM_OPTION( TCF_None );
	RTTI_ENUM_OPTION( TCF_Less );
	RTTI_ENUM_OPTION( TCF_Equal );
	RTTI_ENUM_OPTION( TCF_LessEqual );
	RTTI_ENUM_OPTION( TCF_Greater );
	RTTI_ENUM_OPTION( TCF_NotEqual );
	RTTI_ENUM_OPTION( TCF_GreaterEqual );
	RTTI_ENUM_OPTION( TCF_Always );
RTTI_END_ENUM();

RTTI_BEGIN_ENUM( EMaterialShaderTarget );
	RTTI_ENUM_OPTION( MSH_Invalid );
	RTTI_ENUM_OPTION( MSH_VertexShader );
	RTTI_ENUM_OPTION( MSH_PixelShader );
	RTTI_ENUM_OPTION( MSH_MAX );
RTTI_END_ENUM();

RTTI_BEGIN_ENUM( EMaterialPriority );
	RTTI_ENUM_OPTION( EMP_Normal );
	RTTI_ENUM_OPTION( EMP_Front );
RTTI_END_ENUM();

RTTI_BEGIN_TYPE( SamplerStateInfo );
	RTTI_PROPERTY( m_filteringMin ).readOnly();
	RTTI_PROPERTY( m_filteringMag ).readOnly();
	RTTI_PROPERTY( m_filteringMip ).readOnly();
	RTTI_PROPERTY( m_addressU ).readOnly();
	RTTI_PROPERTY( m_addressV ).readOnly();
	RTTI_PROPERTY( m_addressW ).readOnly();
	RTTI_PROPERTY( m_comparisonFunc ).readOnly();
	RTTI_PROPERTY( m_register ).readOnly();
RTTI_END_TYPE();

RTTI_BEGIN_TYPE( MaterialUsedParameter );
	RTTI_PROPERTY( m_name ).readOnly();
	RTTI_PROPERTY( m_register ).readOnly();
RTTI_END_TYPE();
