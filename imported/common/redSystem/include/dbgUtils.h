/**
* Copyright (c) 2016 CDProjekt Red, Inc. All Rights Reserved.
*/

#pragma once

namespace red
{
	enum EErrorReason : Uint8;
}

namespace dbgutils
{
	struct Address
	{
		Uint64 m_absoluteVirtualAddress;
		Uint64 m_moduleBaseAbsoluteVirtualAddress;
	};

	struct AddressEx : public Address
	{
		Uint32 m_inlineFrameContext;
	};

	enum { MAX_STACK_TRACE_FRAMES = 128 };
	struct StackTrace
	{
		Address	m_frameAddress[ MAX_STACK_TRACE_FRAMES ];
		Uint32	m_numFrameAddresses;
	};

	struct StackTraceEx
	{
		AddressEx	m_frameAddress[ MAX_STACK_TRACE_FRAMES ];
		Uint32		m_numFrameAddresses;
	};

	struct SymbolInfo
	{
		enum { MAX_SYMBOL_LEN = 260 };

		char	m_name[ MAX_SYMBOL_LEN ];
		Uint64	m_displacement;
		Uint32	m_nameLength;
	};

	struct ModuleInfo
	{
		enum { MAX_SYMBOL_LEN = 64 };

		char m_name[ MAX_SYMBOL_LEN ];
		Uint32 m_nameLength;
	};

	struct LineInfo
	{
		enum { MAX_FILE_LEN = 260 };

		char	m_fileName[ MAX_FILE_LEN ];
		Uint32	m_fileNameLength;
		Uint32	m_lineNumber;
		Uint32	m_lineDisplacement;
	};

	struct FileInfo
	{
		enum { MAX_FILE_LEN = 260 };
		wchar_t m_fileName[ MAX_FILE_LEN ];
	};

	struct TypeInfo
	{
		TypeInfo() : m_name{ 0 } {}

		AnsiChar m_name[ 512 ];			// Type name
		Uint32 m_size = 0;				// Type size
		Bool m_primitiveType = false;	// Is primitive type ( int, float, double, etc.)
		Bool m_enumType = false;		// Is enum type
		Bool m_arrayType = false;		// Is C array type ( [] )
		Uint32 m_pointerCount = 0;		// How many * are there
		Uint32 m_arrayLength = 0;		// In bytes
	};

	struct ClassInfo
	{
		struct BaseClassInfo
		{
			BaseClassInfo() : m_name{ 0 }, m_type{ 0 } {}

			Uint32 m_offset = 0;
			AnsiChar m_name[ 512 ];
			AnsiChar m_type[ 512 ];
			Bool m_isVirtual = false;
		};

		struct MemberInfo
		{
			MemberInfo() : m_name{ 0 } {}

			Uint32 m_offset = 0;
			AnsiChar m_name[ 512 ];
			TypeInfo m_typeInfo;
		};

		static constexpr Uint32 MAX_BASE_CLASSES_COUNT = 32;
		static constexpr Uint32 MAX_MEMBERS_COUNT = 512;

		Uint32 m_baseClassesCount = 0;
		BaseClassInfo m_baseClasses[ MAX_BASE_CLASSES_COUNT ];

		Uint32 m_membersCount = 0;
		MemberInfo m_members[ MAX_MEMBERS_COUNT ];
	};


	struct StackInfo
	{
		ModuleInfo m_moduleInfo;
		SymbolInfo m_symbolInfo;
		LineInfo m_lineInfo;
		Bool m_hasValidModuleInfo;
		Bool m_hasValidSymbolInfo;
		Bool m_hasValidLineInfo;
		Bool m_isInlineFrame;

		REDSYSTEM_API const AnsiChar* GetModuleName() const;
		REDSYSTEM_API const AnsiChar* GetSymbolName() const;
		REDSYSTEM_API const Uint64 GetSymbolDisplacement() const;
		REDSYSTEM_API const AnsiChar* GetLineFileName() const;
		REDSYSTEM_API const Uint32 GetLineNumber() const;
		REDSYSTEM_API const AnsiChar* GetInlinePrefix() const;
	};

	struct REDSYSTEM_API ErrMsgArgs
	{
		RED_FORCE_INLINE ErrMsgArgs()
		{
			red::Memzero( this, sizeof(*this) );
		}

		const char*			m_errorReason;
		const char*			m_cppFile;
		const char*			m_expression;
		const char*			m_message;
		Uint32				m_line;
	};

	struct REDSYSTEM_API MiniDumpArgs
	{
		RED_FORCE_INLINE MiniDumpArgs()
			: m_context( nullptr )
			, m_threadID( 0 )
		{}

		const void*				m_context;
		Uint32					m_threadID;
	};

	// Char defined per platform to avoid character conversions during crashes
	struct REDSYSTEM_API RegisteredAttachmentEntry
	{
		enum { MaxPath = 260 };
#if defined( RED_PLATFORM_WINPC ) || defined( RED_PLATFORM_DURANGO )
		wchar_t m_absolutePath[ MaxPath ];
#elif defined( RED_PLATFORM_ORBIS )
		char m_absolutePath[ MaxPath ];
#elif defined( RED_PLATFORM_LINUX )
		char m_absolutePath[ MaxPath ];
#else
#error Unsupported platform!
#endif
	};

	struct REDSYSTEM_API RegisteredAttachmentTable
	{
		static const Uint32			MAX_FILE_ATTACHMENTS = 16;
#if defined( RED_PLATFORM_WINPC ) || defined( RED_PLATFORM_LINUX )
		static const Uint32 MAX_ATTACH_FILE_SIZE = 256 * 1024 * 1024; // #tbd: max log file size after spamming all day long
#else
		static const Uint32 MAX_ATTACH_FILE_SIZE = 1 * 1024 * 1024; // #tbd: can't really enforce, but PS4 will silently fail
#endif

		RegisteredAttachmentEntry	m_registeredAttachments[ MAX_FILE_ATTACHMENTS ];
		Uint32						m_numRegisteredAttachments;
		Uint32						m_numFailedToRegister;
	};

	enum EStackBackTraceControlType : Uint8
	{
		eStackBackTraceControlType_Default,		//!< Only additional frames to skip is used.
		eStackBackTraceControlType_Context,		//!< Use the context parameter
		eStackBackTraceControlType_Unwind,		//!< Use the unwind addresss parameter
	};

	struct REDSYSTEM_API StackBackTraceControl
	{
		RED_FORCE_INLINE StackBackTraceControl()
		{
			red::Memzero( this, sizeof(*this) );
			m_type = eStackBackTraceControlType_Default;
		}

		union
		{
			//! Internally defined context which contains needed backtrace info.
			//! On Win32, the CONTEXT pointer of an unhandled SEH exception.
			void*	m_context;

			//!< Start the stack trace from this address. E.g., from the return address of an error handler.
			Uint64	m_unwindToAbsoluteVirtualAddress;
		};

		//! Number of callstack frames to skip, after unwind address or context.
		Uint32 m_numAdditionalFramesToSkip;

		//! Control type
		EStackBackTraceControlType m_type;
	};

	enum EConnection
	{
		eConnection_LocalProcess,
		eConnection_RemoteProcess,
	};

	REDSYSTEM_API Bool IsTraceEnabled();
	REDSYSTEM_API void Trace( STATIC_CHECK_PRINTF_MSC const char* msg, ... );
	REDSYSTEM_API void VTrace( STATIC_CHECK_PRINTF_MSC const char* msg, va_list arglist );

	REDSYSTEM_API Bool SetThreadRemoteConnection( Uint32 processID, Bool force = false );
	REDSYSTEM_API Bool CloseThreadRemoteConnection();
	REDSYSTEM_API Bool IsDebuggerAttached( Bool* pOutIsConnectionValid = nullptr, EConnection conn = eConnection_LocalProcess );
		
	// #tbd: Support? On Win32 this spawns a new thread on the remote process and calls debugbreak from that.
	//REDSYSTEM_API Bool TriggerDebugBreak( Connection conn = Connection() );
	
	REDSYSTEM_API Bool GetSymbolInfo( const Address& frameAddress, SymbolInfo& outInfo, EConnection conn = eConnection_LocalProcess );
	REDSYSTEM_API Bool GetLineInfo( const Address& frameAddress, LineInfo& outInfo, EConnection conn = eConnection_LocalProcess );
	REDSYSTEM_API Bool GetModuleInfo( const Address& frameAddress, ModuleInfo& outInfo, EConnection conn = eConnection_LocalProcess );

	REDSYSTEM_API Bool WriteMiniDumpFile( const FileInfo& fileName, const MiniDumpArgs& args, EConnection conn = eConnection_LocalProcess );
	
	// Slow but reliable for error callstack hashes. #todo: add a fast version for tracing (e.g., RtlCaptureStackBackTrace ).
	// #tbd: could use the lighter version if also need to do in-process crash trace; full trace is riskier.
	REDSYSTEM_API Bool GetStackBackTrace_Heavy( void* osThreadHandle, const StackBackTraceControl& control, StackTrace& outStackTrace, EConnection conn = eConnection_LocalProcess );

#if defined( RED_PLATFORM_WINPC )
	REDSYSTEM_API Bool GetInlineSymbolInfo( const AddressEx& frameAddressEx, SymbolInfo& outInfo, EConnection conn = eConnection_LocalProcess );
	REDSYSTEM_API Bool GetInlineLineInfo( const AddressEx& frameAddressEx, LineInfo& outInfo, EConnection conn = eConnection_LocalProcess );

	REDSYSTEM_API Bool GetStackBackTraceEx_HeavyWithInlines( void* osThreadHandle, const StackBackTraceControl& control, StackTraceEx& outStackTraceEx, EConnection conn = eConnection_LocalProcess );
	REDSYSTEM_API Bool GetStackBackTraceFromEx( StackTrace& outStackTrace, const StackTraceEx& inStackTraceEx );

	REDSYSTEM_API Bool GetStackBackTrace_Lite( Uint32 numFramesToCapture, Uint32 numFramesToSkip, StackTrace& outStackTrace, Bool resolveModuleBaseAddresses = true, EConnection conn = eConnection_LocalProcess );

	REDSYSTEM_API Bool GettModuleBaseAddr_RemoteVM( Address& address, EConnection conn = eConnection_LocalProcess );
#endif

	REDSYSTEM_API Bool GetStackBackTrace_Profiler( Uint32 numFramesToSkip, StackTrace& outStackTrace );
	REDSYSTEM_API Bool ModularizeStackBackTrace( StackTrace& inOutStackTrace );
	
	// Prints as much as possible of the stacktrace into the given buffer, truncating.
	REDSYSTEM_API void PrintStackTraceToBuffer( const dbgutils::StackTrace& stackTrace, char* buf, size_t bufSize, EConnection conn = eConnection_LocalProcess );
	REDSYSTEM_API void PrintStackTraceExToBuffer( const dbgutils::StackTraceEx& stackTraceEx, char* buf, size_t bufSize, EConnection conn = eConnection_LocalProcess );

	// Prints line by line using the supplied callback function.
	typedef Int32 (*PrintStrackTraceLineCallback)( void* callbackUserData, STATIC_CHECK_PRINTF_MSC const AnsiChar* format, ... );
	REDSYSTEM_API void PrintStackTraceLineByLine( const dbgutils::StackTrace& stackTrace, PrintStrackTraceLineCallback callback, void* callbackUserData, EConnection conn = eConnection_LocalProcess );
	REDSYSTEM_API void PrintJumpToLineFriendlyStackTraceLineByLine( const dbgutils::StackTrace& stackTrace, PrintStrackTraceLineCallback callback, void* callbackUserData, EConnection conn = eConnection_LocalProcess );
	
	REDSYSTEM_API void PrintStackTraceExLineByLine( const dbgutils::StackTraceEx& stackTraceEx, PrintStrackTraceLineCallback callback, void* callbackUserData, EConnection conn = eConnection_LocalProcess );


	// Process line by line using the supplied callback function.
	typedef void( *ProcessStrackTraceLineCallback )( void* callbackUserData, Uint32 idx, const Address& frameAddress, const StackInfo& stackInfo );
	REDSYSTEM_API void ProcessStackTraceLineByLine( const dbgutils::StackTrace& stackTrace, ProcessStrackTraceLineCallback callback, void* callbackUserData, EConnection conn = eConnection_LocalProcess );
	REDSYSTEM_API void ProcessStackTraceExLineByLine( const dbgutils::StackTraceEx& stackTraceEx, ProcessStrackTraceLineCallback callback, void* callbackUserData, EConnection conn = eConnection_LocalProcess );

#if 0
	enum EStackHashType
	{
		eStackHashType_SymbolName,							//!< Hash based on symbol names only (case preserving), without the module
		//eStackHashType_SymbolVirtualAddress,				//!< Hash based on symbol virtual address; ASLR dependent so only useful for runtime
		//eStackHashType_ModuleNameAndSymbolRelativeOffset,	//!< Hash on module name (lowercased) and symbol relative offset to module base address so ASLR independent.
		//...
	};
#endif

	REDSYSTEM_API Bool GenerateHashForStackBackTrace( const dbgutils::StackTrace& stackTrace, Uint64& outHash, dbgutils::EConnection conn );

	REDSYSTEM_API void GetClassInformation( const char* const classTypeName, ClassInfo& classInfo );
	REDSYSTEM_API void GetTypeInformation( const char* const typeName, TypeInfo& typeInfo );
}

#define RED_DBG_TRACE( msg, ... ) do { if ( ::dbgutils::IsTraceEnabled() ) { ::dbgutils::Trace( msg, ##__VA_ARGS__ ); } } while ( 0, 0 )
#define RED_DBG_VTRACE( msg, arglist ) do { if ( ::dbgutils::IsTraceEnabled() ) { ::dbgutils::VTrace( msg, arglist ); } } while ( 0, 0 )
