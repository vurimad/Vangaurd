/*
 * Copyright (c) 2018 CD Projekt Red. All Rights Reserved.
 */

#include "build.h"
#include "dependencyOutputDepotWrapper.h"

namespace dependency
{

OutputCollectorDepotWrapper::OutputCollectorDepotWrapper( res::IBackendResourceDepot& depot, dependency::OutputCollector& collector, data::DepotLocation location )
	: ResourceDepotWrapper( depot )
	, m_collector( collector )
	, m_location( location )
{
}

red::SharedPtr<IFile> OutputCollectorDepotWrapper::CreateResourceWriter( const res::ResourcePath path )
{
	m_collector.Add( path, m_location );
	return m_depot.CreateResourceWriter( path );
}

}