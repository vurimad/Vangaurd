/*
 * Copyright (c) 2017 CD Projekt Red. All Rights Reserved.
 */

#include "build.h"
#include "dataSystemContext.h"

namespace data
{

SystemContext::SystemContext()
	: Context()
	, m_sourceDepsCache( nullptr )
	, m_cookedDepsCache( nullptr )
	, m_depsDatabase( nullptr )
	, m_dataCache( nullptr )
	, m_force( false )
	, m_collectDebugInfo( false )
{
}

SystemContext::~SystemContext() = default;

dependency::Cache& SystemContext::GetDepsCache( data::DepotLocation depotLocation ) const
{
	if ( depotLocation == data::DepotLocation::Source )
	{
		return *m_sourceDepsCache;
	}
	if ( depotLocation == data::DepotLocation::Cooked )
	{
		return *m_cookedDepsCache;
	}
	RED_FATAL( "Invalid depot location passed in!" );
	return *m_cookedDepsCache;
}

} // data
