/**
* Copyright (c) 2017 CD Projekt Red. All Rights Reserved.
*/

#pragma once

#include "renderPublicEnums.h"
#include "../../redReflection/include/singleChannelCurve.h"

class RENDER_DATA_API ITonemappingMode : public ISerializable
{
	RED_USE_MEMORY_POOL( rend::PoolEnvironment );
	RTTI_DECLARE_TYPE(ITonemappingMode);

public:
	TSingleChannelCurve< Float > m_colorPreservation;
	TSingleChannelCurve< Float > m_keyValue;
	TSingleChannelCurve< Float > m_exposureBiasEV;

public:
	ITonemappingMode()
	{
		{
			TSingleChannelCurveBuilder< Float > builder(m_colorPreservation);
			builder.Resize(1);
			builder.SetKeyData(0, 12.0f, 0.0f);
		}

		{
			TSingleChannelCurveBuilder< Float > builder(m_keyValue);
			builder.Resize(1);
			builder.SetKeyData(0, 12.0f, 1.0f);
		}

		{
			TSingleChannelCurveBuilder< Float > builder(m_exposureBiasEV);
			builder.Resize(1);
			builder.SetKeyData(0, 12.0f, 0.0f);
		}
	}

	ITonemappingMode( ISerializable* parent )
		: ITonemappingMode()
	{
		SetParent( parent );
	}
};

//////////////////////////////////////////////////////////////////////////
// Tonemapping mode - Linear - bypass tonemapper
//////////////////////////////////////////////////////////////////////////
class RENDER_DATA_API TonemappingModeLinear : public ITonemappingMode
{
	RTTI_DECLARE_TYPE(TonemappingModeLinear);

public:
	TonemappingModeLinear()
		: ITonemappingMode(nullptr)
	{
	}

	TonemappingModeLinear( ISerializable* parent )
		: ITonemappingMode(parent)
	{
	}
};

class RENDER_DATA_API TonemappingModeACESApprox : public ITonemappingMode
{
	RTTI_DECLARE_TYPE(TonemappingModeACESApprox);

public:
	TonemappingModeACESApprox()
		: ITonemappingMode(nullptr)
	{
		TSingleChannelCurveBuilder< Float > builder(m_exposureBiasEV);
		builder.Resize(1);
		builder.SetKeyData(0, 12.0f, 1.0f);
	}

	TonemappingModeACESApprox( ISerializable* parent )
		: ITonemappingMode(parent)
	{
		TSingleChannelCurveBuilder< Float > builder(m_exposureBiasEV);
		builder.Resize(1);
		builder.SetKeyData(0, 12.0f, 1.0f);
	}
};

//////////////////////////////////////////////////////////////////////////////////////////////////
// Tonemapping mode - Timothy Lottes (AMD)
//////////////////////////////////////////////////////////////////////////////////////////////////

class RENDER_DATA_API TonemappingModeLottes : public ITonemappingMode
{
	RTTI_DECLARE_TYPE( TonemappingModeLottes );

public:
	TonemappingModeLottes() : ITonemappingMode( nullptr ) { }

	TonemappingModeLottes( ISerializable* parent ) : ITonemappingMode( parent ) { }

	Float m_maxInput = 50.0;
	Float m_contrast = 1.5f;
	Float m_midIn = 0.18f;
	Float m_midOut = 0.18f;
	Vector3 m_crosstalk = Vector3( 2.0f, 2.0f, 2.0f );
	Vector3 m_crosstalkSaturation = Vector3( 1.0f, 2.0f, 20.0f );
};

class RENDER_DATA_API TonemappingModeLottesACES : public ITonemappingMode
{
	RTTI_DECLARE_TYPE( TonemappingModeLottesACES );

public:
	TonemappingModeLottesACES() : ITonemappingMode( nullptr ) {}

	TonemappingModeLottesACES( ISerializable* parent ) : ITonemappingMode( parent ) {}

	Float m_maxInput = 50.0;
	Float m_contrast = 1.5f;
	Float m_midIn = 0.18f;
	Float m_midOut = 0.18f;
};


//////////////////////////////////////////////////////////////////////////////////////////////////
// Tonemapping mode - Filmic ACES
//////////////////////////////////////////////////////////////////////////////////////////////////

struct STonemappingACESParams
{
	RTTI_DECLARE_POLYMORPHIC_TYPE( STonemappingACESParams );

	Float						m_minStops;
	Float						m_maxStops;
	Float						m_midGrayScale;			// Middle Gray Scale
	Float						m_surroundGamma;		// Surround Gamma
	Float						m_toneCurveSaturation;	// Saturation level
	Bool						m_adjustWhitePoint;		// CAT D60 to D65
	Bool						m_desaturate;			// Desaturate
	Bool						m_dimSurround;			// Alter Surround
	Bool						m_tonemapLuminance;		// Tonemap Luminance
	Bool						m_applyAfterLUT;		// when true, tonemapping will be applied after LUT

	STonemappingACESParams()
	{
		m_minStops = -7.0f;
		m_maxStops = 9.0f;
		m_midGrayScale = 1.0f;
		m_surroundGamma = 1.0f;
		m_toneCurveSaturation = 1.0f;
		m_adjustWhitePoint = true;
		m_desaturate = false;
		m_dimSurround = true;
		m_tonemapLuminance = false;
		m_applyAfterLUT = false;
	}

	STonemappingACESParams( Float minStops, Float maxStops, Float midGrayScale, Float maxLevel, Float surroundGamma, Float toneCurveSaturation, Bool adjustWhitePoint, Bool desaturate, Bool dimSurround, Bool tonemapLuminance )
		: m_minStops(minStops)
		, m_maxStops(maxStops)
		, m_midGrayScale(midGrayScale)
		, m_surroundGamma(surroundGamma)
		, m_toneCurveSaturation(toneCurveSaturation)
		, m_adjustWhitePoint(adjustWhitePoint)
		, m_desaturate(desaturate)
		, m_dimSurround(dimSurround)
		, m_tonemapLuminance(tonemapLuminance)
		, m_applyAfterLUT( false )
	{}
};

// TODO not used, remove after resave
struct STonemappingACESParamsSDR : public STonemappingACESParams
{
	RTTI_DECLARE_TYPE( STonemappingACESParamsSDR );
};

// TODO not used, remove after resave
struct STonemappingACESParamsHDR : public STonemappingACESParams
{
	RTTI_DECLARE_TYPE( STonemappingACESParamsHDR );
};

class RENDER_DATA_API TonemappingModeACES : public ITonemappingMode
{
	RTTI_DECLARE_TYPE(TonemappingModeACES);

public:
	TonemappingModeACES()
		: ITonemappingMode(nullptr)
	{
		TSingleChannelCurveBuilder< Float > builder(m_exposureBiasEV);
		builder.Resize(1);
		builder.SetKeyData(0, 12.0f, 1.0f);
	}

	TonemappingModeACES( ISerializable* parent )
		: ITonemappingMode( parent )
	{
		TSingleChannelCurveBuilder< Float > builder(m_exposureBiasEV);
		builder.Resize(1);
		builder.SetKeyData(0, 12.0f, 1.0f);
	}

	// ISerializable
	virtual Bool OnPropertyMissing( CName propertyName, const rtti::Variant& readValue ) override;

	STonemappingACESParams m_params;
};