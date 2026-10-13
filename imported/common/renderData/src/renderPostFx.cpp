/**
* Copyright (c) 2017 CD Projekt Red. All Rights Reserved.
*/
#include "build.h"
#include "renderPostFx.h"

RTTI_BEGIN_ENUM_IN_NAMESPACE( PostFx_ScanningState, rend );
{
	RTTI_ENUM_OPTION( Off );
	RTTI_ENUM_OPTION( Scanning );
	RTTI_ENUM_OPTION( Cancelled );
	RTTI_ENUM_OPTION( Complete );
}
RTTI_END_ENUM();


namespace rend
{
	VisionModeHighlightData::VisionModeHighlightData( Bool seeThroughWalls, Bool isPrimaryPattern, Uint8 fillIndex, Uint8 outlineIndex, Float opacity, Bool initial )
		: m_seeThroughWalls{ seeThroughWalls }
		, m_isPrimaryPattern{ isPrimaryPattern }
		, m_fillIndex{ fillIndex }
		, m_outlineIndex{ outlineIndex }
		, m_opacity{ opacity }
		, m_initial{ initial }
	{}
}

Uint8 PostFXEffectParameters::CPVisionMode::PackHighlightInfo( const Bool seeThroughWalls, const Bool isPrimaryPattern, const Uint8 fillIndex, const Uint8 outlineIndex )
{
	RED_FATAL_ASSERT( fillIndex < 8 );
	RED_FATAL_ASSERT( outlineIndex < 8 );

	Uint8 outInfo = 0;
	if( seeThroughWalls ) outInfo |= 1 << 6;
	if( isPrimaryPattern ) outInfo |= 1 << 5;

	if( fillIndex != 0 )
	{
		outInfo |= fillIndex << 2;
		outInfo |= 0x02;
	}

	if( outlineIndex != 0 )
	{
		if( fillIndex == 0 )
		{
			outInfo |= outlineIndex << 2;
		}
		outInfo |= 0x01;
	}

	return outInfo;
}