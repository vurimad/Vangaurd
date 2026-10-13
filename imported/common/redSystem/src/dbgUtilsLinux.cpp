/**
* Copyright (c) 2018 CDProjekt Red, Inc. All Rights Reserved.
*/

#include "build.h"
#include "dbgUtils.h"
#include "utility.h"

#ifdef RED_PLATFORM_LINUX

// #temp
RED_DISABLE_WARNING_CLANG( "-Wunused-function" )

namespace helper
{
	Bool IsDebuggerPresent()
	{
		Bool present = ( ptrace( PTRACE_TRACEME, 0, 1, 0 ) < 0 );
			if ( !present ) 
			{ ptrace( PTRACE_DETACH, 0, 1, 0 ); } 
		return present;
	}
}

namespace dbgutils
{

	Bool IsDebuggerAttached( Bool* pOutIsConnectionValid /*= nullptr*/, EConnection conn /*= eConnection_LocalProcess*/ )
	{
		Bool nullIsConnectionValid = true;
		Bool& outIsConnectionValid = pOutIsConnectionValid ? *pOutIsConnectionValid : nullIsConnectionValid;
		
		if ( conn == eConnection_LocalProcess )
		{
			outIsConnectionValid = true;
			return helper::IsDebuggerPresent();
		}

		// no support for remote connections
		outIsConnectionValid = false;
		return false;
	}

	REDSYSTEM_API Bool GEnableTrace = false;
	REDSYSTEM_API FILE* GTraceFile = nullptr;

	Bool IsTraceEnabled()
	{
		return GEnableTrace;
	}

	STATIC_CHECK_USE_DECL
	void Trace( const char* msg, ... )
	{
		va_list arglist;
		va_start( arglist, msg );
		VTrace( msg, arglist );
		va_end( arglist );
	}

	STATIC_CHECK_USE_DECL
	void VTrace( const char* msg, va_list arglist )
	{
		const Uint32 BUFSIZE = 2048;
		char buf[BUFSIZE];
		const Int32 len = red::VSNPrintF( buf, BUFSIZE, msg, arglist );
		if ( len > 0 && len + 1 < BUFSIZE )
		{
			if ( buf[len - 1] != '\r' && buf[len - 1] != '\n' )
			{
				buf[len] = '\n';
				buf[len + 1] = '\0';
			}
		}

		// TODO santi: log using syslog?

		if ( ::SIsMainThread() ) // help avoid hangs
		{
			fputs( buf, ( GTraceFile ? GTraceFile : stderr ) );
		}

	}

	Bool ModularizeStackBackTrace( StackTrace& /*inOutStackTrace*/ )
	{
		// TODO
		return false;
	}

	Bool GetStackBackTrace_Profiler( Uint32 /*numFramesToSkip*/, StackTrace& /*outStackTrace*/ )
	{
		// TODO
		return false;
	}

	RED_NOINLINE
	Bool GetStackBackTrace_Heavy( void* osThreadHandle, const StackBackTraceControl& control, StackTrace& outStackTrace, EConnection conn /*= eConnection_LocalProcess*/ )
	{
		// TODO
		return false;
	}

	Bool GetModuleInfo( const Address& frameAddress, ModuleInfo& outInfo, dbgutils::EConnection connection /*= eConnection_LocalProcess*/)
	{
		// TODO
		return false;
	}

	Bool GetSymbolInfo( const Address& frameAddress, SymbolInfo& outInfo, EConnection conn /*= eConnection_LocalProcess*/ )
	{
		// TODO
		return false;
	}

	Bool GetLineInfo( const Address& frameAddress, LineInfo& outInfo, EConnection conn /*= eConnection_LocalProcess*/ )
	{
		// TODO
		return false;
	}

	Bool WriteMiniDumpFile( const FileInfo& fileName, const MiniDumpArgs& dumpArgs )
	{
		// TODO
		return false;
	}

	void GetClassInformation( const char* const classTypeName, ClassInfo& classInfo )
	{
	}

	void GetTypeInformation( const char* const typeName, TypeInfo& typeInfo )
	{
	}

} // dbgutils
#endif