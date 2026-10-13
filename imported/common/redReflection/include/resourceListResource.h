/*
 * Copyright (c) 2020 CD Projekt Red. All Rights Reserved.
 */

#pragma once

#include "resource.h"
#include "resourceAsyncReference.h"

namespace red
{

class RED_REFLECTION_API ResourceListResource : public CResource
{
	RTTI_DECLARE_TYPE( ResourceListResource );
	RED_USE_MEMORY_POOL( red::PoolEngine );
	IMPLEMENT_RESOURCE_INTERFACE( "", "reslist", "List of resource files" );
public:
	ResourceListResource();
	~ResourceListResource();

	red::DynArray< TResAsyncRef< CResource > > m_resources;
	// Optional list of descriptions to go along with the resources array.
	red::DynArray< String > m_descriptions;
};

} // red
