/**
* Copyright (c) 20019 CD Projekt Red. All Rights Reserved.
*/
#pragma once

struct RENDER_DATA_API SWeaponPlaneParams
{
	RTTI_DECLARE_TYPE( SWeaponPlaneParams )

	SWeaponPlaneParams operator*( Float f )
	{
		SWeaponPlaneParams obj;
		obj.m_weaponNearPlaneCM = m_weaponNearPlaneCM * f;
		obj.m_weaponFarPlaneCM = m_weaponFarPlaneCM * f;
		obj.m_weaponEdgesSharpness = m_weaponEdgesSharpness * f;
		obj.m_vignetteIntensity = m_vignetteIntensity * f;
		obj.m_vignetteRadius = m_vignetteRadius * f;
		obj.m_vignetteCircular = m_vignetteCircular * f;
		obj.m_blurIntensity = m_blurIntensity * f;
		return obj;
	}

	SWeaponPlaneParams& operator+=( const SWeaponPlaneParams& p )
	{
		m_weaponNearPlaneCM += p.m_weaponNearPlaneCM;
		m_weaponFarPlaneCM += p.m_weaponFarPlaneCM;
		m_weaponEdgesSharpness += p.m_weaponEdgesSharpness;
		m_vignetteIntensity += p.m_vignetteIntensity;
		m_vignetteRadius += p.m_vignetteRadius;
		m_vignetteCircular += p.m_vignetteCircular;
		m_blurIntensity += p.m_blurIntensity;
		return *this;
	}

	SWeaponPlaneParams operator+( const SWeaponPlaneParams& p )
	{
		SWeaponPlaneParams obj;
		obj.m_weaponNearPlaneCM = m_weaponNearPlaneCM + p.m_weaponNearPlaneCM;
		obj.m_weaponFarPlaneCM = m_weaponFarPlaneCM + p.m_weaponFarPlaneCM;
		obj.m_weaponEdgesSharpness = m_weaponEdgesSharpness + p.m_weaponEdgesSharpness;
		obj.m_vignetteIntensity = m_vignetteIntensity + p.m_vignetteIntensity;
		obj.m_vignetteRadius = m_vignetteRadius + p.m_vignetteRadius;
		obj.m_vignetteCircular = m_vignetteCircular + p.m_vignetteCircular;
		obj.m_blurIntensity = m_blurIntensity + p.m_blurIntensity;
		return obj;
	}

	Bool operator==( const SWeaponPlaneParams& p ) const
	{
		return ( m_weaponNearPlaneCM == p.m_weaponNearPlaneCM &&
				 m_weaponFarPlaneCM == p.m_weaponFarPlaneCM &&
				 m_weaponEdgesSharpness == p.m_weaponEdgesSharpness &&
				 m_vignetteIntensity == p.m_vignetteIntensity &&
				 m_vignetteRadius == p.m_vignetteRadius &&
				 m_vignetteCircular == p.m_vignetteCircular &&
				 m_blurIntensity == p.m_blurIntensity );
	}

	Float	m_weaponNearPlaneCM = 0.0f;
	Float	m_weaponFarPlaneCM = 0.0f;
	Float	m_weaponEdgesSharpness = 0.0f;
	Float	m_vignetteIntensity = 0.0f;
	Float	m_vignetteRadius = 0.0f;
	Float	m_vignetteCircular = 0.0f;
	Float	m_blurIntensity = 0.0f;
};