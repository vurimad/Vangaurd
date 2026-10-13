/*
 * Copyright (c) 2017 CD Projekt Red. All Rights Reserved.
 */
#pragma once

#include "../../../common/redCore/include/absolutePath.h"

namespace res
{
namespace prv
{

struct DepotMapping
{
	red::AbsolutePath diskPath;
	red::String mountPath;
	Uint32 level;
};

RED_RESOURCE_API void InitializeResourceDepotMappings( const red::AbsolutePath& engineDepotRoot, const red::AbsolutePath& gameDepotRoot, const red::AbsolutePath& customDepotMapping, red::DynArray< DepotMapping >& mappings );

} // namespace prv
} // namespace res
