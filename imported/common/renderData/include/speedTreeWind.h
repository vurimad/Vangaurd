/**
* Copyright (c) 2016 CD Projekt Red. All Rights Reserved.
*/

///////////////////////////////////////////////////////////////////////  
//
//	*** INTERACTIVE DATA VISUALIZATION (IDV) PROPRIETARY INFORMATION ***
//
//	This software is supplied under the terms of a license agreement or
//	nondisclosure agreement with Interactive Data Visualization and may
//	not be copied or disclosed except in accordance with the terms of
//	that agreement.
//
//      Copyright (c) 2003-2014 IDV, Inc.
//      All Rights Reserved.
//
//		IDV, Inc.
//		Web: http://www.idvinc.com
//
///////////////////////////////////////////////////////////////////////  

#pragma once

#include "../../renderData/include/speedTreeV8Sdk.h"

class RENDER_DATA_API CSpeedTreeWind
{
	RED_USE_MEMORY_POOL( rend::PoolFoliage );

public:
	CSpeedTreeWind();
	virtual ~CSpeedTreeWind();

	// settings
	void							SetParams( const SpeedTree::SConfigGames8& sParams );	//!< this should be called infrequently and never when trees that use it are visible

	virtual CSpeedTreeWind& operator=( const CSpeedTreeWind& obj );
private:
	static constexpr Uint32 CURRENT_VERSION = 1;

protected:

	SpeedTree::CWindStateMgr m_cWindMgr;
	SpeedTree::SConfigGames8 m_sV8Params;

	friend RENDER_DATA_API IFile& SerializeWindParams( class IFile& file, CSpeedTreeWind& wind );
};

RENDER_DATA_API IFile& SerializeWindParams( class IFile& file, CSpeedTreeWind& wind );
