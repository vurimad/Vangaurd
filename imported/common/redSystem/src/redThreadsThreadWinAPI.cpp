/**
* Copyright (c) 2013 CD Projekt Red. All Rights Reserved.
*/
#include "build.h"
#include "redThreadsPlatform.h"

#if defined( RED_THREADS_PLATFORM_WINDOWS_API )

//#include "redThreadsThread.h"

#include "redThreadsThreadWinAPI.h"

#include <process.h>
#include <combaseapi.h>

namespace red { namespace WinAPI {

	namespace helper
	{
		// Windows 10 API. For use in viewing thread names in PIX for Windows.
		static void SetCurrentThreadNameInPIX( const AnsiChar* threadName )
		{
			wchar_t wideThreadName[ 64 ] = L"";
			red::StdCharToWideChar_NoConv( wideThreadName, threadName, RED_ARRAY_COUNT_U32( wideThreadName ) );
#if defined( RED_PLATFORM_WINPC )
			typedef HRESULT( WINAPI *SetThreadDescriptionPROC )( HANDLE hThread, PCWSTR lpThreadDescription );

			// Thread-safe C++11 static
			static const SetThreadDescriptionPROC s_pfSetThreadDescription = reinterpret_cast<SetThreadDescriptionPROC>( ::GetProcAddress( ::GetModuleHandle( L"Kernel32.dll" ), "SetThreadDescription" ) );
			if ( s_pfSetThreadDescription )
			{
				s_pfSetThreadDescription( ::GetCurrentThread(), wideThreadName );
			}
#elif defined( RED_PLATFORM_DURANGO )
			::SetThreadName( ::GetCurrentThread(), wideThreadName );
#endif
		}

		static void SetThreadNameInDebugger( const AnsiChar* threadName )
		{
#pragma pack(push,8)
			typedef struct tagTHREADNAME_INFO
			{
				DWORD dwType;		// Must be 0x1000.
				LPCSTR szName;		// Pointer to name (in user addr space).
				DWORD dwThreadID;	// Thread ID (-1=caller thread).
				DWORD dwFlags;		// Reserved for future use, must be zero.
			} THREADNAME_INFO;
#pragma pack(pop)

			if ( !threadName )
			{
				return;
			}

			THREADNAME_INFO info;
			info.dwType = 0x1000;
			info.szName = threadName;
			info.dwThreadID = (DWORD)-1;
			info.dwFlags = 0;

			__try
			{
				RaiseException( 0x406D1388, 0, sizeof( info ) / sizeof( ULONG_PTR ), (ULONG_PTR*)&info );
			}
			__except ( EXCEPTION_EXECUTE_HANDLER )
			{
			}
		}
	}

	void YieldCurrentThreadImpl()
	{
		(void)::SwitchToThread();
	}

	void SleepOnCurrentThreadImpl( TTimespec sleepTimeInMS )
	{
		::Sleep( sleepTimeInMS );
	}

	void SetCurrentThreadAffinityImpl( TAffinityMask affinityMask )
	{
		if ( affinityMask != 0 )
		{
			REDTHR_WIN_CHECK( ::SetThreadAffinityMask( GetCurrentThread(), affinityMask ) );
		}
	}

	void SetCurrentThreadNameImpl( const char* threadName )
	{
		helper::SetCurrentThreadNameInPIX( threadName );
		helper::SetThreadNameInDebugger( threadName );
	}

	void SuspendThreadByIdImpl( red::ThreadId threadId )
	{
		DWORD dwThreadId = static_cast< DWORD >( threadId.id );
		RED_ASSERT( dwThreadId != GetCurrentThreadId(), "Suspending current thread is not allowed!" );

		HANDLE hThread = OpenThread( THREAD_SUSPEND_RESUME, FALSE, dwThreadId );
		if( hThread != INVALID_HANDLE_VALUE )
		{
			SuspendThread( hThread );
			CloseHandle( hThread );
		}
	}

	void ResumeThreadByIdImpl( red::ThreadId threadId )
	{
		DWORD dwThreadId = static_cast< DWORD >( threadId.id );
		HANDLE hThread = OpenThread( THREAD_SUSPEND_RESUME, FALSE, dwThreadId );
		if( hThread != INVALID_HANDLE_VALUE )
		{
			ResumeThread( hThread );
			CloseHandle( hThread );
		}
	}

#ifdef RED_PLATFORM_DURANGO
	Uint32 GetMaxHardwareConcurrencyImpl()
	{
		DWORD_PTR procMask = 0;
		DWORD_PTR sysMask = 0;
		if ( ::GetProcessAffinityMask( ::GetCurrentProcess(), &procMask, &sysMask ) != 0 )
			return (procMask & 0x7F) == 0x7F ? 7 : 6;
		return 6;
	}
#elif defined( RED_PLATFORM_WINPC )
	// Following algorithm is based on AMD library "CPU core counts", license: MIT
	// https://github.com/GPUOpen-LibrariesAndSDKs/cpu-core-counts/

	red::AnsiChar* GetCpuidVendor( red::AnsiChar* vendor )
	{
		Int32 data[4];
		__cpuid( data, 0 );
		*reinterpret_cast<int*>( vendor     ) = data[1];
		*reinterpret_cast<int*>( vendor + 4 ) = data[3];
		*reinterpret_cast<int*>( vendor + 8 ) = data[2];
		vendor[12] = 0;
		return vendor;
	}

	int GetCpuidFamily()
	{
		Int32 data[4];
		__cpuid( data, 1 );
		Int32 family = ( ( data[0] >> 8 ) & 0x0F );
		Int32 extendedFamily = ( data[0] >> 20 ) & 0xFF;
		Int32 displayFamily = ( family != 0x0F ) ? family : ( extendedFamily + family );
		return displayFamily;
	}

	void GetProcessorCount( Uint32& cores, Uint32& logical )
	{
		cores = logical = 1;
		DWORD len = 0;
		if ( FALSE == GetLogicalProcessorInformationEx( RelationAll, nullptr, &len ) )
		{
			if ( GetLastError() == ERROR_INSUFFICIENT_BUFFER )
			{
				red::AnsiChar* const buf = ( red::AnsiChar* )alloca( len );
				if ( GetLogicalProcessorInformationEx( RelationAll, (PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX)buf, &len ) )
				{
					cores = logical = 0;
					const red::AnsiChar* ptr = buf;
					while ( ptr < buf + len )
					{
						PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX pi = (PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX)ptr;
						if ( pi->Relationship == RelationProcessorCore )
						{
							cores++;
							for ( MemSize g = 0; g < pi->Processor.GroupCount; ++g )
							{
								logical += BitUtils::PopulationCount< Uint64 >( pi->Processor.GroupMask[g].Mask );
							}
						}
						ptr += pi->Size;
					}
				}
			}
		}
	}

	Uint32 GetMaxHardwareConcurrencyImpl()
	{
		Uint32 cores, logical;
		GetProcessorCount( cores, logical );
		Uint32 count = logical;
		red::AnsiChar vendor[13];
		GetCpuidVendor( vendor );
		if ( 0 == red::Strcmp( vendor, "AuthenticAMD" ) )
		{
			if ( 0x15 == GetCpuidFamily() )
			{
				// AMD "Bulldozer" family microarchitecture
				count = logical;
			}
			else
			{
				// This specific logic is the latest recommendation from AMD engineers as of December 2020.
				count = ( cores > 6 ) ? cores : logical;
			}
		}
		return count;
	}
#endif


#ifdef RED_PLATFORM_DURANGO
	typedef DWORD TThreadRetval;
#else
	typedef Uint32 TThreadRetval;
#endif

	static TThreadRetval RED_STDCALL ThreadEntryFunc( void* userData )
	{
		_MM_SET_FLUSH_ZERO_MODE(_MM_FLUSH_ZERO_ON);
		_MM_SET_DENORMALS_ZERO_MODE(_MM_DENORMALS_ZERO_ON);

		Thread* context = static_cast< Thread* >( userData );
		RED_SYSTEM_ASSERT( context, "Missing thread context" );
		
		if ( context )
		{
			SetCurrentThreadNameImpl( context->GetThreadName() );
			::CoInitializeEx( nullptr, COINIT_MULTITHREADED );
			context->ThreadFunc();
		}

		return 0;
	}

	namespace // anonymous
	{
		Int32 priorityLUT[] =
		{
			THREAD_PRIORITY_IDLE,
			THREAD_PRIORITY_LOWEST,
			THREAD_PRIORITY_BELOW_NORMAL,
			THREAD_PRIORITY_NORMAL,
			THREAD_PRIORITY_ABOVE_NORMAL,
			THREAD_PRIORITY_HIGHEST,
			THREAD_PRIORITY_TIME_CRITICAL,
		};
	}

	ThreadImpl::ThreadImpl( const ThreadMemParams& memParams )
		: m_memParams( memParams )
		, m_thread()
		, m_debug( nullptr )
	{
	}

	void ThreadImpl::InitThread( Thread* context )
	{
		RED_SYSTEM_ASSERT( ! IsValid(), "Thread already created!" );
		if ( IsValid() )
		{
			return;
		}

		m_debug = context;

		const TStackSize stackSize = m_memParams.m_stackSize;

		RED_SYSTEM_ASSERT( context, "No thread context specified" );
		const Uint32 flags = STACK_SIZE_PARAM_IS_A_RESERVATION | CREATE_SUSPENDED;

#ifdef RED_PLATFORM_DURANGO
		// "There are some known issues with using CRT with CreateThread as you have noted in MSDN page. We are aware of this issue and will address it in future XDK releases."
		// https://forums.xboxlive.com/AnswerPage.aspx?qid=cb580b0c-4bc1-4fce-87fa-5efa3f165d6d&tgt=1
		m_thread = ::CreateThread( nullptr, stackSize, ThreadEntryFunc, context, flags, nullptr );
#else
		m_thread = reinterpret_cast< HANDLE >( ::_beginthreadex( nullptr, stackSize, ThreadEntryFunc, context, flags, nullptr ) );
#endif
		RED_SYSTEM_ASSERT( m_thread, "Failed to create thread" );

		Int32 priority = priorityLUT[ TP_Normal ];
		REDTHR_WIN_CHECK( ::SetThreadPriority( m_thread, priority ) );
		REDTHR_WIN_CHECK( ::ResumeThread( m_thread ) );
	}

	ThreadImpl::~ThreadImpl()
	{
		// Could detach the thread, but since ThreadFunc belongs to
		// Thread we're more likely in some messed up state.

		AnsiChar dbgBuf[256];
		red::StringConvert( dbgBuf, m_debug ? m_debug->GetThreadName() : "<no context", 256 );

		RED_SYSTEM_ASSERT( !IsValid(), "Programmer error - manage thread lifetimes properly in thread '%s': thread object reached base destructor without a JoinThread() or DetachThread()", dbgBuf );
	}

	void ThreadImpl::JoinThread()
	{
		REDTHR_ASSERT( IsValid() );
		if ( IsValid() )
		{
			const DWORD waitResult = ::WaitForSingleObject( m_thread, INFINITE );
			REDTHR_WIN_CHECK( waitResult != WAIT_FAILED ); // Get extended error info if applicable
			RED_SYSTEM_VERIFY( waitResult == WAIT_OBJECT_0, "Failed to wait for thread" );
			REDTHR_WIN_CHECK( ::CloseHandle( m_thread ) );
			m_thread = HANDLE();
		}
	}

	void ThreadImpl::DetachThread()
	{
		REDTHR_ASSERT( IsValid() );
		if ( IsValid() )
		{
			REDTHR_WIN_CHECK( ::CloseHandle( m_thread ) );
			m_thread = HANDLE();
		}
	}

	void ThreadImpl::SetAffinityMask( Uint64 mask )
	{
		// Catch usage mistakes
		REDTHR_ASSERT( IsValid() );

		// But only set for consoles. Avoid having people set affinity for PC.
#ifdef RED_PLATFORM_DURANGO
		if ( IsValid() )
		{
			REDTHR_WIN_CHECK( ::SetThreadAffinityMask( m_thread, static_cast< DWORD_PTR >( mask ) ) );		
		}
#endif
	}

	void ThreadImpl::SetPriority( EThreadPriority priority )
	{
		REDTHR_ASSERT( IsValid() );
		if ( IsValid() )
		{
			REDTHR_WIN_CHECK( ::SetThreadPriority( m_thread, priorityLUT[priority] ) );
		}
	}

	void ThreadImpl::SetPriorityBoost( Bool threadBoostDisabled )
	{
		REDTHR_ASSERT( IsValid() );
		if ( IsValid() )
		{
			REDTHR_WIN_CHECK( ::SetThreadPriorityBoost( m_thread, threadBoostDisabled ) );
		}
	}

	Bool ThreadImpl::operator==( const ThreadImpl& rhs ) const
	{
		REDTHR_ASSERT( IsValid() );
		if ( IsValid() )
		{
			return ::GetThreadId( m_thread ) == ::GetThreadId( rhs.m_thread );
		}
		return false;
	}

} } // namespace red { namespace WinAPI {

#endif // RED_THREADS_PLATFORM_WINDOWS_API
