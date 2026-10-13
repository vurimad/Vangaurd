/*
 * Copyright (c) 2018 CD Projekt Red. All Rights Reserved.
 */

#include "build.h"
#include "dependencyOutputCollector.h"

namespace dependency
{

void OutputCollector::SetPrimary( const res::ResourcePath& path, data::DepotLocation depot )
{
	m_primary.path = path;
	m_primary.timestamp = 0ull;
	m_primary.hash.Clear();
	m_primary.depot = depot;
}

void OutputCollector::Add( const res::ResourcePath& path, data::DepotLocation depot )
{
	if ( path == m_primary.path && depot == m_primary.depot )
	{
		// Skip processing of primary resource
		return;
	}

	OutputFileInfo item;
	item.path = path;
	item.timestamp = 0ull;
	item.hash.Clear();
	item.depot = depot;

	RED_SCOPE_LOCK( m_guard );
	m_outputs.PushBack( item );
}

void OutputCollector::Clear()
{
	RED_SCOPE_LOCK( m_guard );
	m_outputs.Clear();
}

OutputFileInfo OutputCollector::GetPrimaryOutput() const
{
	RED_SCOPE_LOCK( m_guard );
	return m_primary;
}

OutputFileList OutputCollector::GetOutputList() const
{
	RED_SCOPE_LOCK( m_guard );
	return m_outputs;
}

Uint32 OutputCollector::GetOutputListSize() const
{
	RED_SCOPE_LOCK( m_guard );
	return m_outputs.Size();
}

} // data
