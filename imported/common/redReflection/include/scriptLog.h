/**
* Copyright (c) 2017 CD Projekt Red. All Rights Reserved.
*/

#pragma once

#ifdef RED_LOGGING_ENABLED

#	include "scriptStackFrame.h"
#	include "rttiFunction.h"

#	define SCRIPT_LOG_FORMAT "%hs(%hu) %hs::%hs(): "

#	define SCRIPT_LOG( stack, message, ... ) script::Log( red::LoggerLevel_Info, stack, SCRIPT_LOG_FORMAT message, ## __VA_ARGS__ )
#	define SCRIPT_WARNING( stack, message, ... ) script::Log( red::LoggerLevel_Warning, stack, SCRIPT_LOG_FORMAT message, ## __VA_ARGS__ )
#	define SCRIPT_ERROR( stack, message, ... ) script::Log( red::LoggerLevel_Error, stack, SCRIPT_LOG_FORMAT message, ## __VA_ARGS__ )

#	define SCRIPT_RUNTIME_WARN( stack, message, ... ) script::LogRuntimeError( red::LoggerLevel_Warning, stack, message, ## __VA_ARGS__ )
#	define SCRIPT_RUNTIME_ERROR( stack, message, ... ) script::LogRuntimeError( red::LoggerLevel_Error, stack, message, ## __VA_ARGS__ )

#	define SCRIPT_LOG_CONDITION( condition, stack, message, ... ) if( condition ) { SCRIPT_LOG( stack, message, ## __VA_ARGS__ ); }
#	define SCRIPT_ERROR_CONDITION( condition, stack, message, ... ) if( condition ) { SCRIPT_ERROR( stack, message, ## __VA_ARGS__ ); }

#	define SCRIPT_RUNTIME_ERROR_CONDITION( condition, stack, message, ... ) if( condition ) { SCRIPT_RUNTIME_ERROR( stack, message, ## __VA_ARGS__ ); }

namespace script
{
	using ScriptRuntimeErrorLogHandler = void(*)( red::LoggerLevel level, const CScriptStackFrame& stack, const red::StringView& message );

	RED_REFLECTION_API void SetScriptRuntimeErrorLogHandler( ScriptRuntimeErrorLogHandler  handler );
	RED_REFLECTION_API void CallScriptRuntimeErrorLogHandler( red::LoggerLevel level, const CScriptStackFrame& stack, const AnsiChar* message );

	template <typename... Args>
	RED_INLINE void Log( red::LoggerLevel level, const CScriptStackFrame& stack, const AnsiChar* format, Args&& ... args )
	{
		const Uint32 size = 1024 * 8;
		AnsiChar buffer[ size ];

		const AnsiChar* file = stack.m_function->GetCode().GetFilename().AsChar();
		const Uint16 line = stack.m_debugData.GetLine();
		const AnsiChar* classname = stack.m_function->GetClass() ? stack.m_function->GetClass()->GetName().AsChar() : "";
		const AnsiChar* funcname = stack.m_function->GetName().AsChar();

		red::SNPrintFSafe( buffer, size, format, file, line, classname, funcname, std::forward<Args>( args )... );

		red::LogMessage( level, buffer, red::LoggerCategory_Scripts );
	}

	template <typename... Args>
	RED_INLINE void LogRuntimeError( red::LoggerLevel level, const CScriptStackFrame& stack, const AnsiChar* format, Args&& ... args )
	{
		// Format the message itself.
		const Uint32 size = 1024 * 8;
		AnsiChar runtimeErrorMessageBuffer[ size ];
		red::SNPrintFSafe( runtimeErrorMessageBuffer, size, format, std::forward<Args>( args )... );

		// Print message to console with additional info and tag.

		const AnsiChar* file = stack.m_function->GetCode().GetFilename().AsChar();
		const Uint16 line = stack.m_debugData.GetLine() + 1;
		const AnsiChar* classname = stack.m_function->GetClass() ? stack.m_function->GetClass()->GetName().AsChar() : "";
		const AnsiChar* funcname = stack.m_function->GetName().AsChar();

		AnsiChar prefixTag[ 64 ] = {};
		red::SNPrintFSafe( prefixTag, sizeof( prefixTag ), "[ScriptRuntimeError][Thread:%X] ", red::ThreadId::CurrentThread().AsNumber() );

		AnsiChar fullMessageBuffer[ size ];
		red::SNPrintFSafe( fullMessageBuffer, size, "%s" SCRIPT_LOG_FORMAT " %s", prefixTag, file, line, classname, funcname, runtimeErrorMessageBuffer );
		red::LogMessage( level, fullMessageBuffer, red::LoggerCategory_ScriptRuntimeErrors );

		AnsiChar contextBuffer[ 1024 ] = {};
		red::Strcat( contextBuffer, prefixTag, sizeof( contextBuffer ) );
		if( IScriptable* const contextObject = stack.GetContext() )
		{
			AnsiChar contextInfoBuffer[ 512 ] = {};
			red::SNPrintFSafe( contextInfoBuffer, sizeof( contextInfoBuffer ), "Context 0x%p (type: %hs, path: %hs)",
				contextObject, contextObject->GetClass()->GetName().AsChar(), contextObject->GetPath().ToDebugString() );

			red::Strcat( contextBuffer, contextInfoBuffer, sizeof( contextBuffer ) );
		}
		else
		{
			red::Strcat( contextBuffer, "Context nullptr", sizeof( contextBuffer ) );
		}
		red::LogMessage( level, contextBuffer, red::LoggerCategory_ScriptRuntimeErrors );

		char stackBuffer[ 1024 ] = {};
		red::Strcat( stackBuffer, prefixTag, sizeof( stackBuffer ) );
		red::Strcat( stackBuffer, "Callstack", sizeof( stackBuffer ) );
		red::LogMessage( level, stackBuffer, red::LoggerCategory_ScriptRuntimeErrors );

		const CScriptStackFrame* frame = &stack;
		while( frame )
		{
			stackBuffer[ 0 ] = 0;
			red::Strcat( stackBuffer, prefixTag, sizeof( stackBuffer ) );
			red::Strcat( stackBuffer, "     ", sizeof( stackBuffer ) );
			frame->DumpTopToString( stackBuffer, sizeof( stackBuffer ) );
			red::LogMessage( level, stackBuffer, red::LoggerCategory_ScriptRuntimeErrors );
			frame = frame->GetParent();
		}

		stackBuffer[ 0 ] = 0;
		red::Strcat( stackBuffer, prefixTag, sizeof( stackBuffer ) );
		red::Strcat( stackBuffer, "Callstack End", sizeof( stackBuffer ) );
		red::LogMessage( level, stackBuffer, red::LoggerCategory_ScriptRuntimeErrors );

		// Then dispatch optional custom handler

		CallScriptRuntimeErrorLogHandler( level, stack, runtimeErrorMessageBuffer );
	}
}

#else

	#define SCRIPT_LOG( ... )
	#define SCRIPT_WARNING( ... )
	#define SCRIPT_ERROR( ... )
	#define SCRIPT_RUNTIME_WARN( ... )
	#define SCRIPT_RUNTIME_ERROR( ... )
	#define SCRIPT_LOG_CONDITION( ... )
	#define SCRIPT_ERROR_CONDITION( ... )
	#define SCRIPT_RUNTIME_ERROR_CONDITION( ... )

#endif // RED_LOGGING_ENABLED
