/*
* Copyright (c) 2018 CD Projekt Red. All Rights Reserved.
*/

#pragma once

#include "errorHandlerImplHooks.h"

namespace red { namespace err
{
	struct CrashDumpDataTable;

	ErrorHandlerHooks RegisterErrorHooksOnceOrbis( Uint32 errFlags, const char* appVersion, ErrorHandlerHooks::ScriptCallstackVisitorFunc* scriptCallstackVisitor );
} }