/*
 * Copyright (c) 2018 CD Projekt Red. All Rights Reserved.
 */

#pragma once

#include "dataResourceDepotWrapper.h"

#include "dependencyOutputCollector.h"

namespace dependency
{

class OutputCollectorDepotWrapper : public data::ResourceDepotWrapper
{
public:
	OutputCollectorDepotWrapper( res::IBackendResourceDepot& depot, dependency::OutputCollector& collector, data::DepotLocation location );
	virtual red::SharedPtr<IFile> CreateResourceWriter( const res::ResourcePath resourcePath ) override;

private:
	dependency::OutputCollector& m_collector;
	data::DepotLocation m_location;
};

} // namespace dependency
