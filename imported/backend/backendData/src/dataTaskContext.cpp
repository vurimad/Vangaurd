/*
* Copyright (c) 2018 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"
#include "dataTaskContext.h"

namespace data
{

TaskContext::TaskContext( Context& context, const res::ResourcePath& path, dependency::OutputCollector& collector )
	: Context( context )
	, m_taskErrorHandler( context.ErrorHandler(), path )
	, m_sourceDepotWrapper( context.SourceDepot(), collector, data::DepotLocation::Source )
	, m_cookedDepotWrapper( context.CookedDepot(), collector, data::DepotLocation::Cooked )
{
	m_errorHandler = &m_taskErrorHandler;
	m_sourceDepot = &m_sourceDepotWrapper;
	m_cookedDepot = &m_cookedDepotWrapper;
	m_outputCollector = &collector;
}

TaskContext::~TaskContext()
{
}

} // data
