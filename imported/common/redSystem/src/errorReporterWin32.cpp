/**
* Copyright (c) 2016 CDProjekt Red, Inc. All Rights Reserved.
*/

#include "build.h"
#include "threads.h" // for RED_TLS
#include "dbgUtils.h"

#ifdef RED_PLATFORM_WINPC

#include <psapi.h>
#include <ShlObj.h>
#pragma comment( lib, "shell32.lib" )

typedef LONG (WINAPI *RtlGetVersionPROC)( PRTL_OSVERSIONINFOW lpVersionInformation );

#include "errorHandler.h"
#include "errorReporterIPCWin32.h"
#include "errorReporterWin32.h"
#include "jsonWriter.h"
#include "stringWriter.h"
#include "guid.h"

namespace red
{
#if defined( RED_PLATFORM_DURANGO ) || defined( RED_PLATFORM_WINPC )
	extern const char* AccessViolationTypeToString( ULONG_PTR type );
	extern const char* ExceptionCodeToString( Uint32 exceptionCode );
	extern const char* ExceptionCodeToDetailsString( Uint32 exceptionCode );
#endif
}

namespace dbgutils
{

extern REDSYSTEM_API Bool GEnableTrace;
extern REDSYSTEM_API Bool GTraceTimestamp;
extern REDSYSTEM_API FILE* GTraceFile;
extern REDSYSTEM_API Bool GIsErrorReporter;

namespace win32
{

const wchar_t REPORT_QUEUE_SUB_PATH[] = L"\\REDEngine\\ReportQueue\\";
const Uint32 CPP_FILE_BUFFER_SIZE = MAX_PATH;
const Uint32 EXPR_BUFFER_SIZE = 128;
const Uint32 MSG_BUFFER_SIZE = 2048;
const Uint32 MAX_FILE_DUPLICATES = 99;

static Bool CopyFileWithProgress( const wchar_t* sourceFileAbsolutePath, const wchar_t* destAbsolutePathDirEndingWithSlash, Bool clobberIfExists = true, LPPROGRESS_ROUTINE progressRoutine = nullptr, void* progressRoutineData = nullptr )
{
	Int32 destPathOrigLen = 0;
	wchar_t destPath[ MAX_PATH ] = { L'\0' };
	{
		const wchar_t* lastSlash = red::StrchrR( sourceFileAbsolutePath, '\\' );
		const wchar_t* entryBaseName = lastSlash ? lastSlash + 1 : sourceFileAbsolutePath;
		destPathOrigLen = red::SNPrintFUnsafe( destPath, MAX_PATH, L"%ls%ls", destAbsolutePathDirEndingWithSlash, entryBaseName );
		if ( destPathOrigLen < 0 )
		{
			RED_DBG_TRACE( "Error formatting path!");
			return false;
		}
	}

	Uint32 attempt = 0;
	Bool again = false;
	do
	{
		again = false;
		const Uint32 copyFlags = COPY_FILE_NO_BUFFERING | ( clobberIfExists ? 0 : COPY_FILE_FAIL_IF_EXISTS ); // unconditional of file size; don't risk the memory caching, and not reading back from it anyway
		if ( !::CopyFileExW( sourceFileAbsolutePath, destPath, progressRoutine, progressRoutineData, nullptr, copyFlags ) )
		{
			if ( ::GetLastError() == ERROR_FILE_EXISTS )
			{
				if ( attempt < MAX_FILE_DUPLICATES )
				{
					++attempt;
					red::SNPrintFUnsafe( destPath + destPathOrigLen, MAX_PATH - destPathOrigLen, L".%u", attempt );
					again = true;
					continue;
				}
			}
			RED_DBG_TRACE( "CopyFileExW failed for '%ls'->'%ls': 0x%08X", sourceFileAbsolutePath, destPath, ::GetLastError() );
			return false;
		}
		else
		{
			RED_DBG_TRACE( "CopyFileExW succeeded '%ls'->'%ls'", sourceFileAbsolutePath, destPath );
		}
	}
	while( again );

	return true;
}

static void CopyAttachmentsWithProgress( const FileInfo& destinationDirectoryAbsolutePath, const RegisteredAttachmentTable& table, LPPROGRESS_ROUTINE progressRoutine, void* progressRoutineData )
{
	RED_DBG_TRACE( "Copying %u attachments...", table.m_numRegisteredAttachments );
	RED_DBG_TRACE( "%u attachments failed to register...", table.m_numFailedToRegister );
	// Validate paths in as much as to help prevent a crash
	for ( Uint32 i = 0; i < table.m_numRegisteredAttachments; ++i )
	{
		const RegisteredAttachmentEntry& entry = table.m_registeredAttachments[i];
		const Uint32 len = static_cast< Uint32 >( red::Strlen( entry.m_absolutePath, RegisteredAttachmentEntry::MaxPath ) );

		// Path length check
		if ( len == 0 || len == RegisteredAttachmentEntry::MaxPath )
		{
			RED_DBG_TRACE( "Invalid Path entry length!" );
			continue;
		}

		// Size check
		{
			HANDLE hFile = ::CreateFileW( entry.m_absolutePath, 0, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr );
			if ( !hFile )
			{
				RED_DBG_TRACE( "Failed to open file '%ls'", entry.m_absolutePath );
				continue;
			}

			LARGE_INTEGER size = { 0 };
			if ( !::GetFileSizeEx( hFile, &size ) )
			{
				RED_DBG_TRACE( "Failed to get file size for '%ls'", entry.m_absolutePath );
				::CloseHandle( hFile );
				continue;
			}
			::CloseHandle( hFile );
			hFile = nullptr;

			if ( size.QuadPart > RegisteredAttachmentTable::MAX_ATTACH_FILE_SIZE )
			{
				RED_DBG_TRACE( "File too large for report '%ls', %llu bytes", entry.m_absolutePath, size.QuadPart );
				continue;
			}
		}

		// #todo: better validation against path truncation
		// Try to copy
		if ( entry.m_absolutePath[0] )
		{
			wchar_t destPath[ MAX_PATH ] = { L'\0' };
			{
				const wchar_t* lastSlash = red::StrchrR( entry.m_absolutePath, '\\' );
				const wchar_t* entryBaseName = lastSlash ? lastSlash + 1 : entry.m_absolutePath;
				red::SNPrintFUnsafe( destPath, MAX_PATH, L"%ls%ls", destinationDirectoryAbsolutePath.m_fileName, entryBaseName );
			}

			// #todo: set progress info in dialog (file name and progress of that file)
			// #tbd: registering files under different dirs but with same name...
			// #tbd: cancellable if taking too long. Could have a progress callback and check the time.
			(void)CopyFileWithProgress( entry.m_absolutePath, destinationDirectoryAbsolutePath.m_fileName, true, progressRoutine, progressRoutineData );
		}
	}
}

namespace
{
	struct TextBuffer
	{
		template< Uint32 N >
		TextBuffer( char (&buf)[N] )
			: m_buf( buf )
			, m_size( N )
		{}

		char*	m_buf;
		Uint32	m_size;
	};
}

static void FormatErrMsg_CustomArgs( HANDLE hRemoteProcess, const IPCArgs& ipcArgs, const StackTraceEx& stackTraceEx,
									TextBuffer dlgMessageBuf, TextBuffer dlgCppFileBuf, TextBuffer dlgExpressionBuf, ErrMsgArgs& outErrMsgArgs )
{
	
	const IPCErrMsgArgs& ipcErrMsgArgs = ipcArgs.m_ipcErrMsgArgs;

	CopyStringToLocal( hRemoteProcess, dlgCppFileBuf.m_buf, dlgCppFileBuf.m_size, ipcErrMsgArgs.m_remoteCppFile, ipcErrMsgArgs.m_cppFileSize );
	CopyStringToLocal( hRemoteProcess, dlgExpressionBuf.m_buf, dlgExpressionBuf.m_size, ipcErrMsgArgs.m_remoteExpression, ipcErrMsgArgs.m_expressionSize );
	CopyStringToLocal( hRemoteProcess, dlgMessageBuf.m_buf, dlgMessageBuf.m_size, ipcErrMsgArgs.m_remoteMessage, ipcErrMsgArgs.m_messageSize );
	Uint32 line = ipcErrMsgArgs.m_line;

	ErrMsgArgs msgArgs;
	{
		msgArgs.m_errorReason = red::ErrorReasonText( ipcArgs.m_errorReason );
		msgArgs.m_cppFile = dlgCppFileBuf.m_buf;
		msgArgs.m_expression = dlgExpressionBuf.m_buf;
		msgArgs.m_message = dlgMessageBuf.m_buf;
		msgArgs.m_line = line;
	}
	outErrMsgArgs = msgArgs;
}

static void FormatErrMsg_UnhandledException( HANDLE hRemoteProcess, const IPCArgs& ipcArgs, const EXCEPTION_RECORD& exceptionRecord, const StackTraceEx& stackTraceEx,
											TextBuffer dlgMessageBuf, TextBuffer dlgCppFileBuf , TextBuffer dlgExpressionBuf, ErrMsgArgs& outErrMsgArgs )
{
	const Uint32 exceptionCode = exceptionRecord.ExceptionCode;

	const Uint64 exceptionAddress = reinterpret_cast< Uint64 >( exceptionRecord.ExceptionAddress );
	Uint64 callerAddress = exceptionAddress;
	RED_DBG_TRACE( "Exception=%hs (0x%08X), ExceptionAddress=0x%016llX", red::ExceptionCodeToString( exceptionCode ), exceptionCode, exceptionAddress );
	
	// List the caller instead
	//#tbd: do only if fails to get line info or do queryvirtualmemory and under certain conditions?
	if ( exceptionCode == EXCEPTION_ACCESS_VIOLATION && exceptionRecord.NumberParameters >= 2 )
	{
		const ULONG_PTR type = exceptionRecord.ExceptionInformation[0];
		const ULONG_PTR exceptionVirtAddr = exceptionRecord.ExceptionInformation[1];
		red::SNPrintFUnsafe( dlgMessageBuf.m_buf, dlgExpressionBuf.m_size, "%hs at 0x%llX.", red::AccessViolationTypeToString( type ), exceptionVirtAddr );
		RED_DBG_TRACE( "ACCESS_VIOLATION type=%u, addr=0x%llx", static_cast< Uint32>( type ), exceptionVirtAddr );
		const Uint32 DataExecutionPrevention = 8;
		if ( type == DataExecutionPrevention )
		{
			// Should be the top entry but search just in case.
			RED_DBG_TRACE( "Searching for caller in stack..." );
			for ( Uint32 i = 0; i < stackTraceEx.m_numFrameAddresses; ++i )
			{
				if ( stackTraceEx.m_frameAddress[i].m_absoluteVirtualAddress == exceptionVirtAddr )
				{
					RED_DBG_TRACE( "Found exception address in callstack at index %u", i );
					if ( i + 1 < stackTraceEx.m_numFrameAddresses )
					{
						callerAddress = stackTraceEx.m_frameAddress[ i + 1 ].m_absoluteVirtualAddress;
						RED_DBG_TRACE( "Caller virtual address 0x%llX", callerAddress );
					}
					else
					{
						RED_DBG_TRACE( "Last in callstack!" );
					}
					break;
				}
			}
		}
	}
	else
	{
		red::SNPrintFUnsafe( dlgMessageBuf.m_buf, dlgMessageBuf.m_size, "%hs.", red::ExceptionCodeToDetailsString( exceptionCode ) );
	}

	red::SNPrintFUnsafe( dlgExpressionBuf.m_buf, dlgExpressionBuf.m_size, "%hs (0x%08X)", red::ExceptionCodeToString( exceptionCode ), exceptionCode );

	// #tbd: or just expose GetSymFromAddr more directly since already have the address.
	const char* cppFile = "<Unknown>";
	Uint32 cppLine = 0;
	dbgutils::LineInfo lineInfoBuf;
	red::Memzero( &lineInfoBuf, sizeof(lineInfoBuf) );
	const dbgutils::Address address = { callerAddress, 0 };
	if ( dbgutils::GetLineInfo( address, lineInfoBuf, eConnection_RemoteProcess ) )
	{
		RED_DBG_TRACE( "FileName=%ls, Line=%u", lineInfoBuf.m_fileName, lineInfoBuf.m_lineNumber );
		cppFile = lineInfoBuf.m_fileName;
		cppLine = lineInfoBuf.m_lineNumber;
	}

	red::Strcpy( dlgCppFileBuf.m_buf, cppFile, dlgCppFileBuf.m_size );

	ErrMsgArgs msgArgs;
	{
		msgArgs.m_errorReason = red::ErrorReasonText( ipcArgs.m_errorReason );
		msgArgs.m_cppFile = dlgCppFileBuf.m_buf;
		msgArgs.m_expression = dlgExpressionBuf.m_buf;
		msgArgs.m_message = dlgMessageBuf.m_buf;
		msgArgs.m_line = cppLine;
	}
	outErrMsgArgs = msgArgs;
}

namespace ErrorReporterHelper
{
	template< typename CH >
	class FileHandleStream : public red::NonCopyable
	{
	public:
		static FileHandleStream<CH>& GetInstance()
		{
			static FileHandleStream<CH> theInstance( INVALID_HANDLE_VALUE );
			return theInstance;
		}

	public:
		/*non-explicit*/ FileHandleStream( HANDLE hFile )
			: m_hFile( hFile )
		{}

		~FileHandleStream()
		{
			if ( m_hFile != INVALID_HANDLE_VALUE )
			{
				::FlushFileBuffers( m_hFile );
				::CloseHandle( m_hFile );
			}
		}

	public:
		Bool Flush( const Bool forced, const CH* data, const red::Uint32 count )
		{
			const DWORD bytesToWrite = sizeof(CH) * count;
			DWORD bytesWritten = 0;
			if ( !::WriteFile( m_hFile, data, bytesToWrite, &bytesWritten, nullptr ) || bytesWritten != bytesToWrite)
			{
				return false;
			}

			return true;
		}

	private:
		HANDLE m_hFile;
	};
}

struct ProcessExeNames
{
	// Don't take up too much space in the path. Full name will be in report file anyway.
	wchar_t processExeAbsolutePath[MAX_PATH] = L"Unknown";
	wchar_t processExeName[64] = L"Unknown";
	wchar_t	processExeNameForPrefix[32] = L"Unknown";
	wchar_t shortProcessExeNameForPrefix[16] = L"Unknown";
};

static void TraceStackTraceEx( const dbgutils::StackTraceEx& stackTraceEx )
{
	RED_DBG_TRACE( ">>> Starting stack trace ex >>>>" );

	for( Uint32 i = 0; i < stackTraceEx.m_numFrameAddresses; ++i )
	{
		RED_DBG_TRACE( "Module = 0x%016llX, Addres = 0x%016llX, InlineContext = 0x%08X", stackTraceEx.m_frameAddress[ i ].m_moduleBaseAbsoluteVirtualAddress, stackTraceEx.m_frameAddress[ i ].m_absoluteVirtualAddress, stackTraceEx.m_frameAddress[ i ].m_inlineFrameContext );
	}

	RED_DBG_TRACE( "<<< Finished stack trace ex <<<" );
}


static void PrintStackTraceAsText( const wchar_t* reportDirAbsolutePath, const ErrMsgArgs &errMsgArgs, const StackTraceEx& stackTraceEx )
{
	HANDLE hFile = INVALID_HANDLE_VALUE;
	{
		wchar_t fileName[ MAX_PATH ] = { '\0' };
		red::SNPrintFUnsafe( fileName, MAX_PATH, L"%lsstacktrace.txt", reportDirAbsolutePath );
		hFile = ::CreateFileW( fileName, GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr );
	}

	if ( hFile != INVALID_HANDLE_VALUE )
	{
		struct StackWriter
		{
			static Int32 Printf( void* callbackUserData, STATIC_CHECK_PRINTF_MSC const AnsiChar* format, ... )
			{
				auto* pWriter = reinterpret_cast< red::StackStringWriter< char, 512, ErrorReporterHelper::FileHandleStream<char> >* >( callbackUserData );
				va_list args;
				va_start( args, format );
				pWriter->VAppendf( format, args );
				va_end( args );
				return 1; // doesn't matter, just not error -1
			}
		};

		ErrorReporterHelper::FileHandleStream<char> str( hFile );
		red::StackStringWriter< char, 512, ErrorReporterHelper::FileHandleStream<char> > writer( str );

		writer.Appendf( "Error reason: %hs\r\n", errMsgArgs.m_errorReason );
		writer.Appendf( "Expression: %hs\r\n", errMsgArgs.m_expression );
		writer.Appendf( "Message: %hs\r\n", errMsgArgs.m_message );
		writer.Appendf( "File: %hs(%u)\r\n", errMsgArgs.m_cppFile, errMsgArgs.m_line );
		// Print the full stack line by line, so shouldn't be truncated	
		writer.Append( "Callstack:\r\n" );
		PrintStackTraceExLineByLine( stackTraceEx, &StackWriter::Printf, &writer, eConnection_RemoteProcess );
	}
}

static void PrintStackTraceAsJson( const wchar_t* reportDirAbsolutePath, const ErrMsgArgs &errMsgArgs, const StackTraceEx& stackTraceEx )
{
	HANDLE hFile = INVALID_HANDLE_VALUE;
	{
		wchar_t fileName[ MAX_PATH ] = { '\0' };
		red::SNPrintFUnsafe( fileName, MAX_PATH, L"%lsstacktrace.json", reportDirAbsolutePath );
		hFile = ::CreateFileW( fileName, GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr );
	}

	if ( hFile != INVALID_HANDLE_VALUE )
	{
		struct JsonStackWriter
		{
			static void Printf( void* callbackUserData, Uint32 idx, const dbgutils::Address& frameAddress, const dbgutils::StackInfo& stackInfo )
			{
				auto* writer = reinterpret_cast< red::JsonWritter< char, 512, ErrorReporterHelper::FileHandleStream<char> >* >( callbackUserData );

				writer->BeginObject();
				{
					writer->AddKey( "module" );
					if ( stackInfo.m_hasValidModuleInfo )
					{
						writer->AddValue( stackInfo.m_moduleInfo.m_name );
					}
					else
					{
						writer->AddValue( nullptr );
					}

					writer->AddKey( "function" );
					if ( stackInfo.m_hasValidSymbolInfo )
					{
						writer->AddValueF( "%s+0x%x", stackInfo.m_symbolInfo.m_name, stackInfo.m_symbolInfo.m_displacement );
					}
					else
					{
						writer->AddValueF( "0x%016x", stackInfo.m_symbolInfo.m_name, stackInfo.m_symbolInfo.m_displacement );
					}

					if ( stackInfo.m_hasValidLineInfo )
					{
						writer->AddKeyValue( "file", stackInfo.m_lineInfo.m_fileName );
						writer->AddKeyValue( "line", stackInfo.m_lineInfo.m_lineNumber );
					}
					else
					{
						writer->AddKeyValue( "file", nullptr );
						writer->AddKeyValue( "line", nullptr );
					}

					writer->AddKeyValue( "inlineFrame", stackInfo.m_isInlineFrame );
				}
				writer->EndObject();
			}
		};

		ErrorReporterHelper::FileHandleStream< char > str( hFile );
		red::JsonWritter< char, 512, ErrorReporterHelper::FileHandleStream< char > > writer( str );

		writer.Begin();
		{
			writer.BeginObject( "errorMessage" );
			{
				writer.AddKeyValue( "errorReason", errMsgArgs.m_errorReason );
				writer.AddKeyValue( "expression", errMsgArgs.m_expression );
				writer.AddKeyValue( "message", errMsgArgs.m_message );
				writer.AddKeyValue( "file", errMsgArgs.m_cppFile );
				writer.AddKeyValue( "line", errMsgArgs.m_line );
			}
			writer.EndObject();

			Bool hasAnyValidLineInfo = false;
			ProcessStackTraceExLineByLine( stackTraceEx, []( void* callbackUserData, Uint32 idx, const dbgutils::Address& frameAddress, const dbgutils::StackInfo& stackInfo )
			{
				Bool& hasAnyValidLineInfo = *reinterpret_cast< Bool* >( callbackUserData );
				if ( stackInfo.m_hasValidLineInfo )
				{
					hasAnyValidLineInfo = true;
				}
			}, reinterpret_cast< void* >( &hasAnyValidLineInfo ), eConnection_RemoteProcess );

			writer.AddKeyValue( "isCallstackValid", hasAnyValidLineInfo );

			writer.BeginArray( "callStack" );
			{
				ProcessStackTraceExLineByLine( stackTraceEx, &JsonStackWriter::Printf, &writer, eConnection_RemoteProcess );
			}
			writer.EndArray();
		}
		writer.End();
	}
}

static void PrintEmptyStackTrace( const wchar_t* reportDirAbsolutePath, const ErrMsgArgs &errMsgArgs )
{
	HANDLE hFile = INVALID_HANDLE_VALUE;
	{
		wchar_t fileName[ MAX_PATH ] = { '\0' };
		red::SNPrintFUnsafe( fileName, MAX_PATH, L"%lsstacktrace.txt", reportDirAbsolutePath );
		hFile = ::CreateFileW( fileName, GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr );
	}

	if ( hFile != INVALID_HANDLE_VALUE )
	{
		ErrorReporterHelper::FileHandleStream<char> str( hFile );
		red::StackStringWriter< char, 512, ErrorReporterHelper::FileHandleStream<char> > writer( str );

		writer.Appendf( "Error reason: %hs\r\n", errMsgArgs.m_errorReason );
		writer.Appendf( "Expression: %hs\r\n", errMsgArgs.m_expression );
		writer.Appendf( "Message: %hs\r\n", errMsgArgs.m_message );
		writer.Appendf( "File: %hs(%u)\r\n", errMsgArgs.m_cppFile, errMsgArgs.m_line );
	}
}

static void PrintStackTrace( const wchar_t* reportDirAbsolutePath, const ErrMsgArgs &errMsgArgs, const StackTraceEx& stackTraceEx )
{
	// Print stack trace
	// #tbd: memmap faulting source file and print out context like WinDBG.
	// But riskier and copied files anyway, so have the info.
	PrintStackTraceAsText( reportDirAbsolutePath, errMsgArgs, stackTraceEx );

	// Print stack trace as Json
	PrintStackTraceAsJson( reportDirAbsolutePath, errMsgArgs, stackTraceEx );

	// Log raw stack trace for debug purposes.
	TraceStackTraceEx( stackTraceEx );
}

static Uint64 CalculateStackTraceHash( const StackTraceEx& stackTraceEx )
{
	// For backward compatibility calculate the hash without inline frames
	StackTrace stackTrace;
	red::Memzero( &stackTrace, sizeof( stackTrace ) );
	GetStackBackTraceFromEx( stackTrace, stackTraceEx );

	Uint64 stackHash = 0;
	if( !dbgutils::GenerateHashForStackBackTrace( stackTrace, stackHash, eConnection_RemoteProcess ) )
	{
		RED_DBG_TRACE( "Failed to generate stack hash!" );
		stackHash = 0;
	}

	return stackHash;
}

static void WriteMiniDump( const wchar_t* reportDirAbsolutePath, const ProcessExeNames& processExeNames, const IPCArgs& ipcArgs )
{
	FileInfo miniDumpFileName;
	red::SNPrintFUnsafe( miniDumpFileName.m_fileName, RED_ARRAY_COUNT_U32( miniDumpFileName.m_fileName ), L"%ls%ls.dmp", reportDirAbsolutePath, processExeNames.processExeNameForPrefix );
	MiniDumpArgs dumpArgs;
	{
		dumpArgs.m_context = ipcArgs.m_pRemoteExceptionPointers;
		dumpArgs.m_threadID = ipcArgs.m_threadID;
	}

	if( !WriteMiniDumpFile( miniDumpFileName, dumpArgs, eConnection_RemoteProcess ) )
	{
		RED_DBG_TRACE( "Failed to write minidump!" );
	}
}

static void WaitForLogFlush( const red::Timer& waitForLogTimer )
{
	//////////////////////////////////////////////////////////////////////////
	// After the minidump, give the crashing process time to flush logs and crash data
	// This should maybe be settable in the ipcArgs... at least wait more than the error handler will wait for logs to finish and to generate crash data
	// but not so long that it becomes even riskier or confusing that the error reporter pops up long after the app crashed
	// TODO: Should wait on an event from the crashing process with a timeout, so don't have to just sleep

#if !defined( RED_CONFIGURATION_FINAL )
	static const Uint32 waitForLogFlushMs = 3000;
#else
	static const Uint32 waitForLogFlushMs = 1000;
#endif
	Uint32 timePassedMs = ( Uint32 )( waitForLogTimer.GetSeconds() * 1000.0 );
	if( timePassedMs < waitForLogFlushMs )
	{
		const Uint32 waitLeftMs = waitForLogFlushMs - timePassedMs;
		RED_DBG_TRACE( "Waitng %u ms for log flush", waitLeftMs );
		red::SleepOnCurrentThread( waitLeftMs );
	}
	else
	{
		RED_DBG_TRACE( "Dump processing time is %u over %u log should be already flushed", timePassedMs - waitForLogFlushMs, waitForLogFlushMs );
	}
}

static void GetRegisteredAttachmentsTable( const IPCContext& ipcContext, const IPCArgs& ipcArgs, RegisteredAttachmentTable& outLocalRegisteredAttachmentTable )
{
	if( !CopyToLocal( ipcContext.m_hRemoteProcess, &outLocalRegisteredAttachmentTable, ipcArgs.m_pRemoteRegisteredAttachmentTable ) )
	{
		red::Memzero( &outLocalRegisteredAttachmentTable, sizeof( outLocalRegisteredAttachmentTable ) );
	}
	if( outLocalRegisteredAttachmentTable.m_numRegisteredAttachments > RegisteredAttachmentTable::MAX_FILE_ATTACHMENTS )
	{
		RED_DBG_TRACE( "Potential RegisteredAttachmentTable corruption detected!" );
		outLocalRegisteredAttachmentTable.m_numRegisteredAttachments = RegisteredAttachmentTable::MAX_FILE_ATTACHMENTS;
	}
}

static void CopyRegisteredAttachments( const RegisteredAttachmentTable& localRegisteredAttachmentTable, const wchar_t* reportDirAbsolutePath )
{
	// #tbd: if some file is opened in exclusive mode, then can fail

	dbgutils::FileInfo attachmentDir;
	red::Memzero( &attachmentDir, sizeof( attachmentDir ) );
	red::SNPrintFUnsafe( attachmentDir.m_fileName, RED_ARRAY_COUNT_U32( attachmentDir.m_fileName ), L"%lsattch\\", reportDirAbsolutePath );
	if( !::CreateDirectoryW( attachmentDir.m_fileName, nullptr ) && ::GetLastError() != ERROR_ALREADY_EXISTS )
	{
		RED_DBG_TRACE( "Failed to create attachment dir %ls: 0x%08X", attachmentDir.m_fileName, ::GetLastError() );
	}
	else
	{
		CopyAttachmentsWithProgress( attachmentDir, localRegisteredAttachmentTable, nullptr, nullptr );
	}
}

static void PrintWERReport( const IPCContext& ipcContext, const wchar_t* reportDirAbsolutePath, const ProcessExeNames& processExeNames, const IPCArgs& ipcArgs, const ErrMsgArgs& errMsgArgs, Uint64 stackHash )
{
	// Create an error report similar to WER for unified parsing if need to fish them out later

	HANDLE hFile = INVALID_HANDLE_VALUE;
	{
		wchar_t fileName[ MAX_PATH ] = { '\0' };
		red::SNPrintFUnsafe( fileName, MAX_PATH, L"%lsreport.txt", reportDirAbsolutePath );
		hFile = ::CreateFileW( fileName, GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr );
	}
	if( hFile != INVALID_HANDLE_VALUE )
	{
		ErrorReporterHelper::FileHandleStream<char> str( hFile );
		red::StackStringWriter< char, 512, ErrorReporterHelper::FileHandleStream<char> > writer( str );
		writer.Appendf( "Version=1\r\n" );
		writer.Append( "EventType=REDEngineErrorReport\r\n" );
		writer.Appendf( "EventTime=%llu\r\n", 0 ); // #tbd: WER timestamp value. Forget it nobody uses this anyway.
		{
			const red::GUID reportGUID = red::GUID::Create(); //#tbd: Allocates memory. UUID from rpc?
			char guidBuf[ 48 ] = { '\0' };
			reportGUID.ToString( guidBuf, RED_ARRAY_COUNT_U32( guidBuf ) );
			red::StrToLower( guidBuf, RED_ARRAY_COUNT_U32( guidBuf ) );
			writer.Appendf( "ReportIdentifier=%hs\r\n", guidBuf );
		}

		writer.Append( "Sig[0].Name=StackHash\r\n" );
		writer.Appendf( "Sig[0].Value=0x%016llx\r\n", stackHash );

		writer.Append( "Sig[1].Name=ErrorReason\r\n" );
		writer.Appendf( "Sig[1].Value=%hs\r\n", errMsgArgs.m_errorReason );

		// Write version number
		{
			char appVersionNumber[ 256 ] = "<Unknown>";
			CopyStringToLocal( ipcContext.m_hRemoteProcess, appVersionNumber, RED_ARRAY_COUNT_U32( appVersionNumber ), ipcArgs.m_remoteAppVersionNumber, ipcArgs.m_remoteAppVersionNumberCount );
			writer.Append( "Sig[2].Name=InternalVersion\r\n" );
			writer.Appendf( "Sig[2].Value=%hs\r\n", appVersionNumber );
		}

		// OS and locale info
		{
			writer.Append( "DynamicSig[1].Name=OS Version\r\n" );

			// Because GetVersionEx lies in Win8+ depending on your EXE manifest.
			// Can still lie if process compatibility mode set...
			HMODULE hNtDLL = LoadLibraryW( L"ntdll.dll" );
			if( hNtDLL )
			{
				RTL_OSVERSIONINFOEXW info;
				red::Memzero( &info, sizeof( info ) );
				info.dwOSVersionInfoSize = sizeof( info );
				RtlGetVersionPROC pfRtlGetVersion = reinterpret_cast< RtlGetVersionPROC >( ::GetProcAddress( hNtDLL, "RtlGetVersion" ) );
				if( pfRtlGetVersion )
				{
					if( pfRtlGetVersion( reinterpret_cast< RTL_OSVERSIONINFOW* >( &info ) ) == 0 /*NT_SUCCESS*/ )
					{
						// #tbd: where does 48 as "productType" come from in some real WER reports? Seems to be there for years in prev versions of Windows.
						writer.Appendf(
							"DynamicSig[1].Value=%u.%u.%u.%u.%u.%u.%u.%u\r\n",
							info.dwMajorVersion,
							info.dwMinorVersion,
							info.dwBuildNumber,
							info.dwPlatformId,
							info.wServicePackMajor,
							info.wServicePackMinor,
							info.wSuiteMask,
							info.wProductType
						);
					}
				}
				::FreeLibrary( hNtDLL );
			}
			else
			{
				RED_DBG_TRACE( "LoadLibraryW failed for ntdll: 0x%08X", ::GetLastError() );
				writer.Append( "DynamicSig[1].Value=<Unknown>\r\n" );
			}

			// #todo: Locale ID
		}

		// LoadedModule infos
		{
			// #tbd: or use heavier CreateToolhelp32Snapshot here if want to also dump more thread info too
			Bool modulesTruncated = false;
			const Uint32 MAX_MODULES = 512;
			HMODULE modules[ MAX_MODULES ];
			red::Memzero( &modules, sizeof( modules ) );
			DWORD sizeNeeded = 0;
			if( EnumProcessModulesEx( ipcContext.m_hRemoteProcess, modules, sizeof( modules ), &sizeNeeded, LIST_MODULES_DEFAULT ) != 0 )
			{
				Uint32 numModules = sizeNeeded / sizeof( HMODULE );
				if( numModules > MAX_MODULES )
				{
					RED_DBG_TRACE( "Warning: EnumProcessModuleEx truncated result: %u of %u elements", MAX_MODULES, numModules );
					numModules = MAX_MODULES;
					modulesTruncated = true;
				}

				wchar_t modulePath[ MAX_PATH ] = { L'\0' };
				char modulePathAscii[ MAX_PATH ] = { '\0' };
				for( Uint32 i = 0; i < numModules; ++i )
				{
					const Uint32 len = ::GetModuleFileNameExW( ipcContext.m_hRemoteProcess, modules[ i ], modulePath, MAX_PATH );
					if( len > 0 )
					{
						const wchar_t* loadedModule = modulePath;
#if defined( RED_CONFIGURATION_FINAL ) && !defined( USE_PROFILER )
						// In Final build we don't want to send paths from users as it may contain GDPR sensitive data
						if( const wchar_t* lastSlash = red::StrchrR( loadedModule, L'\\' ) )
						{
							loadedModule = lastSlash + 1;
						}
#endif
						red::WideCharToStdChar_NoConv( modulePathAscii, loadedModule, RED_ARRAY_COUNT_U32( modulePathAscii ) );
						writer.Appendf( "LoadedModule[%u]=%hs\r\n", i, modulePathAscii );
					}
					else
					{
						RED_DBG_TRACE( "Warning: GetModuleFileNameExW failed for %p", modules[ i ] );
						writer.Appendf( "LoadedModule[%u]=<Unknown>\r\n", i );
					}
				}
				if( modulesTruncated )
				{
					writer.Append( "LoadedModule[...%u]=<Truncated>\r\n", ( sizeNeeded / sizeof( HMODULE ) ) - 1 );
				}
			}
			else
			{
				RED_DBG_TRACE( "EnumProcessModuleEx failed: 0x%08X", ::GetLastError() );
				writer.Append( "LoadedModule[?]=<Unknown>\r\n", ( sizeNeeded / sizeof( HMODULE ) ) - 1 );
			}
		}

		// EXE paths
		{
			char tmpBuf[ MAX_PATH ] = { '\0' };
			red::WideCharToStdChar_NoConv( tmpBuf, processExeNames.processExeName, RED_ARRAY_COUNT_U32( tmpBuf ) );
			writer.Appendf( "AppName=%hs\r\n", tmpBuf );
#if !defined( RED_CONFIGURATION_FINAL ) || defined( USE_PROFILER )
			red::WideCharToStdChar_NoConv( tmpBuf, processExeNames.processExeAbsolutePath, RED_ARRAY_COUNT_U32( tmpBuf ) );
			writer.Appendf( "AppPath=%hs\r\n", tmpBuf );
#endif
		}
	}
}

static void CloseTraceFile()
{
	// Consume the trace
	if( GTraceFile )
	{
		fflush( GTraceFile );
		fclose( GTraceFile );
		GTraceFile = nullptr;
	}
}

static void CreateReadyFile( const wchar_t* reportDirAbsolutePath )
{
	// Create the magic file that says the report is done
	HANDLE hReadyFile = INVALID_HANDLE_VALUE;
	wchar_t fileName[ MAX_PATH ] = { '\0' };
	red::SNPrintFUnsafe( fileName, MAX_PATH, L"%ls.ready", reportDirAbsolutePath );
	hReadyFile = ::CreateFileW( fileName, GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr );
	if( hReadyFile != INVALID_HANDLE_VALUE )
	{
		::SetFileAttributesW( fileName, FILE_ATTRIBUTE_HIDDEN );
	}
	::CloseHandle( hReadyFile );
}

static Bool GenerateOutOfProcessErrorReport( const IPCContext& ipcContext, HANDLE hThread, const wchar_t* reportDirAbsolutePath, const ProcessExeNames& processExeNames, const IPCArgs& ipcArgs, const StackTraceEx& stackTraceEx, const RegisteredAttachmentTable& registeredAttachmentTable, const ErrMsgArgs& errMsgArgs )
{
	red::Timer waitForLogTimer;

	WriteMiniDump( reportDirAbsolutePath, processExeNames, ipcArgs );

	Uint64 stackHash = 0;
#if !defined( RED_CONFIGURATION_FINAL )
	PrintStackTrace( reportDirAbsolutePath, errMsgArgs, stackTraceEx );

	stackHash = CalculateStackTraceHash( stackTraceEx );
#else
	PrintEmptyStackTrace( reportDirAbsolutePath, errMsgArgs );
#endif

	PrintWERReport( ipcContext, reportDirAbsolutePath, processExeNames, ipcArgs, errMsgArgs, stackHash );

	WaitForLogFlush( waitForLogTimer );

	CopyRegisteredAttachments( registeredAttachmentTable, reportDirAbsolutePath );

	// Launch shipping version of crash reporter
	if( !LaunchCrashReporter() )
	{
		// Try to launch development version of crash reporter
		LaunchErrorReportManager();
	}

	CloseTraceFile();

	CreateReadyFile( reportDirAbsolutePath );

	return true;
}

namespace ErrorReportServiceHelpers
{
	static void GenerateErrorReport( const IPCContext& ipcContext, HANDLE hThread, const wchar_t* reportDirAbsolutePath, const ProcessExeNames& processExeNames, const IPCArgs& ipcArgs, const StackTraceEx& stackTraceEx, const RegisteredAttachmentTable& registeredAttachmentTable, const ErrMsgArgs& errMsgArgs )
	{
		__try
		{
			// HWND can't be null
			if ( !::ShutdownBlockReasonCreate( ::GetConsoleWindow(), L"Generating error report") )
			{
				RED_DBG_TRACE( "ShutdownBlockReasonCreate HWND=%p failed: 0x%08X", ::GetConsoleWindow(), ::GetLastError() );
			}
			else
			{
				RED_DBG_TRACE( "ShutdownBlockReasonCreate succeeded" );
			}
			
			RED_DBG_TRACE( "Generating report...");
			GenerateOutOfProcessErrorReport( ipcContext, hThread, reportDirAbsolutePath, processExeNames, ipcArgs, stackTraceEx, registeredAttachmentTable, errMsgArgs );
			RED_DBG_TRACE( "Done!" );
		}
		__finally
		{
			(void)::ShutdownBlockReasonDestroy( ::GetConsoleWindow() );
		}
	}
}

static void GetProcessExeNames(HANDLE remoteProcess, ProcessExeNames& outNames)
{
	{
		DWORD size = RED_ARRAY_COUNT_U32(outNames.processExeAbsolutePath); // input is buffer size, output is strlen (excluding null terminator)
		if (!::QueryFullProcessImageNameW(remoteProcess, 0, outNames.processExeAbsolutePath, &size))
		{
			RED_DBG_TRACE("QueryFullProcessImageNameW failed: 0x%08X", ::GetLastError());
		}
		else
		{
			const wchar_t* lastSlash = red::StrchrR(outNames.processExeAbsolutePath, L'\\');
			const wchar_t* src = lastSlash ? lastSlash + 1 : outNames.processExeAbsolutePath;
			red::Strcpy(outNames.processExeName, src, RED_ARRAY_COUNT_U32(outNames.processExeName));
			red::Strcpy(outNames.processExeNameForPrefix, src, RED_ARRAY_COUNT_U32(outNames.processExeNameForPrefix));
			red::Strcpy(outNames.shortProcessExeNameForPrefix, src, RED_ARRAY_COUNT_U32(outNames.shortProcessExeNameForPrefix));

			// Truncate it at the first, even if not an extension. Just for display purposes.
			red::ReplaceChar(outNames.processExeNameForPrefix, RED_ARRAY_COUNT_U32(outNames.processExeNameForPrefix), L'.', L'\0');
			red::ReplaceChar(outNames.shortProcessExeNameForPrefix, RED_ARRAY_COUNT_U32(outNames.shortProcessExeNameForPrefix), L'.', L'\0');
		}
	}
}

static Bool TryCreateReportDir(HANDLE remoteProcess, Uint32 processID, Uint32 threadID, const ProcessExeNames& processExeNames, const wchar_t* reportQueueAbsolutePath, FileInfo& outReportDirAbsolutePath)
{
	SYSTEMTIME localTime;
	Uint64 utcTimestamp = 0;
	{
		SYSTEMTIME systemTime;
		::GetSystemTime(&systemTime);
		FILETIME fileTime;
		::SystemTimeToFileTime(&systemTime, &fileTime);
		ULARGE_INTEGER li;
		red::Memcpy(&li, &fileTime, sizeof(ULARGE_INTEGER));
		utcTimestamp = li.QuadPart;

		// How To Convert from GMT(UTC) Time to Local Time
		// https://support.microsoft.com/en-us/kb/245786
		FILETIME localFileTime;
		::FileTimeToLocalFileTime(&fileTime, &localFileTime);
		::FileTimeToSystemTime(&localFileTime, &localTime);
	}

	red::Memzero(&outReportDirAbsolutePath, sizeof(outReportDirAbsolutePath));
	// Create report subdirectory
	{
		if (!reportQueueAbsolutePath || !reportQueueAbsolutePath[0])
		{
			RED_DBG_TRACE("Invalid reportQueueAbsolutePath");
			return false;
		}

		const size_t pathLen = red::Strlen(reportQueueAbsolutePath, MAX_PATH);
		if (pathLen == MAX_PATH)
		{
			RED_DBG_TRACE("reportQueueAbsolutePath exceeded supported path size!");
			return false;
		}

		const Int32 len = red::SNPrintFUnsafe(outReportDirAbsolutePath.m_fileName, RED_ARRAY_COUNT_U32(outReportDirAbsolutePath.m_fileName), L"%ls\\%ls-%04d%02d%02d-%02d%02d%02d-%lu-%lu\\",
			reportQueueAbsolutePath, processExeNames.shortProcessExeNameForPrefix,
			localTime.wYear, localTime.wMonth, localTime.wDay,
			localTime.wHour, localTime.wMinute, localTime.wSecond, processID, threadID);
		red::ReplaceChar(outReportDirAbsolutePath.m_fileName, MAX_PATH, L'/', L'\\');

		if (!::CreateDirectoryW(outReportDirAbsolutePath.m_fileName, nullptr))
		{
			RED_DBG_TRACE("CreateDirectoryW failed to create '%ls': 0x%08X%hs", outReportDirAbsolutePath.m_fileName, ::GetLastError(),
				(::GetLastError() == ERROR_ALREADY_EXISTS ? " (ERROR_ALREADY_EXISTS). Failed to create unique directory for crash dump!!!" : ""));
			return false;
		}

		return true;
	}
}

static Bool ProcessErrorReportRequest( const IPCContext& ipcContext, const wchar_t* reportQueueAbsolutePath, Uint32 processID )
{
	ScopedSetEvent scopedSetEvent( ipcContext.m_hEventReportFinished );

	IPCArgs ipcArgs;
	const Uint32 expectedIPCArgsVersion = ipcArgs.m_ipcArgsVersion;
	if ( !ipcContext.m_ipcArgs.Read( ipcArgs ) )
	{
		RED_DBG_TRACE( "Failed to read IPC args!" );
		return false;
	}

	if ( ipcArgs.m_sizeofIPCArgs != sizeof( IPCArgs ) )
	{
		RED_DBG_TRACE( "IPCArgs sizeof mismatch: %lu vs expected %lu", ipcArgs.m_sizeofIPCArgs, sizeof( IPCArgs ) );
		return false;
	}
	else if ( ipcArgs.m_ipcArgsVersion != expectedIPCArgsVersion )
	{
		RED_DBG_TRACE( "IPCArgs version mismatch: %u vs expected %u", ipcArgs.m_ipcArgsVersion, expectedIPCArgsVersion );
		return false;
	}

	if ( processID != ipcArgs.m_processID )
	{
		RED_DBG_TRACE( "MonitorRemoteProcess unexpected processID %u", ipcArgs.m_processID );
		return false;
	}

	ProcessExeNames processExeNames;
	GetProcessExeNames(ipcContext.m_hRemoteProcess, processExeNames);

	FileInfo reportDirAbsolutePath;
	if (!TryCreateReportDir(ipcContext.m_hRemoteProcess, ipcArgs.m_processID, ipcArgs.m_threadID, processExeNames, reportQueueAbsolutePath, reportDirAbsolutePath))
	{
		RED_DBG_TRACE("TryCreateReportDir failed");
		return false;
	}

	//#tbd: THREAD_GET_CONTEXT | THREAD_SUSPEND_RESUME not enough to get threadID in stacktrace func, ironically we have the ID here
	// Get the thread for the minidump and stack trace
	const HANDLE hThread = ::OpenThread( THREAD_ALL_ACCESS, FALSE, ipcArgs.m_threadID );
	if ( !hThread )
	{
		RED_DBG_TRACE( "OpenThread <%u> failed: 0x%08X", ipcArgs.m_threadID, ::GetLastError() );
		return false;
	}

#if !defined( RED_CONFIGURATION_FINAL ) || defined( USE_PROFILER )
	// If trace not already enabled, trace into a file.
	// NOTE: file will be "consumed" and closed elsewhere before the ".ready" file is created. Abandoning it here.
	if ( !GEnableTrace )
	{
		FileInfo traceFileName;
		red::SNPrintFUnsafe( traceFileName.m_fileName, RED_ARRAY_COUNT_U32( traceFileName.m_fileName ), L"%lstracelog.txt", reportDirAbsolutePath.m_fileName );
		_wfopen_s(&GTraceFile, traceFileName.m_fileName, L"w"); // write raw, not as "wt"
	}
	if ( GTraceFile )
	{
		GEnableTrace = true;
		GTraceTimestamp = true;
		RED_DBG_TRACE("Trace log");
	}
#endif

	RegisteredAttachmentTable registeredAttachmentTable;
	GetRegisteredAttachmentsTable( ipcContext, ipcArgs, registeredAttachmentTable );

	// #tbd: Sanity check thread didn't crash and then some other process didn't create a thread with the same ID
	// Or inherit the process/thread handle and duplicate it.
	// 	const Uint32 threadProcID = ::GetProcessIdOfThread( hThread );	if ( threadProcID != ipcContext.m_ownerProcessID )

	ScopedHandle scopedThread( hThread );

	EXCEPTION_RECORD exceptionRecord;
	red::Memzero( &exceptionRecord, sizeof( exceptionRecord ) );
	StackTraceEx stackTraceEx;
	red::Memzero( &stackTraceEx, sizeof( stackTraceEx ) );
	{
		CONTEXT context;
		red::Memzero( &context, sizeof(context) );
		if ( ipcArgs.m_pRemoteExceptionPointers )
		{
			EXCEPTION_POINTERS ep;
			red::Memzero( &ep, sizeof( ep ) );
			if ( CopyToLocal( ipcContext.m_hRemoteProcess, &ep, ipcArgs.m_pRemoteExceptionPointers ) )
			{
				// Execption address offset from module	
				if ( !CopyToLocal( ipcContext.m_hRemoteProcess, &context, ep.ContextRecord ) )
				{
					RED_DBG_TRACE( "Failed to get remote thread context!" );
					red::Memzero( &context, sizeof( context ) );
				}
			}

			if ( !CopyToLocal( ipcContext.m_hRemoteProcess, &exceptionRecord, ep.ExceptionRecord ) )
			{
				RED_DBG_TRACE( "Failed to get remote thread exception record!" );
				red::Memzero( &exceptionRecord, sizeof( exceptionRecord ) );
			}

			StackBackTraceControl traceControl;
			traceControl.m_type = eStackBackTraceControlType_Context;
			traceControl.m_context = &context; // LOCAL context pointer, since stack trace isn't reading remote process memory
			traceControl.m_numAdditionalFramesToSkip = 0;
			if ( !GetStackBackTraceEx_HeavyWithInlines( reinterpret_cast<void*>( hThread ), traceControl, stackTraceEx, eConnection_RemoteProcess ) )
			{
				RED_DBG_TRACE( "Failed to get remote stack back trace!" );
			}
		}
	}

	{
		// #append to any custom or just change HandleError iface
		
		// #tbd: cache process name, in case terminated while trying to attach debugger and fail and need to reshow the dialog
		
		char dlgMessageBuf[ MSG_BUFFER_SIZE ] = { '\0' };
		char dlgCppFileBuf[ CPP_FILE_BUFFER_SIZE ] = { '\0' };
		char dlgExpressionBuf[ EXPR_BUFFER_SIZE ] = { '\0' };
		ErrMsgArgs msgArgs;
		if ( ipcArgs.m_errorReason == red::eErrorReason_UnhandledException )
		{
			RED_DBG_TRACE("UnhandledException: ExceptionCode=%u", exceptionRecord.ExceptionCode );
			FormatErrMsg_UnhandledException( ipcContext.m_hRemoteProcess, ipcArgs, exceptionRecord, stackTraceEx, dlgMessageBuf, dlgCppFileBuf, dlgExpressionBuf, msgArgs );
		}
		else
		{
			RED_DBG_TRACE("Custom dialog error");
			FormatErrMsg_CustomArgs( ipcContext.m_hRemoteProcess, ipcArgs, stackTraceEx, dlgMessageBuf, dlgCppFileBuf, dlgExpressionBuf, msgArgs );
		}

		static const char UNKNOWN_TXT[] = "<Unknown>";

		struct MessageSanitizer
		{
			static void Run( ErrMsgArgs& args )
			{
				DoString( &args.m_errorReason );
				DoString( &args.m_cppFile );
				DoString( &args.m_expression );
				DoString( &args.m_message );
			}

			static void DoString( const char** pStr )
			{
				if ( !*pStr || !*pStr[0] )
				{
					*pStr = UNKNOWN_TXT;
				}
			}
		};

		MessageSanitizer::Run( msgArgs );

		ErrorReportServiceHelpers::GenerateErrorReport( ipcContext, hThread, reportDirAbsolutePath.m_fileName, processExeNames, ipcArgs, stackTraceEx, registeredAttachmentTable, msgArgs );
	}

	return true;
}

Bool MonitorRemoteProcess( const IPCContext& ipcContext, const wchar_t* reportQueueAbsolutePath, Uint32 processID )
{
	if ( !ipcContext.m_isValid )
	{
		RED_DBG_TRACE( "Invalid error handler IPC" );
		return false;
	}

	if ( !SetThreadRemoteConnection( processID ) )
	{
		RED_DBG_TRACE( "Failed to open remote process connection!" );
		return false;
	}

	RED_DBG_TRACE( "Waiting for error report request...");
	if ( !WaitForErrorReportRequested( ipcContext ) )
	{
		return false;
	}

	RED_DBG_TRACE( "Request received!" );

	// #tbd: bit risky accepting WAIT_ABANDONED, but currently should only happen if the editorLauncher crashed while
	// checking if it can terminate the launcher.
	// #tbd: also a bit risky waiting INFINITE, but if debugbreak the editorLauncher miraculously before it can
	// release the mutex right after acquiring it.
	Bool ret = false;
	for(;;)
	{
		const Uint32 waitRet = ::WaitForSingleObject( ipcContext.m_hReporterAccessMutex, 1000 );
		if ( waitRet == WAIT_OBJECT_0 || waitRet == WAIT_ABANDONED )
		{
			__try
			{
				ret = ProcessErrorReportRequest( ipcContext, reportQueueAbsolutePath, processID );
			}
			__finally
			{
				::ReleaseMutex( ipcContext.m_hReporterAccessMutex );
			}
			break;
		}
		else if ( waitRet == WAIT_TIMEOUT )
		{
			RED_DBG_TRACE( "Timed out waiting to lock IPC access mutex" );
		}
		else
		{
			RED_DBG_TRACE( "Failed to lock IPC access mutex: 0x%08X", ::GetLastError() );
			break;
		}
	}

	return ret;
}

// NOTE: Be mindful of the stack in this function; it's attempted during stack overflow (with SEH __try/__except just in case).
Bool WakeOutOfProcessErrorReporter( const IPCContext& ipcContext, const ErrorReporterArgs& args, _EXCEPTION_POINTERS*	exceptionInfo )
{
	// #tbd: detect if error reporter itself crashed and possibly try to relaunch. Or have the crash reporter or WER handle that.
	const Uint32 processID = ::GetCurrentProcessId();
	const Uint32 threadID = ::GetCurrentThreadId();

	if ( !ipcContext.m_isValid )
	{
		return false;
	}

	static IPCArgs ipcArgs;
	{
		ipcArgs.m_pRemoteExceptionPointers = exceptionInfo;
		ipcArgs.m_pRemoteRegisteredAttachmentTable = args.m_pAttachmentTable;
		if ( args.m_pCustomErrMsg )
		{
			__try
			{
				IPCErrMsgArgs& msgArgs = ipcArgs.m_ipcErrMsgArgs;
				msgArgs.m_remoteCppFile = args.m_pCustomErrMsg->m_file;
				msgArgs.m_cppFileSize = args.m_pCustomErrMsg->m_file ? static_cast< Uint32 >( red::Strlen( args.m_pCustomErrMsg->m_file ) ) + 1 : 0 ;
				msgArgs.m_remoteExpression = args.m_pCustomErrMsg->m_expression;
				msgArgs.m_expressionSize = args.m_pCustomErrMsg->m_expression ? static_cast< Uint32 >( red::Strlen( args.m_pCustomErrMsg->m_expression ) ) + 1 : 0;
				msgArgs.m_remoteMessage = args.m_pCustomErrMsg->m_message;
				msgArgs.m_messageSize = args.m_pCustomErrMsg->m_message ? static_cast< Uint32 >( red::Strlen( args.m_pCustomErrMsg->m_message ) ) + 1 : 0;
				msgArgs.m_line = args.m_pCustomErrMsg->m_line;
			}
			__except( EXCEPTION_EXECUTE_HANDLER )
			{
				ipcArgs.m_ipcErrMsgArgs = IPCErrMsgArgs();
			}
		}
		else
		{
			ipcArgs.m_ipcErrMsgArgs = IPCErrMsgArgs();
		}
		ipcArgs.m_processID = processID;
		ipcArgs.m_threadID = threadID;
		ipcArgs.m_errorReason = args.m_errorReason;
		ipcArgs.m_remoteAppVersionNumber = args.m_appVersionNumber;
		ipcArgs.m_remoteAppVersionNumberCount = args.m_appVersionNumber ? static_cast< Uint32 >( red::Strlen( args.m_appVersionNumber ) ) + 1 : 0;
	}

	if ( !ipcContext.m_ipcArgs.Write( ipcArgs ) )
	{
		RED_DBG_TRACE( "Failed to write IPC args!" );
		return false;
	}

	if ( !WakeErrorReporter( ipcContext ) )
	{
		return false;
	}

	return true;
}

Bool WaitForOutOfProcessErrorReporter(const IPCContext& ipcContext)
{
	if (!WaitForErrorReportFinished(ipcContext))
	{
		return false;
	}

	// Should have already been reset by the error reporter, but make sure for recoverable errors
	::ResetEvent(ipcContext.m_hEventWakeUpErrorReportProcess);

	// Set by the error reporter, cleared by the requestor		
	::ResetEvent(ipcContext.m_hEventReportFinished);

	return true;
}

Bool IsAttachedToProcess( Uint32 processID )
{
	dbgutils::win32::IPCContext ipcContext;
	if ( !dbgutils::win32::OpenIPC( processID, dbgutils::win32::eIPCOpenParam_ErrorReporter, ipcContext ) )
	{
		RED_DBG_TRACE( "Failed to open %u", processID );
		return false;
	}

	const Uint32 waitRet = ::WaitForSingleObject( ipcContext.m_hReporterAccessMutex, 0 );
	Bool ret = false;
	if ( waitRet == WAIT_TIMEOUT )
	{
		// Something has the mutex. If the reporter crashed, we'd get WAIT_ABANDONED
		ret = true;
	}
	else if ( waitRet == WAIT_OBJECT_0 || waitRet == WAIT_ABANDONED )
	{
		::ReleaseMutex( ipcContext.m_hReporterAccessMutex );
	}

	dbgutils::win32::CloseIPC( ipcContext );

	return ret;
}

//#todo: make common, farm out opening IPC
Int32 ErrorReporterMainLoop( const char* connectionString )
{
	GIsErrorReporter = true;

	Uint32 processID = 0;
	if ( !red::StringToInt( processID, connectionString, nullptr, red::BaseTen ) || processID == 0 )
	{
		RED_DBG_TRACE( "Failed to parse process ID from cmdLine '%hs'", connectionString );
		return 1;
	}

	RED_DBG_TRACE( "Parsed process ID %u", processID );

	dbgutils::win32::IPCContext ipcContext;
	if ( !dbgutils::win32::OpenIPC( processID, dbgutils::win32::eIPCOpenParam_ErrorReporter, ipcContext ) )
	{
		RED_DBG_TRACE( "Failed to open %u", processID );
		return 1;
	}

	wchar_t localQueuePath[ MAX_PATH ] = { L'\0' };
	{
		wchar_t* appDataDir = nullptr;
		if ( SUCCEEDED( ::SHGetKnownFolderPath( FOLDERID_LocalAppData, KF_FLAG_DEFAULT, nullptr, &appDataDir ) ) )
		{
			red::Strcpy( localQueuePath, appDataDir, MAX_PATH );
			red::Strcat( localQueuePath, REPORT_QUEUE_SUB_PATH, MAX_PATH );
			::CoTaskMemFree( appDataDir );
			appDataDir = nullptr;
			::SHCreateDirectory( nullptr, localQueuePath );
			// Try to let the separate error report uploader process the report queue and move it into the archives folder.
			// Otherwise people could start opening the files up and lock them.
			::SetFileAttributesW( localQueuePath, FILE_ATTRIBUTE_HIDDEN );
		}
		else
		{
			RED_DBG_TRACE( "Failed to get LOCALAPPDATA folder!");
		}
	}

	RED_DBG_TRACE( "Monotoring remote process" );
	for(;;)
	{
		if ( !dbgutils::win32::MonitorRemoteProcess( ipcContext, localQueuePath, processID ) )
		{
			break;
		}
	}
	RED_DBG_TRACE( "Monitoring remote process finished. Shutting down crash reporter..." );

	return 0;
}

} // win32
} // dbgutils

#endif // RED_PLATFORM_WINPC
