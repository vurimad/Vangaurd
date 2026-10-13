/**
* Copyright (c) 2026 RED Vanguard, All Rights Reserved.
*/

#include "build.h"

#include "scriptingSystem.h"
#include "scriptingSystemImpl.h"

IScriptingSystem& IScriptingSystem::GetInstance()
{
	static CScriptingSystem theScriptingSystem;
	return theScriptingSystem;
}
