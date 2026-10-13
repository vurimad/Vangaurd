/**
* Copyright (c) 2016 CDProjekt Red, Inc. All Rights Reserved.
*/

#include "build.h"
#include "dbgUtils.h"

#ifdef RED_PLATFORM_DURANGO

// Need latest XDK for SymbolResolve
#if _XDK_VER < 0x38390422
# error Your XDK is too old.
#endif

// #fixme: should be part of .props in premake. The most kosher way is adding as a project reference, but that seems to have other issues.

/*
From the release notes since the SDK docs are insufficent: https://developer.xboxlive.com/en-us/platform/development/documentation/software/Pages/14_07_whats_new_jun15.aspx
GetLaunchActivationArgs
Developers can use the new GetLaunchActivationArgs function to get the protocol activation arguments for use in static initialization.
This function can be called prior to the application’s activation event.It will return the activation arguments specified in Visual Studio or from
the command - line. This function is available for retail scenarios, however it returns an empty string.

To reference this API, right click on the visual studio project, select References, click Add New Reference and check the box next to Xbox DevKit Extensions.
*/

#include <WinString.h>
extern HRESULT WINAPI GetLaunchActivationArgs( HSTRING* args );
#pragma comment( lib, "ExtensionSDKs/XboxDevKitExtensions/8.0/DesignTime/CommonConfiguration/Neutral/Lib/devkitextensions.lib" )

//#tbd: LoadLibrary, but if crashing at start could have a DLL loader lock scenario.
#pragma comment( lib, "toolhelpx.lib" )
#include <SymbolResolve.h>

#include <winsock2.h>
#include <Ws2tcpip.h>

namespace dbgutils
{

Bool IsDebuggerAttached( Bool* pOutIsConnectionValid /*= nullptr*/, EConnection conn /*= eConnection_LocalProcess*/ )
{
	Bool nullIsConnectionValid = true;
	Bool& outIsConnectionValid = pOutIsConnectionValid ? *pOutIsConnectionValid : nullIsConnectionValid;
	outIsConnectionValid = true;
	if ( conn != eConnection_LocalProcess )
	{
		outIsConnectionValid = false;
		return false;
	}
	return ::IsDebuggerPresent() != 0;
}

Bool IsTraceEnabled()
{
#if !defined( RED_CONFIGURATION_FINAL ) || defined( USE_PROFILER )
	return true;
#else
	return false;
#endif
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
	char buf[ BUFSIZE ];
	const Int32 len = red::VSNPrintF( buf, BUFSIZE, msg, arglist );
	if ( len > 0 && len + 1 < BUFSIZE )
	{
		if ( buf[ len - 1 ] != '\r' && buf[ len - 1 ] != '\n' )
		{
			buf[ len ] = '\n';
			buf[ len + 1 ] = '\0';
		}
	}
	::OutputDebugStringA( buf );
}

static Bool CaptureFromContext( const PCONTEXT pContext, Uint32 numAdditionalFramesToSkip, StackTrace& outStackTrace )
{
	if ( !pContext )
	{
		return false;
	}

	// Copy context to avoid any side effects for exception handlers
	CONTEXT ctxt = *pContext;

	Uint32 numRecordedFrames = 0;

	Uint32 numWalkedFrames = 0;
	Uint64 baseAddress = 0;

	KNONVOLATILE_CONTEXT_POINTERS nvContext;
	red::Memzero( &nvContext, sizeof( KNONVOLATILE_CONTEXT_POINTERS ) );
	void*       handlerData = nullptr;
	Uint64		establisherFrame = 0;

	do
	{
		RUNTIME_FUNCTION *pRF = RtlLookupFunctionEntry( ctxt.Rip, &baseAddress, NULL );
		const Uint64 absoluteVirtualAddress = ctxt.Rip;

		if (pRF)
		{
			// unwind based on current context
			RtlVirtualUnwind(
				UNW_FLAG_NHANDLER,
				baseAddress,
				ctxt.Rip,
				pRF,
				&ctxt,
				&handlerData,
				&establisherFrame,
				&nvContext );
		}
		else
		{
			// If we don't have a RUNTIME_FUNCTION, then we've encountered
			// a leaf function.  Adjust the stack appropriately.
			ctxt.Rip = *reinterpret_cast<Uint64*>( ctxt.Rsp );
			ctxt.Rsp += 8;
		}

		if ( numWalkedFrames >= numAdditionalFramesToSkip )
		{
			Address& addr = outStackTrace.m_frameAddress[ numRecordedFrames++ ];
			addr.m_absoluteVirtualAddress = absoluteVirtualAddress;
			addr.m_moduleBaseAbsoluteVirtualAddress = baseAddress;
		}

		++numWalkedFrames;

	} while (ctxt.Rip != 0);

	outStackTrace.m_numFrameAddresses = numRecordedFrames;

	return true;
}

// #TODO!
Bool GetStackBackTrace_Profiler( Uint32 /*numFramesToSkip*/, StackTrace& /*outStackTrace*/ )
{
	return false;
}

Bool ModularizeStackBackTrace( StackTrace& /*inOutStackTrace*/ )
{
	return false;
}

RED_NOINLINE
Bool GetStackBackTrace_Heavy( void* osThreadHandle, const StackBackTraceControl& control, StackTrace& outStackTrace, EConnection conn /*= eConnection_LocalProcess*/ )
{
	red::Memzero( &outStackTrace, sizeof(outStackTrace) );

	if ( conn != eConnection_LocalProcess )
	{
		return false;
	}

	if ( control.m_type == eStackBackTraceControlType_Context )
	{
		return CaptureFromContext( static_cast< PCONTEXT >( control.m_context ), control.m_numAdditionalFramesToSkip, outStackTrace );
	}

	Uint64 frames[ MAX_STACK_TRACE_FRAMES ];
	Uint32 numFramesCaptured = RtlCaptureStackBackTrace( 1, MAX_STACK_TRACE_FRAMES, reinterpret_cast< void** >( frames ), nullptr );
	Uint64 unwindToAddress = 0;
	Bool foundUnwindAddress = true;
	if (control.m_type == eStackBackTraceControlType_Unwind)
	{
		unwindToAddress = control.m_unwindToAbsoluteVirtualAddress;
		foundUnwindAddress = false;
	}
	const Uint32 numAdditionalFramesToSkip = control.m_numAdditionalFramesToSkip;
	Uint32 unwindStartIndex = 0;
	Uint32 numRecordedFrames = 0;

	for ( Uint32 i = 0; i < numFramesCaptured; ++i )
	{
		Uint64 baseAddress = 0;
		RUNTIME_FUNCTION *pRF = RtlLookupFunctionEntry( frames[ i ], &baseAddress, nullptr );

		if (pRF == nullptr)
		{
			// If we don't have a RUNTIME_FUNCTION, then we've encountered
			// a leaf function.  Adjust the stack data appropriately.
			HMODULE hMod = nullptr;
			if ( GetModuleHandleEx( GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS, reinterpret_cast< const wchar_t* >( frames[ i ] ), &hMod ) )
			{
				baseAddress = reinterpret_cast< Uint64 >( hMod );
			}
		}

		if ( !foundUnwindAddress )
		{
			if ( frames[i] != unwindToAddress )
			{
				continue;
			}
			foundUnwindAddress = true;
			unwindStartIndex = i;
		}

		if ( ( i - unwindStartIndex ) < numAdditionalFramesToSkip )
		{
			continue;
		}

		Address& addr = outStackTrace.m_frameAddress[ numRecordedFrames++ ];
		addr.m_absoluteVirtualAddress = frames[i];
		addr.m_moduleBaseAbsoluteVirtualAddress = baseAddress;
	}

	outStackTrace.m_numFrameAddresses = numRecordedFrames;

	return true;
}

// If XBSymbolServerProxy
static Bool s_isSymbolResolutionFailed;

Bool GetSymbolInfo( const Address& frameAddress, SymbolInfo& outInfo, EConnection conn /*= eConnection_LocalProcess*/ )
{
	if ( conn != eConnection_LocalProcess )
	{
		return false;
	}

	if ( s_isSymbolResolutionFailed )
	{
		return false;
	}

	// #todo: bulk resolve
	struct ResolverContext
	{
		explicit ResolverContext( SymbolInfo& outSymbolInfo )
			: m_pOutSymbolInfo( &outSymbolInfo )
		{
			m_outHr = E_FAIL;
		}

		static BOOL CALLBACK Callback( LPVOID context, ULONG_PTR address, HRESULT result, PCWSTR name, ULONG offset	)
		{
			ResolverContext* pThis = reinterpret_cast< ResolverContext* >( context );

			SymbolInfo& symInfo = *pThis->m_pOutSymbolInfo;
			
			if ( SUCCEEDED(result) )
			{
				red::WideCharToStdChar_NoConv( symInfo.m_name, name, RED_ARRAY_COUNT_U32( symInfo.m_name ) );
				symInfo.m_displacement = offset;
				symInfo.m_nameLength = static_cast< Uint32 >( red::Strlen( symInfo.m_name ) );
			}

			pThis->m_outHr = result;
			
			return TRUE;
		}

		SymbolInfo* m_pOutSymbolInfo;
		HRESULT		m_outHr;
	};
	
	ResolverContext ctxt( outInfo );
	ULONG_PTR addrs[] = { frameAddress.m_absoluteVirtualAddress };
	HRESULT hr = ::GetSymbolFromAddress( ResolveDisposition::DefaultPriority, RED_ARRAY_COUNT_U32(addrs), addrs, ResolverContext::Callback, &ctxt );
	if ( FAILED(hr) )
	{
		RED_DBG_TRACE( "GetSymbolFromAddress failed: HRESULT=0x%08X", hr );
		s_isSymbolResolutionFailed = true;
		return false;
	}
	if ( FAILED( ctxt.m_outHr ) )
	{
		RED_DBG_TRACE( "GetSymbolFromAddress callback failed: HRESULT=0x%08X", ctxt.m_outHr );
		return false;
	}

	return true;
}

Bool GetLineInfo( const Address& frameAddress, LineInfo& outInfo, EConnection conn /*= eConnection_LocalProcess*/ )
{
	if (conn != eConnection_LocalProcess)
	{
		return false;
	}

	// #todo: bulk resolve
	if ( s_isSymbolResolutionFailed )
	{
		return false;
	}

	struct ResolverContext
	{
		explicit ResolverContext( LineInfo& outLineInfo )
			: m_pOutLineInfo( &outLineInfo )
		{
			m_outHr = E_FAIL;
		}

		static BOOL CALLBACK Callback( LPVOID context, ULONG_PTR address, HRESULT result, PCWSTR filepath, ULONG linenumber )
		{
			ResolverContext* pThis = reinterpret_cast<ResolverContext*>( context );

			LineInfo& lineInfo = *pThis->m_pOutLineInfo;

			if (SUCCEEDED( result ))
			{
				red::WideCharToStdChar_NoConv( lineInfo.m_fileName, filepath, RED_ARRAY_COUNT_U32( lineInfo.m_fileName ) );
				lineInfo.m_lineNumber = linenumber;
				lineInfo.m_fileNameLength = static_cast<Uint32>( red::Strlen( lineInfo.m_fileName ) );
			}

			pThis->m_outHr = result;

			return TRUE;
		}

		LineInfo*	m_pOutLineInfo;
		HRESULT		m_outHr;
	};

	ResolverContext ctxt( outInfo );

	ULONG_PTR addrs[] = { frameAddress.m_absoluteVirtualAddress };
	HRESULT hr = ::GetSourceLineFromAddress( ResolveDisposition::DefaultPriority, RED_ARRAY_COUNT_U32(addrs), addrs, ResolverContext::Callback, &ctxt );
	if (FAILED( hr ))
	{
		RED_DBG_TRACE( "GetSourceLineFromAddress failed: HRESULT=0x%08X", hr );
		s_isSymbolResolutionFailed = true;
		return false;
	}
	if (FAILED( ctxt.m_outHr ))
	{
		RED_DBG_TRACE( "GetSourceLineFromAddress callback failed: HRESULT=0x%08X", ctxt.m_outHr );
		return false;
	}

	return true;
}

Bool GetModuleInfo( const Address& frameAddress, ModuleInfo& outInfo, EConnection conn /*= eConnection_LocalProcess*/ )
{
	if (conn != eConnection_LocalProcess)
	{
		return false;
	}

	HMODULE hModule = reinterpret_cast< HMODULE >( frameAddress.m_moduleBaseAbsoluteVirtualAddress );

	wchar_t buf[ MAX_PATH ] = L"";
	const wchar_t* moduleName = nullptr;
	Uint32 moduleNameLen = 0;

	const Uint32 fullLen = ::GetModuleFileNameW( hModule, buf, MAX_PATH );
	if (fullLen == 0)
	{
		RED_DBG_TRACE( "GetModuleFileNameW HMODULE <%016llX> failed: 0x%08X", frameAddress.m_moduleBaseAbsoluteVirtualAddress, ::GetLastError() );
		return false;
	}
	if (fullLen == MAX_PATH)
	{
		RED_DBG_TRACE( "GetModuleFileNameW HMODULE <%016llX> truncated name", frameAddress.m_moduleBaseAbsoluteVirtualAddress );
	}
	const wchar_t* lastSlash = red::StrchrR( buf, L'\\' );
	moduleName = lastSlash ? lastSlash + 1 : buf;
	moduleNameLen = static_cast<Uint32>( red::Strlen( moduleName ) );

	red::WideCharToStdChar_NoConv( outInfo.m_name, moduleName, ModuleInfo::MAX_SYMBOL_LEN );

	return true;
}


Bool WriteMiniDumpFile( const FileInfo& fileName, const MiniDumpArgs& dumpArgs, EConnection conn /*= eConnection_LocalProcess*/ )
{
	if (conn != eConnection_LocalProcess)
	{
		return false;
	}

	/*
	Developer Tools
Local Crash Dump Support
Starting with the February 2016 Xbox One XDK, you can collect crash dumps on your local development kit instead of sending them to Watson.
These crash dumps are written to your title's scratch drive. You can configure your title to collect the following types of local crash dumps using Developer Home on the Console (Dev Home) Preview,
Xbox One Manager, or the Configuration (xbconfig.exe) tool:

None: No local crash dumps are collected.
Triage: Collects stack traces for all threads, active OS handles, thread information, and information on unloaded modules. Triage dumps are scrubbed of personally identifiable information (PII).
Mini: Collects data segments from all loaded modules, security token related data, thread information, and information on the unloaded modules. Mini dumps are not scrubbed of PII.
Heap: Provides a full user-mode crash dump of all processes and memory running in the game OS. Although this option provides the most information, it can take a few minutes to write out all of the data.
*/

	return false;
}

void GetClassInformation( const char* const classTypeName, ClassInfo& classInfo )
{
}

void GetTypeInformation( const char* const typeName, TypeInfo& typeInfo )
{
}

}

#endif