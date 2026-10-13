/*
 * Copyright (c) 2017 CD Projekt Red. All Rights Reserved.
 */

#pragma once

#include "dataTask.h"

class IGenerator;

namespace red
{
	struct InstrumentationObject;
}

namespace job
{
	class Builder;
}

namespace generator
{

class BACKEND_DATA_API Task : public data::Task
{
public:
	Task( const res::ResourcePath& resourcePath, data::SystemContext& system );
	~Task();

private:
	virtual bool InitialiseTool( job::Builder& builder ) override;
	virtual bool GetDependenciesFromTool( job::Builder& builder ) override;
	virtual bool BuildWithTool( job::Builder& builder ) override;

	red::UniquePtr< IGenerator > m_generator;
};

BACKEND_DATA_API data::TaskUniquePtr CreateTask( const res::ResourcePath& path, data::SystemContext& system );

} // generator
