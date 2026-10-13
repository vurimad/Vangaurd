/*
 * Copyright (c) 2019 CD Projekt Red. All Rights Reserved.
 */

#include "build.h"
#include "resourceSwapList.h"
#include "util.h"
#include "resourceLoader.h"
#include "resourceToken.h"

namespace res
{

//------------------------------------------------------------------------------

ResourceSwapList::ResourceSwapList()
{
}

ResourceSwapList::~ResourceSwapList()
{
}

bool ResourceSwapList::IsPathSwapped( const res::ResourcePath& path ) const
{
	return m_swappedResources.Find( path ) != m_swappedResources.End();
}

res::ResourcePath ResourceSwapList::ResolvePath( const res::ResourcePath& path ) const
{
	auto iter = m_swappedResources.Find( path );
	if ( iter != m_swappedResources.End() )
	{
		return iter.Value();
	}
	return path;
}

void ResourceSwapList::Internal_SetSwappedResources( const red::Map< res::ResourcePath, res::ResourcePath >& swappedResources )
{
	m_swappedResources = swappedResources;
}

} // res
