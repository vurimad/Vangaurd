/*
 * Copyright (c) 2018 CD Projekt Red. All Rights Reserved.
 */

#pragma once

#include "dataTask.h"

namespace red
{
class BlobView;
}

class ICooker;

namespace cooker
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

	bool ResourceExistsOnDisk( const res::ResourcePath& path ) const;

	red::UniquePtr< ICooker > m_cooker;
};

BACKEND_DATA_API data::TaskUniquePtr CreateTask( const res::ResourcePath& path, data::SystemContext& system );

} // cooker
