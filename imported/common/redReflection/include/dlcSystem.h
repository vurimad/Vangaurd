/*
* Copyright (c) 2020 CD Projekt Red. All Rights Reserved.
*/

#pragma once

#include "dlcManifest.h"

namespace res
{

class RED_REFLECTION_API DlcSystem
{
	RED_USE_MEMORY_POOL( red::PoolEngine );
public:

	DlcSystem();
	~DlcSystem();

	bool Initialise();
	void Shutdown();

	job::Counter LoadManifests();
	void UnnloadManifests();

	red::DynArray< THandle< CResource > > GetManifests() const;

private:

	struct Entry
	{
		res::ResourcePath m_manifestPath;
		THandle< DlcManifest > m_manifestResource;
	};

	red::DynArray< Entry > m_entries;
	bool m_initialised;
};

RED_REFLECTION_API extern DlcSystem GDlcSystem;

} // res
