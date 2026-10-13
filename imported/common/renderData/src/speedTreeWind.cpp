/**
* Copyright (c) 2016 CD Projekt Red. All Rights Reserved.
*/

///////////////////////////////////////////////////////////////////////  
//
//	*** INTERACTIVE DATA VISUALIZATION (IDV) CONFIDENTIAL AND PROPRIETARY INFORMATION ***
//
//	This software is supplied under the terms of a license agreement or
//	nondisclosure agreement with Interactive Data Visualization, Inc. and
//  may not be copied, disclosed, or exploited except in accordance with 
//  the terms of that agreement.
//
//      Copyright (c) 2003-2014 IDV, Inc.
//      All rights reserved in all media.
//
//      IDV, Inc.
//      http://www.idvinc.com
//
///////////////////////////////////////////////////////////////////////  

#include "build.h"
#include "speedTreeWind.h"

///////////////////////////////////////////////////////////////////////  

CSpeedTreeWind::CSpeedTreeWind()
	: m_cWindMgr(SpeedTree::ST_WIND_MODE_GAMES_8)
{
}

CSpeedTreeWind::~CSpeedTreeWind()
{

}

void CSpeedTreeWind::SetParams(const SpeedTree::SConfigGames8& sParams)
{
	m_cWindMgr.Configure(sParams);
	m_sV8Params = sParams;
}

CSpeedTreeWind& CSpeedTreeWind::operator=( const CSpeedTreeWind& obj )
{
	if ( this != &obj )
	{
		m_cWindMgr = obj.m_cWindMgr;
		m_sV8Params = obj.m_sV8Params;
	}
	return *this;
}

///////////////////////////////////////////////////////////////////////
//  Serialize
///////////////////////////////////////////////////////////////////////

IFile& SerializeWindParams( class IFile& file, CSpeedTreeWind& wind )
{
	Uint32 currVersion = CSpeedTreeWind::CURRENT_VERSION;
	file << currVersion;

	//if file is reader, check here the loaded currVersion and handle it properly
	static_assert( CSpeedTreeWind::CURRENT_VERSION == 1u, "Wind Params version changed! Handle here the old version properly!" );
	if( file.IsReader() )
	{
		RED_ASSERT( currVersion > 0 && currVersion <= CSpeedTreeWind::CURRENT_VERSION, "SpeedTree wind params unsupported version. Expected version is: %d, but got: %d in %hs",
					CSpeedTreeWind::CURRENT_VERSION, currVersion, file.GetFileNameForDebug() );
	}

	SpeedTree::SConfigGames8 params = wind.m_sV8Params;

#define SERIALIZE_CURVE(name) for ( Uint32 curve = 0; curve < SpeedTree::SConfigGames8::c_nNumPointsInCurves; ++curve ) { file << params.name[curve]; }
	file << params.m_fGlobalHeight;
	file << params.m_fFullBranchMove;
	file << params.m_fFullRipple;
	file << params.m_vBoundingBoxMin.x;
	file << params.m_vBoundingBoxMin.y;
	file << params.m_vBoundingBoxMin.z;
	file << params.m_vBoundingBoxMax.x;
	file << params.m_vBoundingBoxMax.y;
	file << params.m_vBoundingBoxMax.z;
	file << params.m_fBranchStiffnessAmount;
	file << params.m_fBranchStiffnessUp;
	file << params.m_fBranchStretch;
	file << params.m_fShimmer;

	//common
	{
		file << params.m_fStrengthResponse;
		file << params.m_fDirectionResponse;
		file << params.m_fGustFrequency;
		file << params.m_fGustStrengthMin;
		file << params.m_fGustStrengthMax;
		file << params.m_fGustDurationMin;
		file << params.m_fGustDurationMax;
		file << params.m_fGustRiseScalar;
		file << params.m_fGustFallScalar;
	}

	SERIALIZE_CURVE( m_sShared.m_afFrequency );
	SERIALIZE_CURVE( m_sShared.m_afBend );
	SERIALIZE_CURVE( m_sShared.m_afTurbulence );
	SERIALIZE_CURVE( m_sShared.m_afFlexibility );
	file << params.m_sShared.m_fTurbulenceIndependence;

	SERIALIZE_CURVE( m_sBranch.m_afFrequency );
	SERIALIZE_CURVE( m_sBranch.m_afBend );
	SERIALIZE_CURVE( m_sBranch.m_afTurbulence );
	SERIALIZE_CURVE( m_sBranch.m_afFlexibility );
	file << params.m_sBranch.m_fTurbulenceIndependence;

	SERIALIZE_CURVE( m_sRipple.m_afBend );
	SERIALIZE_CURVE( m_sRipple.m_afTurbulenceFrequency );
	SERIALIZE_CURVE( m_sRipple.m_afTurbulence );
	file << params.m_sRipple.m_fTurbulenceIndependence;

	file << params.m_fIsGrass;

	if ( file.IsReader() )
	{
		wind.SetParams( params );
	}


#undef SERIALIZE_CURVE

	return file;
}