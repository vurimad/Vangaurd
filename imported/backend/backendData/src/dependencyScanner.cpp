/*
 * Copyright (c) 2017 CD Projekt Red. All Rights Reserved.
 */

#include "build.h"
#include "dependencyScanner.h"
#include "dependencyScannerImpl.h"
#include "../../backendEngine/include/resourceUtils.h"

namespace dependency
{

bool ScanResource( res::IBackendResourceDepot* depot, const res::ResourcePath& path, red::DynArray< res::ResourcePath >& result )
{
	return prv::Success( prv::ScanResource( depot, path, result ) );
}

} // dependency
