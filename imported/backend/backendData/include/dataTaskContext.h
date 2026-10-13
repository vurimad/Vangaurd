/*
* Copyright (c) 2018 CD Projekt Red. All Rights Reserved.
*/

#pragma once

#include "dataContext.h"
#include "dataErrorHandlerImpl.h"
#include "dependencyOutputCollector.h"
#include "dependencyOutputDepotWrapper.h"

namespace data
{

// Wrapper class to provide a custom task-specific error handler to the generators and cookers.
class TaskContext : public Context
{
public:
	TaskContext( Context& context, const res::ResourcePath& path, dependency::OutputCollector& collector );
	~TaskContext();

private:
	TaskErrorHandler m_taskErrorHandler;

	dependency::OutputCollectorDepotWrapper m_sourceDepotWrapper;
	dependency::OutputCollectorDepotWrapper m_cookedDepotWrapper;
};

} // data
