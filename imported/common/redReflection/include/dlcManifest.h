/*
* Copyright (c) 2020 CD Projekt Red. All Rights Reserved.
*/

#pragma once

#include "resource.h"
#include "resourceAsyncReference.h"

namespace res
{

class RED_REFLECTION_API DlcManifest : public CResource
{
	RTTI_DECLARE_TYPE( DlcManifest );
	RED_USE_MEMORY_POOL( red::PoolEngine );
	IMPLEMENT_RESOURCE_INTERFACE( "", "dlc_manifest", "DLC Manifest Resource" );
public:
	DlcManifest();
	~DlcManifest();

	static res::ResourcePath GetPathForDLC( const red::String& basePath );

	TResAsyncRef< CResource > m_tweakBlob;
	TResAsyncRef< CResource > m_quest;
	TResAsyncRef< CResource > m_journal;
	TResAsyncRef< CResource > m_factories;
	TResAsyncRef< CResource > m_weaponAppearances;
	TResAsyncRef< CResource > m_vehicleAppearances;
	TResAsyncRef< CResource > m_communitySpawnsets;
};

} // res
