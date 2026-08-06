/*
 * Copyright (c) 2019 CD Projekt Red. All Rights Reserved.
 */

#include "build.h"
#include "censorshipSystem.h"
#include "util.h"
#include "rttiSystem.h"

RTTI_BEGIN_BITFIELD( CensorshipFlags, 4 );
RTTI_BITFIELD_OPTION( Censor_Nudity );
RTTI_BITFIELD_OPTION( Censor_OverSexualised );
RTTI_BITFIELD_OPTION( Censor_Suggestive );
RTTI_BITFIELD_OPTION( Censor_Homosexuality );
RTTI_BITFIELD_OPTION( Censor_Gore );
RTTI_BITFIELD_OPTION( Censor_Drugs );
RTTI_BITFIELD_OPTION( Censor_Religion );
RTTI_BITFIELD_OPTION_ALIAS( "Censor_Chinese", Censor_WinnieThePooh );
RTTI_END_BITFIELD();

namespace red
{

constexpr red::CNameHash MakeCNameHash( const char* nameString )
{
	return red::CalculateHash64( nameString );
}

// Simple compile time mapping between censor region and the flags indicating what is censored
struct CensorSettings
{
	red::CNameHash region;
	Uint32 censorFlags;
};

namespace
{
constexpr Uint32 Censor_All = Censor_Nudity | Censor_OverSexualised | Censor_Suggestive | Censor_Homosexuality | Censor_Gore | Censor_Drugs | Censor_Religion | Censor_WinnieThePooh;
constexpr Uint32 Censor_Arabic = Censor_Nudity | Censor_OverSexualised | Censor_Suggestive | Censor_Homosexuality | Censor_Religion;
}

// Using country codes from https://en.wikipedia.org/wiki/ISO_3166-1_alpha-2
static constexpr CensorSettings g_censorSettings[] = {

	// Absolutely need to censor these regions

	// Japan
	{ MakeCNameHash( "jp" ), Censor_Nudity | Censor_OverSexualised | Censor_Gore },

	// Arabic Countries
	{ MakeCNameHash( "sa" ), Censor_Arabic },	// Saudi Arabia
	{ MakeCNameHash( "ae" ), Censor_Arabic },	// United Arab Emirates
	{ MakeCNameHash( "bh" ), Censor_Arabic },	// Bahrain
	{ MakeCNameHash( "kw" ), Censor_Arabic },	// Kuwait
	{ MakeCNameHash( "om" ), Censor_Arabic },	// Oman
	{ MakeCNameHash( "qa" ), Censor_Arabic },	// Qatar

	// Dummy entry that allows censoring everything in final build
	{ MakeCNameHash( "censor_all" ), Censor_All },
	// Friendlier name used for Chinese streamers to censor everything
	{ MakeCNameHash( "streaming" ), Censor_All },

#ifndef RED_CONFIGURATION_FINAL
	// Explicitly for testing
	{ MakeCNameHash( "_0" ), 0 },
	{ MakeCNameHash( "_1" ), Censor_Nudity },
	{ MakeCNameHash( "_2" ), Censor_OverSexualised },
	{ MakeCNameHash( "_3" ), Censor_Suggestive },
	{ MakeCNameHash( "_4" ), Censor_Homosexuality },
	{ MakeCNameHash( "_5" ), Censor_Gore },
	{ MakeCNameHash( "_6" ), Censor_Drugs },
	{ MakeCNameHash( "_7" ), Censor_Religion },
	{ MakeCNameHash( "_8" ), Censor_WinnieThePooh },
#endif
};

static constexpr Uint32 GetCensorFlagsForRegion( red::CNameHash regionHash )
{
	for ( const auto& censorEntry : g_censorSettings )
	{
		if ( censorEntry.region == regionHash )
		{
			return censorEntry.censorFlags;
		}
	}
	return 0u;
}


CensorshipSystem::CensorshipSystem()
	: m_region( CName::NONE() )
	, m_censorFlags( 0 )
	, m_regionCensorFlags( 0 )
	, m_savegameCensorFlags( 0 )
	, m_disableNudity( false )
{
}

CensorshipSystem::~CensorshipSystem()
{
}

void CensorshipSystem::SetRegion( CName region )
{
	m_region = region;
	m_regionCensorFlags = GetCensorFlagsForRegion( region.GetHash() );
	UpdateCensorFlags();
}

bool CensorshipSystem::IsCensoringSomething() const
{
	return m_censorFlags != 0;
}

bool CensorshipSystem::IsRegionCensoringSomething() const
{
	return m_regionCensorFlags != 0;
}

Uint32 CensorshipSystem::GetCensorFlags() const
{
	return m_censorFlags;
}

Uint32 CensorshipSystem::GetRegionCensorFlags() const
{
	return m_regionCensorFlags;
}

Uint32 CensorshipSystem::GetSaveGameCensorFlags() const
{
	return m_savegameCensorFlags;
}

void CensorshipSystem::SetSaveGameCensorFlags( Uint32 flags )
{
	m_savegameCensorFlags = flags;
	UpdateCensorFlags();
}

CName CensorshipSystem::GetCensorRegion() const
{
	return m_region;
}

void CensorshipSystem::SetOptionalNudityCensorship( bool setting )
{
	m_disableNudity = setting;
	UpdateCensorFlags();
}

void CensorshipSystem::UpdateCensorFlags()
{
	const Uint32 nudityFlags = m_disableNudity ? Censor_Nudity : 0u;
	m_censorFlags = m_regionCensorFlags | m_savegameCensorFlags | nudityFlags;
}

CensorshipSystem GCensorshipSystem;

} // red
