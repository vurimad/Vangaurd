/**
* Copyright (c) 2016 CDProjekt Red, Inc. All Rights Reserved.
*/

#pragma once

#include "fixedSizeFunction.h"
#include "threads.h"

namespace red
{
	class GUID;

#if !defined( RED_DLL ) && !defined( RED_WITH_DLL )
	extern thread_local Bool g_oom;
#endif

	enum EErrorReason : Uint8
	{
		eErrorReason_Unknown,				//!< Unknown reason
		eErrorReason_PureCall,				//!< C++ pure virtual function call
		eErrorReason_Assert,				//!< Assert failed
		eErrorReason_Abort,					//!< CRT abort() was called
		eErrorReason_CrtError,				//!< CRT assert/error handler invoked
		eErrorReason_UnhandledException,	//!< Unhandled exception: #tbd: currently just SEH
		eErrorReason_InvalidParameter,		//!< CRT invalid paramter handler invoked
		// #tbd: integrate with redMemory or remove?
		eErrorReason_OutOfMemory,			//!< Fatal out of memory occurred
	};

	enum EErrorHandlerFlags : Uint32
	{
		eErrorHandlerFlags_DisableAllCrashReporting = 1 << (0),
		eErrorHandlerFlags_ErrorReporterJustInTime	= 1 << (1),	//!< Launch reporter just-in-time on error; spawning may fail due to failure conditions

		// #tbd: TTY for PS4
		eErrorHandlerFlags_MessageStreamTrace	= 1 <<(2),		//!< Debug trace
		eErrorHandlerFlags_MessageStreamStderr	= 1 <<(3),		//!< Stderr
		eErrorHandlerFlags_MessageStreamStdout	= 1 <<(4),		//!< Stdout
		eErrorHandlerFlags_MessageStreamConsole = 1 << (5),		//!< OutputDebugString or platform equiv
		eErrorHandlerFlags_MessageStreamLog		= 1 << (6),		//!< To the logging system
		eErrorHandlerFlags_AllMessageStreams = eErrorHandlerFlags_MessageStreamTrace | eErrorHandlerFlags_MessageStreamStderr | 
											   eErrorHandlerFlags_MessageStreamStdout | eErrorHandlerFlags_MessageStreamConsole | eErrorHandlerFlags_MessageStreamLog,

		eErrorHandlerFlags_Default = eErrorHandlerFlags_MessageStreamConsole | eErrorHandlerFlags_MessageStreamLog,
	};

	struct REDSYSTEM_API ErrorMessage
	{
		RED_FORCE_INLINE ErrorMessage()
			: m_file( nullptr )
			, m_expression( nullptr )
			, m_message( nullptr )
			, m_line( 0 )
		{}

		Bool IsValid() const
		{
			return m_file && m_expression && m_message;
		}

		const char* m_file;
		const char* m_expression;
		const char* m_message;
		Uint32		m_line;
	};

	using ScriptStackFrameVisitorFunc = red::FixedSizeFunction< Bool( Uint32, const red::AnsiChar*, Uint32 ) >;
	using ScriptCallstackVisitorFunc = void( ScriptStackFrameVisitorFunc );

	//! Initialize the error handler; reinitialization is supported as much as possible.
	//! However, it's intended that InitErrorHandler is called early in program start and not as a way to switch flags on and off
	//! at whatever time.
	extern REDSYSTEM_API void			InitErrorHandler( Uint32 errorHandlerFlags, ScriptCallstackVisitorFunc* visitScriptCallstack = nullptr );

	//! Handles error reporting and possible termination.
	//! @param errorReason the error
	//! @param message custom error message parameters
	//! @param callstackFramesToSkip number of call frames to omit from any error reports; only makes sense if the caller cannot be inlined
	//! @param pInOutErrControl optional internal state controlling repeated errors. Initialize to zero if used.
	//! @param internal platform specific data.
	extern REDSYSTEM_API void			HandleAssert( const ErrorMessage& customErrorMsg );

	extern REDSYSTEM_API void FastFailAbortProcess() RED_ANALYSIS_NORETURN_CLANG_ATTR;

	//! Attach a file for the error report; the file doesn't have to exist yet.
	//! On Windows, the path can be absolute or relative to the executable.
	//! The directory separators are converted to appropriate type per platform.
	extern REDSYSTEM_API Bool			RegisterAttachmentForErrorReport( const char* pathToRegister );

	extern REDSYSTEM_API const char*	ErrorReasonText( red::EErrorReason errorReason );

	//! Set id of the logger thread - used by thread suspending on crash to not suspend the log thread.
	extern REDSYSTEM_API void			SetLoggerThreadId( red::ThreadId tid );
}

namespace errorReporter
{
	extern REDSYSTEM_API Int32 MainLoop( const char* connectionString );
	extern REDSYSTEM_API Bool IsAttachedToProcess( Uint32 processID );
}
