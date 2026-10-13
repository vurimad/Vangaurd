/*
 * Copyright (c) 2018 CD Projekt Red. All Rights Reserved.
 */

#include "build.h"
#include "linkResolver.h"

#include "fileLinkMap.h"

namespace commandlet
{

FileLinkResolver::FileLinkResolver( const red::HashMap< res::ResourcePath, res::ResourcePath >& linkMapping )
	: m_linkMapping( linkMapping )
{}

FileLinkResolver::FileLinkResolver( const commandlet::FileLinkMap& linkMap )
	: m_linkMapping( linkMap.Mapping() )
{}

res::ResourcePath FileLinkResolver::ResolvePath( const res::ResourcePath& path )
{
	auto iter = m_linkMapping.Find( path );
	if ( iter != m_linkMapping.End() )
	{
		return iter.Value();
	}
	else
	{
		return path;
	}
}

} // commandlet
