/**
* Copyright (c) 2016 CDProjekt Red, Inc. All Rights Reserved.
*/

#include "build.h"
#include "dbgUtils.h"
#include "utility.h"

#ifdef RED_PLATFORM_ORBIS

#include <kernel.h>
#include <libdbg.h>

#ifdef RED_CONFIGURATION_ORBIS_USE_DBGLIB
#pragma comment( lib, "libSceDbg_stub_weak.a" )
#endif

// #temp
RED_DISABLE_WARNING_CLANG( "-Wunused-function" )

namespace dbgutils
{

Bool IsDebuggerAttached( Bool* pOutIsConnectionValid /*= nullptr*/, EConnection conn /*= eConnection_LocalProcess*/ )
{
	Bool nullIsConnectionValid = true;
	Bool& outIsConnectionValid = pOutIsConnectionValid ? *pOutIsConnectionValid : nullIsConnectionValid;
	outIsConnectionValid = true;

#ifdef RED_CONFIGURATION_ORBIS_USE_DBGLIB
	if ( conn != eConnection_LocalProcess )
	{
		return false;
	}
	return ::sceDbgIsDebuggerAttached() != 0;
#else
	outIsConnectionValid = false;
	return false;
#endif
}

Bool IsTraceEnabled()
{
#ifdef RED_CONFIGURATION_ORBIS_USE_DBGLIB
	return true;
#else
	return false;
#endif
}

void Trace( const char* msg, ... )
{
#ifdef RED_CONFIGURATION_ORBIS_USE_DBGLIB
	va_list arglist;
	va_start( arglist, msg );
	::sceDbgUserChannelVPrintf( SCE_DBG_USER_CHANNEL_2, msg, arglist );
	::sceDbgUserChannelPrint( SCE_DBG_USER_CHANNEL_2, "\n" );
	va_end( arglist );
#endif
}

void VTrace( const char* msg, va_list arglist )
{
#ifdef RED_CONFIGURATION_ORBIS_USE_DBGLIB
	::sceDbgUserChannelVPrintf( SCE_DBG_USER_CHANNEL_2, msg, arglist );
	::sceDbgUserChannelPrint( SCE_DBG_USER_CHANNEL_2, "\n" );
#endif
}

static Bool FindKernelModuleBaseAddress( const SceKernelModule modules[], Uint32 numModules, Uint64 absoluteVirtualAddress, Uint64& outModuleBaseAddr, Uint32& outModuleSize )
{
#ifdef RED_CONFIGURATION_ORBIS_USE_DBGLIB
	for ( Uint32 i = 0; i < numModules; ++i )
	{
		SceDbgModuleInfo moduleInfo;
		moduleInfo.size = sizeof(SceDbgModuleInfo);
		const Int32 err = ::sceDbgGetModuleInfo( modules[i], &moduleInfo );
		if ( err != SCE_OK )
		{
			RED_DBG_TRACE( "sceDbgGetModuleInfo failed: 0x%08X", err );
			return false;
		}

		const Uint64 baseAddr = reinterpret_cast< Uint64 >( moduleInfo.segmentInfo[0].baseAddr );
		const Uint32 size = moduleInfo.segmentInfo[0].size;
		if ( absoluteVirtualAddress >=  baseAddr && absoluteVirtualAddress < baseAddr + size )
		{
			outModuleBaseAddr = baseAddr;
			outModuleSize = size;
			return true;
		}
	}
#endif

	return false;
}

static Bool FindKernelModuleInfo( const SceKernelModule modules[], Uint32 numModules, Uint64 moduleBaseAddr, SceDbgModuleInfo& outInfo )
{
#ifdef RED_CONFIGURATION_ORBIS_USE_DBGLIB
	for ( Uint32 i = 0; i < numModules; ++i )
	{
		SceDbgModuleInfo moduleInfo;
		moduleInfo.size = sizeof(SceDbgModuleInfo);
		const Int32 err = ::sceDbgGetModuleInfo( modules[i], &moduleInfo );
		if ( err != SCE_OK )
		{
			RED_DBG_TRACE( "sceDbgGetModuleInfo failed: 0x%08X", err );
			return false;
		}

		const Uint64 baseAddr = reinterpret_cast< Uint64 >( moduleInfo.segmentInfo[0].baseAddr );
		if ( baseAddr == moduleBaseAddr )
		{
			outInfo = moduleInfo;
			return true;
		}
	}
#endif

	return false;
}

static Bool GetKernelModuleList( SceDbgModule modules[], Uint32 maxModules, Uint32& outNumModules )
{
#ifdef RED_CONFIGURATION_ORBIS_USE_DBGLIB
	outNumModules = 0;
	size_t numModules = 0;
	const Int32 err = ::sceDbgGetModuleList( modules, maxModules, &numModules );

	if ( err != SCE_OK )
	{
		RED_DBG_TRACE( "sceDbgGetModuleList failed: 0x%08X", err );
		return false;
	}
	if ( numModules < 1 )
	{
		RED_DBG_TRACE( "Retrieved no kernel modules!" );
		return false;
	}
	
	outNumModules = static_cast< Uint32 >( numModules );
#endif

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
#ifdef RED_CONFIGURATION_ORBIS_USE_DBGLIB
	red::Memzero( &outStackTrace, sizeof(outStackTrace) );

	if ( conn != eConnection_LocalProcess )
	{
		RED_DBG_TRACE( "Only local process stacktrace supported on PS4" );
		return false;
	}

	if ( control.m_type == eStackBackTraceControlType_Context )
	{
		RED_DBG_TRACE( "Context stacktraces not supported on PS4" );
		return false;
	}

	const Uint32 MAX_MODULES = 256;
	SceDbgModule modules[MAX_MODULES];
	Uint32 numModules = 0;
	if ( !GetKernelModuleList( modules, MAX_MODULES, numModules ) )
	{
		return false;
	}
	SceDbgCallFrame frameBuffer[ MAX_STACK_TRACE_FRAMES ];
	Uint32 numFrames = 0;
	const size_t bufferSizeBytes = sizeof(frameBuffer);
	const Int32 err = ::sceDbgBacktraceSelf( frameBuffer, bufferSizeBytes, &numFrames, SCE_DBG_BACKTRACE_MODE_DONT_EXCEED );
	if ( err != SCE_OK )
	{
		RED_DBG_TRACE( "sceDbgBacktraceSelf failed: 0x%08X", err );
		return false;
	}

	Uint64 unwindToAddress = 0;
	Bool foundUnwindAddress = true;
	if ( control.m_type == eStackBackTraceControlType_Unwind )
	{
		unwindToAddress = control.m_unwindToAbsoluteVirtualAddress;
		foundUnwindAddress = false;
	}
	const Uint32 numAdditionalFramesToSkip = control.m_numAdditionalFramesToSkip;
	Uint32 unwindStartIndex = 0;
	Uint32 numRecordedFrames = 0;

	// Cache module info here to avoid looking up for every stack frame. 
	// Can tell if virtual address belong to module by seeing if add >= moduleBaseAddress && add < moduleBaseAddress + moduleSize
	Uint64 currentModuleBaseAddress = 0;
	Uint32 currentModuleSize = 0;

	for ( Uint32 i = 0; i < numFrames; ++i )
	{
		const Uint64 absoluteVirtualAddress = frameBuffer[i].pc;

		if ( absoluteVirtualAddress == 0 )
		{
			// Invalid frame
			break;
		}

		if ( !foundUnwindAddress )
		{
			if ( absoluteVirtualAddress == unwindToAddress )
			{
				foundUnwindAddress = true;
				unwindStartIndex = i;
			}
			else
			{
				continue;
			}
		}

		if ( ( i - unwindStartIndex ) < numAdditionalFramesToSkip )
		{
			continue;
		}

		//  Address falls outside the range of the currently cached module. Find the proper base address for it.
		if ( absoluteVirtualAddress < currentModuleBaseAddress || absoluteVirtualAddress >= currentModuleBaseAddress + currentModuleSize )
		{
			if ( !FindKernelModuleBaseAddress( modules, numModules, absoluteVirtualAddress, currentModuleBaseAddress, currentModuleSize ) )
			{
				RED_DBG_TRACE( "Failed to find kernel module base address for <0x%llX>", absoluteVirtualAddress );
				return false;
			}
		}

		Address& addr = outStackTrace.m_frameAddress[ numRecordedFrames++ ];
		addr.m_absoluteVirtualAddress = absoluteVirtualAddress;
		addr.m_moduleBaseAbsoluteVirtualAddress = currentModuleBaseAddress;
	}

	outStackTrace.m_numFrameAddresses = numRecordedFrames;
	return true;
#else
	return false;
#endif
}

Bool GetModuleInfo( const Address& frameAddress, ModuleInfo& outInfo, dbgutils::EConnection connection /*= eConnection_LocalProcess*/)
{
#ifdef RED_CONFIGURATION_ORBIS_USE_DBGLIB
	if ( connection != eConnection_LocalProcess )
	{
		return false;
	}
	
	if ( !frameAddress.m_moduleBaseAbsoluteVirtualAddress )
	{
		return false;
	}

	const Uint32 MAX_MODULES = 256;
	SceDbgModule modules[MAX_MODULES];
	Uint32 numModules = 0;
	if ( !GetKernelModuleList( modules, MAX_MODULES, numModules ) )
	{
		return false;
	}

	SceDbgModuleInfo moduleInfo;
	if ( !FindKernelModuleInfo( modules, numModules, frameAddress.m_moduleBaseAbsoluteVirtualAddress, moduleInfo ) )
	{
		RED_DBG_TRACE( "Failed to find kernel module info for <0x%llX>", frameAddress.m_moduleBaseAbsoluteVirtualAddress );
		return false;
	}

	const size_t moduleNameLen = red::Strlen( moduleInfo.name );
	if  ( moduleNameLen >= ModuleInfo::MAX_SYMBOL_LEN )
	{
		RED_DBG_TRACE( "Module '%hs' <0x%llX> will be truncated", moduleInfo.name, frameAddress.m_moduleBaseAbsoluteVirtualAddress );
	}
	red::Strcpy( outInfo.m_name, moduleInfo.name, ModuleInfo::MAX_SYMBOL_LEN  );
	return true;
#else
	return false;
#endif
}

Bool GetSymbolInfo( const Address& frameAddress, SymbolInfo& outInfo, EConnection conn /*= eConnection_LocalProcess*/ )
{
	return false;
}

Bool GetLineInfo( const Address& frameAddress, LineInfo& outInfo, EConnection conn /*= eConnection_LocalProcess*/ )
{
	return false;
}

Bool WriteMiniDumpFile( const FileInfo& fileName, const MiniDumpArgs& dumpArgs )
{
	return false;
}

void GetClassInformation( const char* const classTypeName, ClassInfo& classInfo )
{
}

void GetTypeInformation( const char* const typeName, TypeInfo& typeInfo )
{
}

#ifdef RED_CONFIGURATION_SANITIZE
extern "C" {
	void red_sanitize_trap(void)
	{
		/*
		Instruct code generator to emit a function call to the specified function name for __builtin_trap().

		LLVM code generator translates __builtin_trap() to a trap instruction if it is supported by the target ISA.
		Otherwise, the builtin is translated into a call to abort. If this option is set,
		then the code generator will always lower the builtin to a call to the specified function regardless of whether the target ISA has a trap instruction.
		This option is useful for environments (e.g. deeply embedded) where a trap cannot be properly handled, or when some custom behavior is desired.
		*/
		abort();
	}
}
#endif

} // dbgutils
#endif