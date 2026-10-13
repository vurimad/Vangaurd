/**
* Copyright (c) 2016 CDProjekt Red, Inc. All Rights Reserved.
*/

#include "build.h"

#include "crt.h"
#include "hash.h"
#include "dbgUtils.h"

#if defined( RED_PLATFORM_WINPC )

// The types of inline frame context. Taken from DbgEng.h
// Note that originally it was referring to FrameType but here it is used for the whole FrameInfo.
#define STACK_FRAME_TYPE_INIT   0x00000000
#define STACK_FRAME_TYPE_STACK  0x00000100
#define STACK_FRAME_TYPE_INLINE 0x00000200
#define STACK_FRAME_TYPE_RA     0x00008000 // Whether the instruction pointer is the current IP or a RA from callee frame.
#define STACK_FRAME_TYPE_MASK   0x00000F00

#endif


namespace dbgutils
{

namespace helper
{
	static const AnsiChar* const UNKNOWN_FILE = "<Unknown file>";
	static const AnsiChar* const UNKNOWN_SYMBOL = "<Unknown symbol>";
	static const AnsiChar* const UNKNOWN_MODULE = "<Unknown module>";
	static const AnsiChar* const INLINE_FRAME_PREFIX = "[Inline Frame] ";

	static Bool GetStackInfo( const Address& frameAddress, StackInfo& outInfo, EConnection conn )
	{
		outInfo.m_hasValidModuleInfo = dbgutils::GetModuleInfo( frameAddress, outInfo.m_moduleInfo, conn );
		outInfo.m_hasValidSymbolInfo = dbgutils::GetSymbolInfo( frameAddress, outInfo.m_symbolInfo, conn );
		outInfo.m_hasValidLineInfo = dbgutils::GetLineInfo( frameAddress, outInfo.m_lineInfo, conn );
		outInfo.m_isInlineFrame = false;
		return true;
	}

	static Bool GetStackInfoEx( const AddressEx& frameAddressEx, StackInfo& outInfo, EConnection conn )
	{
#if defined( RED_PLATFORM_WINPC )
		// Check if this is inline frame.
		if ( ( frameAddressEx.m_inlineFrameContext & STACK_FRAME_TYPE_MASK ) == STACK_FRAME_TYPE_INLINE )
		{
			AddressEx realFrameAddressEx = frameAddressEx;
			if ( ( realFrameAddressEx.m_inlineFrameContext & STACK_FRAME_TYPE_RA ) != 0 )
			{
				// Here we should subtract CALL instruction size.
				// But it is hard to get this and 1 works.
				realFrameAddressEx.m_absoluteVirtualAddress -= 1;
			}

			outInfo.m_hasValidModuleInfo = dbgutils::GetModuleInfo( realFrameAddressEx, outInfo.m_moduleInfo, conn );
			outInfo.m_hasValidSymbolInfo = dbgutils::GetInlineSymbolInfo( realFrameAddressEx, outInfo.m_symbolInfo, conn );
			outInfo.m_hasValidLineInfo = dbgutils::GetInlineLineInfo( realFrameAddressEx,  outInfo.m_lineInfo, conn );
			outInfo.m_isInlineFrame = true;
			return true;
		}

		// This is regular frame.
		return GetStackInfo( frameAddressEx, outInfo, conn );
#else
		return false;
#endif
	}

	static Uint32 GetStackInfoForStackTrace( const StackTrace& inStackTrace, StackInfo ( &outStackInfo )[ MAX_STACK_TRACE_FRAMES ], EConnection conn )
	{
		RED_DBG_TRACE( "[GetStackInfoForStackTrace] Start" );

		Uint32 idx = 0;
		for ( idx = 0; idx < inStackTrace.m_numFrameAddresses; ++idx )
		{
			if ( !GetStackInfo( inStackTrace.m_frameAddress[ idx ], outStackInfo[ idx ], conn ) )
			{
				break;
			}
		}

		RED_DBG_TRACE( "[GetStackInfoForStackTrace] End" );

		return idx;
	}

	static Uint32 GetStackInfoForStackTraceEx( const StackTraceEx& inStackTraceEx, StackInfo( &outStackInfo )[ MAX_STACK_TRACE_FRAMES ], EConnection conn, Uint32& outFirstFrameIdx )
	{
#if defined( RED_PLATFORM_WINPC )
		RED_DBG_TRACE( "[GetStackInfoForStackTraceEx] Start" );

		// Remove double inline frame on stack top.
		Uint32 startIdx = 0;
		for ( Uint32 i = 0; i + 1 < inStackTraceEx.m_numFrameAddresses; ++i )
		{
			if( inStackTraceEx.m_frameAddress[ i ].m_absoluteVirtualAddress != inStackTraceEx.m_frameAddress[ i + 1 ].m_absoluteVirtualAddress )
			{
				break;
			}

			// There is regular frame after which is inline frame -> skip it.
			if( ( inStackTraceEx.m_frameAddress[ i ].m_inlineFrameContext & STACK_FRAME_TYPE_MASK ) != STACK_FRAME_TYPE_INLINE )
			{
				startIdx = i + 1;
			}
		}

		Uint32 idx;
		for ( idx = startIdx; idx < inStackTraceEx.m_numFrameAddresses; ++idx )
		{
			if ( !GetStackInfoEx( inStackTraceEx.m_frameAddress[ idx ], outStackInfo[ idx - startIdx ], conn ) )
			{
				break;
			}
		}

		RED_DBG_TRACE( "[GetStackInfoForStackTraceEx] End" );

		outFirstFrameIdx = startIdx;
		return idx - startIdx;
#else
		outFirstFrameIdx = 0;
		return 0;
#endif
	}

	static Bool IsExecutableModule( const StackInfo& info )
	{
		if ( !info.m_hasValidModuleInfo )
		{
			return false;
		}

		const AnsiChar* dot = red::StrchrR( info.m_moduleInfo.m_name, '.' );
		if ( dot && ( red::StrcmpNC( dot, ".exe" ) == 0 || red::StrcmpNC( dot, ".elf" ) == 0 || red::StrcmpNC( dot, ".self" ) == 0 || red::StrcmpNC( dot, ".bin" ) == 0 ) )
		{
			return true;
		}

		return false;
	}

	static Int32 PrintJumpToLineFriendlyStackTraceToBufferHelper( const StackInfo& info, PrintStrackTraceLineCallback callback, void* callbackUserData )
	{
		if ( !callback )
		{
			return -1;
		}

		// Write the line endings so they're in Window's format
		// This is needed as the dialog box won't break text onto
		// new lines if it doesn't encounter a proper windows line ending
		Int32 len = -1;
		if ( !info.m_hasValidSymbolInfo || !info.m_hasValidSymbolInfo )
		{
			return 0;
		}

		len = callback( callbackUserData, "%hs(%u)\r\n", info.GetLineFileName(), info.GetLineNumber() );
		if ( len < 0 )
		{
			return -1;
		}
		return len;
	}

	static Int32 PrintStackTraceToBufferHelper( const Address& frameAddress, const StackInfo& info, Bool printModule, PrintStrackTraceLineCallback callback, void* callbackUserData )
	{
		if ( !callback )
		{
			return -1;
		}

		// Write the line endings so they're in Window's format
		// This is needed as the dialog box won't break text onto
		// new lines if it doesn't encounter a proper windows line ending
		Int32 len = -1;
		if ( !info.m_hasValidSymbolInfo )
		{
			const Uint64 aslrRelativeAddress = frameAddress.m_absoluteVirtualAddress - frameAddress.m_moduleBaseAbsoluteVirtualAddress;
			if ( printModule || !IsExecutableModule( info ) )
			{
				len = callback( callbackUserData, "%hs%hs!0x%llx\r\n", info.GetInlinePrefix(), info.GetModuleName(), aslrRelativeAddress );
			}
			else
			{
				len = callback( callbackUserData, "%hs0x%llx\r\n", info.GetInlinePrefix(), aslrRelativeAddress );
			}
		}
		else
		{
			if ( printModule || !IsExecutableModule( info ) )
			{
				if ( info.m_hasValidLineInfo )
				{
					len = callback( callbackUserData, "%hs%hs!%hs+0x%llx - %hs(%u)\r\n", info.GetInlinePrefix(), info.GetModuleName(), info.GetSymbolName(), info.GetSymbolDisplacement(), info.GetLineFileName(), info.GetLineNumber() );
				}
				else
				{
					// E.g., auto-generated constructor/destructor has no line info
					len = callback( callbackUserData, "%hs%hs!%hs+0x%llx\r\n", info.GetInlinePrefix(), info.GetModuleName(), info.GetSymbolName(), info.GetSymbolDisplacement() );
				}
			}
			else
			{
				if ( info.m_hasValidLineInfo )
				{
					len = callback( callbackUserData, "%hs%hs+0x%llx - %hs(%u)\r\n", info.GetInlinePrefix(), info.GetSymbolName(), info.GetSymbolDisplacement(), info.GetLineFileName(), info.GetLineNumber() );
				}
				else
				{
					// E.g., auto-generated constructor/destructor has no line info
					len = callback( callbackUserData, "%hs%hs+0x%llx\r\n", info.GetInlinePrefix(), info.GetSymbolName(), info.GetSymbolDisplacement() );
				}
			}
		}

		if ( len < 0 )
		{
			return -1;
		}
		return len;
	}

	struct BufferInfo
	{
		BufferInfo( char* buf, size_t bufSize )
			: m_buf( buf )
			, m_bufSize( bufSize )
		{}

		char* m_buf;
		size_t m_bufSize;
	};

	struct BufferPrintLineHelper
	{
		static Int32 Printf( void* callbackUserData, STATIC_CHECK_PRINTF_MSC const AnsiChar* format, ... )
		{
			BufferInfo& info = *reinterpret_cast< BufferInfo* >( callbackUserData );
			if ( info.m_bufSize < 1 )
			{
				return -1;
			}

			va_list args;
			va_start( args, format );
			Int32 retval = red::VSNPrintF( info.m_buf, info.m_bufSize, format, args );
			va_end( args );
			if ( retval >= 0 )
			{
				info.m_buf += retval;
				if ( retval > info.m_bufSize )
				{
					info.m_bufSize = 0;
				}
				else
				{
					info.m_bufSize -= retval;
				}
			}
			return retval;
		}
	};

}


const AnsiChar* StackInfo::GetModuleName() const
{
	return m_hasValidModuleInfo ? m_moduleInfo.m_name : helper::UNKNOWN_MODULE;
}
const AnsiChar* StackInfo::GetSymbolName() const
{
	return m_hasValidSymbolInfo ? m_symbolInfo.m_name : helper::UNKNOWN_SYMBOL;
}
const Uint64 StackInfo::GetSymbolDisplacement() const
{
	return m_hasValidSymbolInfo ? m_symbolInfo.m_displacement : 0;
}
const AnsiChar* StackInfo::GetLineFileName() const
{
	return m_hasValidLineInfo ? m_lineInfo.m_fileName : helper::UNKNOWN_FILE;
}
const Uint32 StackInfo::GetLineNumber() const
{
	return m_hasValidLineInfo ? m_lineInfo.m_lineNumber : 0;
}
const AnsiChar* StackInfo::GetInlinePrefix() const
{
	return m_isInlineFrame ? helper::INLINE_FRAME_PREFIX : "";
}


void PrintStackTraceLineByLine( const dbgutils::StackTrace& stackTrace, PrintStrackTraceLineCallback callback, void* callbackUserData, EConnection conn /*= eConnection_LocalProcess */)
{
	if ( !callback )
	{
		return;
	}

	StackInfo stackInfo[ MAX_STACK_TRACE_FRAMES ] = {};
	Uint32 numFrames = helper::GetStackInfoForStackTrace( stackTrace, stackInfo, conn );

	// #tbd: filtering for DLL build, where our own symbols will be in a DLL
	// Simply filtering out the exe module to reduce visual noise in the dialog box or even in callstack hashes.
	bool printModule = ( numFrames > 0 && !helper::IsExecutableModule( stackInfo[ 0 ] ) );

	for ( Uint32 i = 0; i < numFrames; ++i )
	{
		const Int32 len = helper::PrintStackTraceToBufferHelper( stackTrace.m_frameAddress[ i ], stackInfo[ i ], printModule, callback, callbackUserData );
		if ( len < 0 )
		{
			break;
		}
	}
}

void PrintStackTraceExLineByLine( const dbgutils::StackTraceEx& stackTraceEx, PrintStrackTraceLineCallback callback, void* callbackUserData, EConnection conn /*= eConnection_LocalProcess */)
{
	if ( !callback )
	{
		return;
	}

	Uint32 firstFrameIdx = 0;
	StackInfo stackInfo[ MAX_STACK_TRACE_FRAMES ] = {};
	Uint32 numFrames = helper::GetStackInfoForStackTraceEx( stackTraceEx, stackInfo, conn, firstFrameIdx );

	// #tbd: filtering for DLL build, where our own symbols will be in a DLL
	// Simply filtering out the exe module to reduce visual noise in the dialog box or even in callstack hashes.
	bool printModule = ( numFrames > 0 && !helper::IsExecutableModule( stackInfo[ 0 ] ) );

	for ( Uint32 i = 0; i < numFrames; ++i )
	{
		const Int32 len = helper::PrintStackTraceToBufferHelper( stackTraceEx.m_frameAddress[ firstFrameIdx + i ], stackInfo[ i ], printModule, callback, callbackUserData );
		if ( len < 0 )
		{
			break;
		}
	}
}

void PrintJumpToLineFriendlyStackTraceLineByLine( const StackTrace& stackTrace, PrintStrackTraceLineCallback callback, void* callbackUserData, EConnection conn /*= eConnection_LocalProcess */ )
{
	if ( !callback )
	{
		return;
	}

	StackInfo stackInfo[ MAX_STACK_TRACE_FRAMES ] = {};
	Uint32 numFrames = helper::GetStackInfoForStackTrace( stackTrace, stackInfo, conn );

	for ( Uint32 i = 0; i < numFrames; ++i )
	{
		const Int32 len = helper::PrintJumpToLineFriendlyStackTraceToBufferHelper( stackInfo[ i ], callback, callbackUserData );
		if ( len < 0 )
		{
			break;
		}
	}
}


void ProcessStackTraceLineByLine( const dbgutils::StackTrace& stackTrace, ProcessStrackTraceLineCallback callback, void* callbackUserData, EConnection conn /*= eConnection_LocalProcess */)
{
	if ( !callback )
	{
		return;
	}

	StackInfo stackInfo[ MAX_STACK_TRACE_FRAMES ] = {};
	Uint32 numFrames = helper::GetStackInfoForStackTrace( stackTrace, stackInfo, conn );

	for ( Uint32 i = 0; i < numFrames; ++i )
	{
		callback( callbackUserData, i, stackTrace.m_frameAddress[ i ], stackInfo[ i ] );
	}
}

void ProcessStackTraceExLineByLine( const dbgutils::StackTraceEx& stackTraceEx, ProcessStrackTraceLineCallback callback, void* callbackUserData, EConnection conn /*= eConnection_LocalProcess */)
{
	if ( !callback )
	{
		return;
	}

	Uint32 firstFrameIdx = 0;
	StackInfo stackInfo[ MAX_STACK_TRACE_FRAMES ] = {};
	Uint32 numFrames = helper::GetStackInfoForStackTraceEx( stackTraceEx, stackInfo, conn, firstFrameIdx );

	for ( Uint32 i = 0; i < numFrames; ++i )
	{
		callback( callbackUserData, i, stackTraceEx.m_frameAddress[ firstFrameIdx + i ], stackInfo[ i ] );
	}
}


void PrintStackTraceToBuffer( const dbgutils::StackTrace& stackTrace, char* buf, size_t bufSize, EConnection conn /*= eConnection_LocalProcess */)
{
	red::Memzero( buf, bufSize ); // make sure not leaving any garbage in buffer
	helper::BufferInfo info( buf, bufSize );

	PrintStackTraceLineByLine( stackTrace, helper::BufferPrintLineHelper::Printf, &info, conn );
}

void PrintStackTraceExToBuffer( const dbgutils::StackTraceEx& stackTraceEx, char* buf, size_t bufSize, EConnection conn /*= eConnection_LocalProcess */)
{
	red::Memzero( buf, bufSize ); // make sure not leaving any garbage in buffer
	helper::BufferInfo info( buf, bufSize );

	PrintStackTraceExLineByLine( stackTraceEx, helper::BufferPrintLineHelper::Printf, &info, conn );
}

Bool GenerateHashForStackBackTrace( const dbgutils::StackTrace& stackTrace, Uint64& outHash, dbgutils::EConnection conn )
{
	// #tbd: better hashing than this. Should probably just use "moduleName(tolower string) + symbol relative (non-ASLR) offset".
	// Not including line number (could be same symbol name, different line). 
	// Fuzzy callstack hashing. Should probably also not bother with certain function calls near the end, e.g., main() or other top-level init functions prone to refactor (unless of course
	// there error happens before this cut-off).
	if ( stackTrace.m_numFrameAddresses == 0 )
	{
		outHash = 0;
		return true;
	}

	RED_DBG_TRACE( ">>> Starting stack hash >>>>" );
	Uint64 hash = RED_FNV_OFFSET_BASIS64;
	for ( Uint32 i = 0; i < stackTrace.m_numFrameAddresses; ++i )
	{
		const dbgutils::Address& addr = stackTrace.m_frameAddress[i];
		dbgutils::SymbolInfo symInfo;
		if ( dbgutils::GetSymbolInfo( addr, symInfo, conn ) )
		{
			RED_DBG_TRACE( "Hashing %hs for <0x%016llX>...", symInfo.m_name, addr.m_absoluteVirtualAddress );
			hash = red::CalculateHash64( symInfo.m_name, hash );
		}
		else
		{
			// #tbd: probably some Win32 DLL we don't have symbols for
			// Offset because of address space layout randomization (ASLR)
			dbgutils::ModuleInfo modInfo;
			if ( dbgutils::GetModuleInfo( addr, modInfo ) )
			{
				const Uint64 modOffset = addr.m_absoluteVirtualAddress - addr.m_moduleBaseAbsoluteVirtualAddress;
				RED_DBG_TRACE( "Hashing modName+offset: %hs+0x%llx for <0x%016llX>...", modInfo.m_name, modOffset, addr.m_absoluteVirtualAddress );
				hash = red::CalculateHash64( modInfo.m_name, hash );
				hash = red::CalculateHash64( &modOffset, sizeof(modOffset) );
			}
			else
			{
				// If invalid module, map to same for hash. E.g., whether we're trying to execute 0x0 or 0xFFFFFFFF... isn't too relevent and is probably some garbage value anyway
				RED_DBG_TRACE( "Hashing <unknown> for <0x%016llX>", addr.m_absoluteVirtualAddress);
				hash = red::CalculateHash64( "<unknown>", hash );
			}
		}
	}
	RED_DBG_TRACE( "<<< Finished stack hash <<<" );

	outHash = hash;
	return true;
}

} // dbgutils

