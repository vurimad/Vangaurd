/*
* Copyright (c) 2019 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"
#include "resourceDatabaseProvider.h"

RTTI_BEGIN_ABSTRACT_TYPE_IN_NAMESPACE(IResourceDatabaseProvider, resdb);
RTTI_END_TYPE();

namespace resdb
{
	IResourceDatabaseProvider::~IResourceDatabaseProvider() = default;
}

