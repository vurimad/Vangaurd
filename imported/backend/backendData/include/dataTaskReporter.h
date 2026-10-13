/*
 * Copyright (c) 2018 CD Projekt Red. All Rights Reserved.
 */

#pragma once

#include "dataTask.h"

namespace data
{

class TaskReporter
{
public:
	virtual void SetTasksCount( Uint32 count ) = 0;
	virtual void UpdateSystemStatus( const red::String& str ) = 0;
	virtual void UpdateTaskProgress( Uint32 id, String&& message ) = 0;
	virtual void ClearTaskProgress( Uint32 id ) = 0;
};

} // data
