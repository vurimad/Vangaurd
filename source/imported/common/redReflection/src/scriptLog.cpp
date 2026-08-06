/**
* Copyright (c) 2019 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"

#include "scriptLog.h"

namespace script
{
#ifdef RED_LOGGING_ENABLED
	ScriptRuntimeErrorLogHandler g_scriptRuntimeErrorLogHandler = nullptr;

	void SetScriptRuntimeErrorLogHandler( ScriptRuntimeErrorLogHandler handler )
	{
		g_scriptRuntimeErrorLogHandler = handler;
	}

	void CallScriptRuntimeErrorLogHandler( red::LoggerLevel level, const CScriptStackFrame& stack, const AnsiChar* message )
	{
		if( !g_scriptRuntimeErrorLogHandler )
		{
			return;
		}

		red::StringView messageView( message );
		g_scriptRuntimeErrorLogHandler( level, stack, messageView );
	}
#else
RED_NO_EMPTY_FILE();
#endif
}