#pragma once
#include "../../redReflection/include/enumBuilder.h"
#include "../../redReflection/include/rttiClassBuilder.h"


//////////////////////////////////////////////////////////////////////////

enum EApertureValue
{
	APERTURE_1_0 = 0,
	APERTURE_1_4,
	APERTURE_2_0,
	APERTURE_2_8,
	APERTURE_4_0,
	APERTURE_5_6,
	APERTURE_8_0,
	APERTURE_11_0,
	APERTURE_16_0,
	APERTURE_22_0,
	APERTURE_32_0
};

RTTI_DECLARE_ENUM( EApertureValue );

//////////////////////////////////////////////////////////////////////////

struct SDofParams
{
	SDofParams ()
	{
		Reset();
	}

	void Reset()
	{
		overrideFactor		= 0.f;
		dofIntensity		= 1.f;
		dofBlurDistNear		= 0.f;
		dofFocusDistNear	= 5.f;
		dofFocusDistFar		= 10.f;
		dofBlurDistFar		= 20.f;
	}
	Float overrideFactor;
	Float dofIntensity;
	Float dofBlurDistNear;
	Float dofFocusDistNear;
	Float dofFocusDistFar;
	Float dofBlurDistFar;
};

//////////////////////////////////////////////////////////////////////////

struct SBokehDofParams
{
	RTTI_DECLARE_TYPE( SBokehDofParams );


	Float					m_planeInFocus;
	EApertureValue			m_fStops;
	Float					m_bokehSizeMuliplier;
	Float					m_hexToCircleScale;
	Bool					m_enabled;
	Bool					m_usePhysicalSetup;


	SBokehDofParams ()
	{
		Reset();
	}

	void Reset()
	{
		m_enabled = false;
		m_usePhysicalSetup = false;
		m_planeInFocus = 3.0f;
		m_fStops = APERTURE_4_0;
		m_bokehSizeMuliplier = 1.0f;
		m_hexToCircleScale = 1.0f;
	}

	Float PupilRelativeSize() const
	{
		switch ( m_fStops )
		{
		case APERTURE_1_0  : return 1.0f;
		case APERTURE_1_4  : return 0.7142f;
		case APERTURE_2_0  : return 0.5f;
		case APERTURE_2_8  : return 0.3571f;
		case APERTURE_4_0  : return 0.25f;
		case APERTURE_5_6  : return 0.1785f;
		case APERTURE_8_0  : return	0.125f;
		case APERTURE_11_0 : return	0.0909f;
		case APERTURE_16_0 : return	0.0625f;
		case APERTURE_22_0 : return	0.0454f;
		case APERTURE_32_0 : return 0.0312f;
		default: return 1.0f;
		}
	}

	const Float PupilSize() const
	{
		return 2.0f * m_bokehSizeMuliplier * PupilRelativeSize();
	}
};

//////////////////////////////////////////////////////////////////////////
