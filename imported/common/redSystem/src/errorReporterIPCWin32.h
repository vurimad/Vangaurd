#include "utility.h"

#ifdef RED_PLATFORM_WINPC
namespace red
{
	enum EErrorReason : Uint8;
}

namespace dbgutils
{

struct RegisteredAttachmentTable;

namespace win32
{
namespace impl
{
	struct FileMappingInfo
	{
		HANDLE			m_hMapFile;
		mutable void*	m_pMappedView;
	};

	extern Bool CopyToRemote( HANDLE remoteProcess, void* __restrict pOutRemoteValue, const void* __restrict pLocalValue, size_t bytesToCopy );
	extern Bool CopyToLocal( HANDLE remoteProcess, void* __restrict pOutLocalValue, const void* __restrict pRemoteValue, size_t bytesToCopy );
	extern Bool CopyArrayToLocal( HANDLE remoteProcess, void* __restrict outLocalArray, Uint32 localArrayCount, const void* __restrict remoteArray, Uint32 remoteArrayCount, size_t elementSize, Uint32& outNumElementsCopied );
	extern Bool CopyStringToLocal( HANDLE remoteProcess, void* __restrict localDst, Uint32 localCount, const void* __restrict remoteSrc, Uint32 remoteCount, size_t elementSize );
	extern Bool CopyStringToRemote( HANDLE remoteProcess, void* __restrict remoteDst, Uint32 remoteCount, const void* __restrict localSrc, Uint32 localCount, size_t elementSize );
	extern Bool OpenIPCFileMapping( const wchar_t* mappingName, Uint32 size, Uint8 openFlags, FileMappingInfo& outFileMapping );
	extern void CloseIPCFileMapping( FileMappingInfo& fileMapping );
}

///---

enum EFileMappingFlags : Uint8
{
	eFileMappingFlag_Read		= 0,
	eFileMappingFlag_Write		= RED_FLAG(0),
	eFileMappingFlag_Existing	= RED_FLAG(1),
	eFileMappingFlag_ReadWrite = eFileMappingFlag_Read | eFileMappingFlag_Write,
};

template< typename TData >
class FileMapping
{
	static_assert( std::is_trivially_copyable< TData >::value, "Filemapping data should be memcpy'able");

public:
	FileMapping()
		: m_flags( 0 )
	{
		red::Memzero( &m_fileMapping, sizeof( m_fileMapping ) );
	}

	~FileMapping()
	{
		Close();
	}

	Bool Open( const wchar_t* mappingName, Uint8 openFlags )
	{
		if ( m_fileMapping.m_pMappedView )
		{
			RED_DBG_TRACE( "File mapping already open!");
			return false;
		}

		if ( !impl::OpenIPCFileMapping( mappingName, sizeof( TData ), openFlags, m_fileMapping ) )
		{
			return false;
		}
		m_flags = openFlags;
		return true;
	}

	void Close()
	{
		impl::CloseIPCFileMapping( m_fileMapping );
		m_flags = 0;
	}

	Bool Read( TData& outData ) const
	{
		if ( !m_fileMapping.m_pMappedView )
		{
			return false;
		}

		red::Memcpy( &outData, m_fileMapping.m_pMappedView, sizeof( TData ) );
		return true;
	}

	Bool Write( const TData& data ) const
	{
		if ( !m_fileMapping.m_pMappedView || (m_flags & eFileMappingFlag_Write ) == 0 )
		{
			return false;
		}

		red::Memcpy( m_fileMapping.m_pMappedView, &data, sizeof( TData ) );
		return true;
	}

private:
	impl::FileMappingInfo	m_fileMapping;
	Uint8					m_flags;
};

///---

struct IPCErrMsgArgs
{
	IPCErrMsgArgs()
		: m_remoteCppFile( nullptr )
		, m_remoteExpression( nullptr )
		, m_remoteMessage( nullptr )
		, m_line( 0 )
		, m_cppFileSize( 0 )
		, m_expressionSize( 0 )
		, m_messageSize( 0 )
	{}

	const char*				m_remoteCppFile;
	const char*				m_remoteExpression;
	const char*				m_remoteMessage;
	Uint32					m_line;

	Uint32					m_cppFileSize;
	Uint32					m_expressionSize;
	Uint32					m_messageSize;
};

//#tbd: could create templated pointer wrapper that also stores type size, so can do size compare for each member a little more easily
// Note: can use ::ReadProcessMemory()/::WriteProcessMemory() and pass in a pointer to data
// instead of allocating a larger structure. The pointer must of course remain valid.
// Just make sure to sanitize data! E.g., don't assume strings are null terminated or a certain size; maybe they got corrupted.
struct IPCArgs
{
	IPCArgs();

	// Size and version fields must come first
	size_t								m_sizeofIPCArgs;
	Uint32								m_ipcArgsVersion;
	
	const EXCEPTION_POINTERS*			m_pRemoteExceptionPointers;
	const RegisteredAttachmentTable*	m_pRemoteRegisteredAttachmentTable;
	IPCErrMsgArgs						m_ipcErrMsgArgs;
	const char*							m_remoteAppVersionNumber;
	Uint32								m_remoteAppVersionNumberCount;
	Uint32								m_processID;
	Uint32								m_threadID;
	red::EErrorReason					m_errorReason;
};

struct ReporterInfo
{
	ReporterInfo();

	// Size and version fields must come first
	size_t	m_sizeofReporterInfo;
	Uint32	m_reporterInfoVersion;

	HANDLE	m_hReporterProc;
	Bool	m_isActive;
};

struct IPCContext
{
	IPCContext()
		: m_ipcArgs()
		, m_reportInfo()
		, m_hEventReportFinished( nullptr )
		, m_hEventWakeUpErrorReportProcess( nullptr )
		, m_hReporterAccessMutex( nullptr )
		, m_hRemoteProcess( nullptr )
		, m_ownerProcessID( 0 )
		, m_isValid( false )
	{}

	FileMapping<IPCArgs>			m_ipcArgs;
	FileMapping<ReporterInfo>		m_reportInfo;
	HANDLE							m_hEventReportFinished;
	HANDLE							m_hEventWakeUpErrorReportProcess;
	HANDLE							m_hReporterAccessMutex;
	HANDLE							m_hRemoteProcess;
	Uint32							m_ownerProcessID;
	Bool							m_isValid;

private:
	IPCContext( const IPCContext& );
	void operator=( IPCContext& );
};

///---

template< typename T> RED_INLINE
Bool CopyToRemote( HANDLE remoteProcessHandle, T* __restrict pOutRemoteValue, const T* __restrict  pLocalValue )
{
	static_assert( std::is_trivially_copyable< T >::value, "Should be memcpy'able" );
	return impl::CopyToRemote( remoteProcessHandle, pOutRemoteValue, pLocalValue, sizeof( T ) );
}

template< typename T> RED_INLINE
Bool CopyToLocal( HANDLE remoteProcess, T* __restrict  pOutLocalValue, const T* __restrict  pRemoteValue)
{
	static_assert( std::is_trivially_copyable< T >::value, "Should be memcpy'able" );
	return impl::CopyToLocal( remoteProcess, pOutLocalValue, pRemoteValue, sizeof( T ) );
}

// Size is buffer count including  null terminator
template< typename T > RED_INLINE
Bool CopyArrayToLocal( HANDLE remoteProcess, T* __restrict outLocalArray, Uint32 localArrayCount, const T* __restrict  remoteArray, Uint32 remoteArrayCount, Uint32& outNumElementsCopied )
{
	static_assert( std::is_trivially_copyable< T >::value, "Should be memcpy'able" );
	return impl::CopyArrayToLocal( remoteProcess, outLocalArray, localArrayCount, remoteArray, remoteArrayCount, sizeof(T), outNumElementsCopied );
}

RED_INLINE
Bool CopyStringToLocal( HANDLE remoteProcess, wchar_t* __restrict localDst, Uint32 localCount, const wchar_t* __restrict remoteSrc, Uint32 remoteCount )
{
	return impl::CopyStringToLocal( remoteProcess, localDst, localCount, remoteSrc, remoteCount, sizeof(wchar_t) );
}

RED_INLINE
Bool CopyStringToLocal( HANDLE remoteProcess, char* __restrict localDst, Uint32 localCount, const char* __restrict remoteSrc, Uint32 remoteCount )
{
	return impl::CopyStringToLocal( remoteProcess, localDst, localCount, remoteSrc, remoteCount, sizeof(char)/*=1*/ );
}

RED_INLINE
Bool CopyStringToRemote( HANDLE remoteProcess, wchar_t* __restrict remoteDst, Uint32 remoteCount, const wchar_t* __restrict localSrc, Uint32 localCount )
{
	return impl::CopyStringToRemote( remoteProcess, remoteDst, remoteCount, localSrc, localCount, sizeof(wchar_t) );
}

RED_INLINE
Bool CopyStringToRemote( HANDLE remoteProcess, char* __restrict remoteDst, Uint32 remoteCount, const char* __restrict localSrc, Uint32 localCount )
{
	return impl::CopyStringToRemote( remoteProcess, remoteDst, remoteCount, localSrc, localCount, sizeof(char)/*=1*/ );
}

///---

template< typename CH >
class RemoteStringBufferWithTruncation : red::NonCopyable
{
public:
	RED_INLINE RemoteStringBufferWithTruncation( HANDLE hRemoteProcess, CH* remoteDst, Uint32 remoteCount )
		: m_hRemoteProcess( hRemoteProcess )
		, m_remoteDst( remoteDst )
		, m_remoteCount( remoteCount )
	{}

	RED_INLINE Bool Write( const CH* localString )
	{
		if ( m_errorFlag )
		{
			return false;
		}
		if ( !localString )
		{
			return false;
		}
		if ( m_remoteCount < 1 )
		{
			// Full, truncate
			return true;
		}
		const Uint32 localLen = static_cast< Uint32 >( red::Strlen( localString ) );
		Uint32 countToWrite = localLen + 1;
		if ( countToWrite > m_remoteCount )
		{
			countToWrite = m_remoteCount;
		}
		if ( !CopyStringToRemote( m_hRemoteProcess, m_remoteDst, m_remoteCount, localString, countToWrite ) )
		{
			m_errorFlag = true;
			return false;
		}

		// Back one so next append will write over the null terminator
		m_remoteDst += countToWrite - 1;
		m_remoteCount -= countToWrite - 1;

		return true;
	}

private:
	HANDLE				m_hRemoteProcess;
	CH*					m_remoteDst;
	Uint32				m_remoteCount;
	Bool				m_errorFlag;
};

///---

struct ScopedSetEvent
{
public:
	explicit ScopedSetEvent( HANDLE hEvent );
	~ScopedSetEvent();

private:
	void operator=( const ScopedSetEvent& );
	ScopedSetEvent( const ScopedSetEvent& );

private:
	HANDLE m_hEvent;
};

struct ScopedHandle
{
public:
	explicit ScopedHandle( HANDLE hHandle );
	~ScopedHandle();

private:
	void operator=( const ScopedHandle& );
	ScopedHandle( const ScopedHandle& );

private:
	HANDLE m_hHandle;
};

//---

enum EIPCOpenParam : Uint8
{
	eIPCOpenParam_ErrorReportee,
	eIPCOpenParam_ErrorReporter,
};

//---

extern Bool OpenIPC( Uint32 processID, EIPCOpenParam param, IPCContext& outIPCContext );
extern void CloseIPC( IPCContext& ipcContext );
extern Bool ConnectErrorReporterIfNeeded_NotReentrant( IPCContext& ipcContext );
extern Bool WakeErrorReporter( const IPCContext& ipcContext );
extern Bool WaitForErrorReportFinished( const IPCContext& ipcContext );
extern Bool WaitForErrorReportRequested( const IPCContext& ipcContext );

// BM BS since it does a DLL build of this...
extern REDSYSTEM_API Bool LaunchErrorReportManager();

// Launches shipping version of error reporter manger.
extern REDSYSTEM_API Bool LaunchCrashReporter();

} // win32
} // dbgutils
#endif // RED_PLATFORM_WINPC
