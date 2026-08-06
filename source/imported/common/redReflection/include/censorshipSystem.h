/*
 * Copyright (c) 2019 CD Projekt Red. All Rights Reserved.
 */

#pragma once

#include "bitFieldBuilder.h"

 //------------------------------------------------------------------------------
 // Censorship flags

enum CensorshipFlags : Uint32
{
	Censor_Nudity			= RED_FLAG(0), // Nudity (showing genitals)
	Censor_OverSexualised	= RED_FLAG(1), // Sexually explicit content
	Censor_Suggestive		= RED_FLAG(2), // Sexually suggestive content
	Censor_Homosexuality	= RED_FLAG(3), // Homosexual content
	Censor_Gore				= RED_FLAG(4), // Gore during dismemberment
	Censor_Drugs			= RED_FLAG(5), // Drug use or explicit references
	Censor_Religion			= RED_FLAG(6), // Reference to religion or other gods or religions
	Censor_WinnieThePooh	= RED_FLAG(7), // China
};

RTTI_DECLARE_BITFIELD( CensorshipFlags );

namespace red
{

// Global values for censorship system, in this library so they can be accessed everywhere in the code
class RED_REFLECTION_API CensorshipSystem
{
	RED_USE_MEMORY_POOL( red::PoolEngine );
public:
	CensorshipSystem();
	~CensorshipSystem();

	void SetRegion( CName region );

	bool IsCensoringSomething() const;
	bool IsRegionCensoringSomething() const;

	// Censorship flags
	Uint32 GetCensorFlags() const;
	Uint32 GetRegionCensorFlags() const;

	Uint32 GetSaveGameCensorFlags() const;
	void SetSaveGameCensorFlags( Uint32 flags );

	// Note: Do not use this to perform your own censorship, use the flags directly
	CName GetCensorRegion() const;

	// Set the optional nudity censorship
	void SetOptionalNudityCensorship( bool setting );

	bool IsRegionNudityCensored() const { return ( m_regionCensorFlags & Censor_Nudity ) != 0; }
	bool IsRegionSexualisedCensored() const { return ( m_regionCensorFlags & Censor_OverSexualised ) != 0; }
	bool IsRegionSuggestiveCensored() const { return ( m_regionCensorFlags & Censor_Suggestive ) != 0; }
	bool IsRegionHomosexualityCensored() const { return ( m_regionCensorFlags & Censor_Homosexuality ) != 0; }
	bool IsRegionGoreCensored() const { return ( m_regionCensorFlags & Censor_Gore ) != 0; }
	bool IsRegionDrugsCensored() const { return ( m_regionCensorFlags & Censor_Drugs ) != 0; }
	bool IsRegionReligionCensored() const { return ( m_regionCensorFlags & Censor_Religion ) != 0; }

	bool IsNudityCensored() const { return ( m_censorFlags & Censor_Nudity ) != 0; }
	bool IsSexualisedCensored() const { return ( m_censorFlags & Censor_OverSexualised ) != 0; }
	bool IsSuggestiveCensored() const { return ( m_censorFlags & Censor_Suggestive ) != 0; }
	bool IsHomosexualityCensored() const { return ( m_censorFlags & Censor_Homosexuality ) != 0; }
	bool IsGoreCensored() const { return ( m_censorFlags & Censor_Gore ) != 0; }
	bool IsDrugsCensored() const { return ( m_censorFlags & Censor_Drugs ) != 0; }
	bool IsReligionCensored() const { return ( m_censorFlags & Censor_Religion ) != 0; }

private:
	void UpdateCensorFlags();

	CName m_region;
	Uint32 m_censorFlags;
	Uint32 m_regionCensorFlags;
	Uint32 m_savegameCensorFlags;
	bool m_disableNudity;
};

RED_REFLECTION_API extern CensorshipSystem GCensorshipSystem;

} // red
