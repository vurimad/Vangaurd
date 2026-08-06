/**
* Copyright (c) 2016 CD Projekt Red. All Rights Reserved.
*/

#pragma once

class CScriptDebuggerImplBase
{
	RED_USE_MEMORY_POOL( red::PoolScript );

public:
	virtual ~CScriptDebuggerImplBase() {}

	virtual Bool IsDebuggerConnected() const = 0;
};
