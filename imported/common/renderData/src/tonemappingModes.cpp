/**
* Copyright (c) 2017 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"
#include "tonemappingModes.h"

RTTI_BEGIN_ABSTRACT_TYPE(ITonemappingMode);
	RTTI_PARENT_TYPE(ISerializable);
	RTTI_PROPERTY( m_colorPreservation ).editable().inlined();
RTTI_END_TYPE();

RTTI_BEGIN_TYPE(TonemappingModeLinear);
	RTTI_PARENT_TYPE(ITonemappingMode);
RTTI_END_TYPE();

RTTI_BEGIN_TYPE( TonemappingModeLottes );
	RTTI_PARENT_TYPE( ITonemappingMode );
	RTTI_PROPERTY( m_maxInput ).editable().range( 1.0f, 500.0f );
	RTTI_PROPERTY( m_contrast ).editable().range( 0.1f, 10.0f );
	RTTI_PROPERTY( m_midIn ).editable().range( 0.0f, 1.0f );
	RTTI_PROPERTY( m_midOut ).editable().range( 0.0f, 1.0f );
	RTTI_PROPERTY( m_crosstalk ).editable().range( 0.0f, 20.0f );
	RTTI_PROPERTY( m_crosstalkSaturation ).editable().range( 0.0f, 20.0f );
RTTI_END_TYPE();

RTTI_BEGIN_TYPE( TonemappingModeLottesACES );
	RTTI_PARENT_TYPE( ITonemappingMode );
	RTTI_PROPERTY( m_maxInput ).editable().range( 1.0f, 500.0f );
	RTTI_PROPERTY( m_contrast ).editable().range( 0.1f, 10.0f );
	RTTI_PROPERTY( m_midIn ).editable().range( 0.0f, 1.0f );
	RTTI_PROPERTY( m_midOut ).editable().range( 0.0f, 1.0f );
RTTI_END_TYPE();

RTTI_BEGIN_TYPE(TonemappingModeACESApprox);
	RTTI_PARENT_TYPE(ITonemappingMode);
RTTI_END_TYPE();

RTTI_BEGIN_TYPE(TonemappingModeACES);
	RTTI_PARENT_TYPE(ITonemappingMode);
	RTTI_PROPERTY(m_params).editable();
RTTI_END_TYPE();

RTTI_BEGIN_TYPE(STonemappingACESParams);
	RTTI_PROPERTY(m_minStops).editable().range(-24.0f, 0.0f);
	RTTI_PROPERTY(m_maxStops).editable().range(0.0f, 24.0f);
	RTTI_PROPERTY(m_midGrayScale).editable().range(0.01f, 100.0f);
	RTTI_PROPERTY(m_surroundGamma).editable().range(0.1f, 10.2f);
	RTTI_PROPERTY(m_toneCurveSaturation).editable().range(0.0f, 5.0f);
	RTTI_PROPERTY(m_adjustWhitePoint).editable();
	RTTI_PROPERTY(m_desaturate).editable();
	RTTI_PROPERTY(m_dimSurround).editable();
	RTTI_PROPERTY(m_tonemapLuminance).editable();
	RTTI_PROPERTY(m_applyAfterLUT).editable();
RTTI_END_TYPE();

RTTI_BEGIN_TYPE( STonemappingACESParamsSDR );
	RTTI_PARENT_TYPE( STonemappingACESParams );
RTTI_END_TYPE();

RTTI_BEGIN_TYPE( STonemappingACESParamsHDR );
	RTTI_PARENT_TYPE( STonemappingACESParams );
RTTI_END_TYPE();

Bool TonemappingModeACES::OnPropertyMissing( CName propertyName, const rtti::Variant& readValue )
{
	//if ( propertyName == RED_NAME_CONSTEXPR_NOREG( "LUT" ) )
	//{
	//	readValue.Get< TResRef< CBitmapTexture > >( m_ldrLut.m_LUT );
	//	return true;
	//}
	return ITonemappingMode::OnPropertyMissing( propertyName, readValue );
}
