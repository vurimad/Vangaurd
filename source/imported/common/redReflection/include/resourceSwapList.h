/*
 * Copyright (c) 2019 CD Projekt Red. All Rights Reserved.
 */

#pragma once

#include "resourcePath.h"

namespace res
{

class RED_REFLECTION_API ResourceSwapList
{
	RED_USE_MEMORY_POOL( red::PoolEngine );
public:
	ResourceSwapList();
	~ResourceSwapList();

	bool IsPathSwapped( const res::ResourcePath& path ) const;
	res::ResourcePath ResolvePath( const res::ResourcePath& path ) const;

	void Internal_SetSwappedResources( const red::Map< res::ResourcePath, res::ResourcePath >& swappedResources );

private:
	red::Map< res::ResourcePath, res::ResourcePath > m_swappedResources{ red::PoolEngine() };
};

} // res
