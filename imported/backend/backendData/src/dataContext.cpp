/*
* Copyright (c) 2017 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"
#include "dataContext.h"
#include "dataPlatform.h"

//------------------------------------------------------------------------------

namespace data
{

Context::Context()
	: m_sourceDepot( nullptr )
	, m_cookedDepot( nullptr )
	, m_sourceLoader( nullptr )
	, m_cookedLoader( nullptr )
	, m_sourceDepotDatabase( nullptr )
	, m_outputCollector( nullptr )
	, m_errorHandler( nullptr )
	, m_workerTaskAccumulator( nullptr )
	, m_targetPlatform( data::TargetPlatform::NONE )
	, m_isServer( false )
	, m_isWorker( false )
{
}

Context::~Context() = default;

data::ResourceDepot& Context::GetResourceDepot( data::DepotLocation location ) const
{
	if ( location == DepotLocation::Source )
	{
		return *m_sourceDepot;
	}
	else if ( location == DepotLocation::Cooked )
	{
		return *m_cookedDepot;
	}
	RED_FATAL( "Invalid depot location passed in!" );
	return *m_cookedDepot;
}

data::ResourceLoader& Context::GetResourceLoader( data::DepotLocation location ) const
{
	if ( location == DepotLocation::Source )
	{
		return *m_sourceLoader;
	}
	else if ( location == DepotLocation::Cooked )
	{
		return *m_cookedLoader;
	}
	RED_FATAL( "Invalid depot location passed in!" );
	return *m_cookedLoader;
}

} // data
