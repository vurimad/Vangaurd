/**
* Copyright (c) 2010 CD Projekt Red. All Rights Reserved.
*/
#pragma once

// --------------------------------------------------------------------------

class RENDER_DATA_API CEnvDisplaySettingsParams
{
	RTTI_DECLARE_TYPE( CEnvDisplaySettingsParams );

public:
	Float	m_gamma;
	Bool	m_enableInstantAdaptation;
	Bool	m_enableGlobalLightingTrajectory;
	Bool	m_enableEnvProbeInstantUpdate;
	Bool	m_allowEnvProbeUpdate;
	Bool	m_allowBloom;
	Bool	m_allowColorMod;
	Bool	m_allowAntialiasing;
	Bool	m_allowGlobalFog;
	Bool	m_allowDOF;
	Bool	m_allowSSAO;
	Bool	m_allowCloudsShadow;
	Bool	m_disableTonemapping;
	Bool	m_forceCutsceneDofMode;
	Bool	m_allowWaterShader;
	EEnvManagerModifier	m_displayMode;

public:
	CEnvDisplaySettingsParams ();

	Bool IsLightMeterMode() const;
	Bool HasLightingPass() const;
	Bool NeedShadows() const;
	Bool HasDetailedView() const;
	Bool IsMotionVectorsView() const;
};

// --------------------------------------------------------------------------
