/*
 * Copyright (c) 2017 CD Projekt Red. All Rights Reserved.
 */

#pragma once

namespace res
{
class IBackendResourceDepot;
} // res

namespace dependency
{

BACKEND_DATA_API bool ScanResource( res::IBackendResourceDepot* depot, const res::ResourcePath& path, red::DynArray< res::ResourcePath >& result );

} // dependency
