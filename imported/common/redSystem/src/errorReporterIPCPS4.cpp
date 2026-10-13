/**
* Copyright © 2016 CDProjekt Red, Inc. All Rights Reserved.
*/

#include "build.h"
#include "dbgUtils.h"

#ifdef RED_PLATFORM_WINPC

// #tbd: use premake to determine if have PS4 SDK and so can define this
// More lame sticking in dev/internal
// Def out in final
# define RED_USE_PS4_ORTMAPI
# ifdef RED_USE_PS4_ORTMAPI
#  import "progid:ortmapi.ORTMAPI" no_namespace exclude("_FILETIME")
#  include <atlbase.h>
#  include <atlcom.h>
#  include <atlsafe.h>

template<typename InterfacePtr>
class Collection
{
	CComVariant m_Array;

public:
	explicit Collection(CComVariant const& arr)
		: m_Array(arr)
	{}

	LONG GetCount()
	{
		if (m_Array.vt != (VT_ARRAY | VT_VARIANT))
			return -1;
// 			throw _com_error(E_INVALIDARG);

		CComSafeArray<VARIANT> arr(m_Array.parray);
		return arr.GetCount();
	}

	InterfacePtr GetItem(LONG index)
	{
		if (m_Array.vt != (VT_ARRAY | VT_VARIANT))
//			throw _com_error(E_INVALIDARG);
		return nullptr;

		CComSafeArray<VARIANT> arr(V_ARRAY(&m_Array));

		// Zero indexing
		index += arr.GetLowerBound();

		if (index > arr.GetUpperBound())
			throw _com_error(E_INVALIDARG);

		CComVariant var = arr.GetAt(index);

		if (var.vt != VT_DISPATCH)
			//throw _com_error(E_INVALIDARG);
			return nullptr;

		InterfacePtr ip;
		HRESULT hr = V_DISPATCH(&var)->QueryInterface(__uuidof(InterfacePtr), (void **)&ip);

		if (FAILED(hr))
			//throw _com_error(hr);
			return nullptr;

		return ip;
	}
};

class ATL_NO_VTABLE ProcessMonitor
	: public CComObjectRootEx<CComObjectThreadModel>
	, public IDispatchImpl<IEventDebug, &__uuidof(__ORTMAPILib)>
{
public:
	BEGIN_COM_MAP(ProcessMonitor)
		COM_INTERFACE_ENTRY_NOINTERFACE(IDispatch)
		COM_INTERFACE_ENTRY(IEventDebug)
	END_COM_MAP()

public:
	ProcessMonitor()
		: m_processID( 0 )
		, m_exitCode( -1 )
	{
	}
	
	~ProcessMonitor()
	{
	}

	Bool WaitForProcessCreate()
	{
		return false;
	}
	
	Bool CheckProcessCreate()
	{
		return false;
	}
	
	Bool CheckProcessExit()
	{
		return false;
	}

	Int32 GetExitCode() { return m_exitCode; }

public:
	// IEventDebug methods
	STDMETHODIMP raw_OnProcessCreate(IProcessCreateEvent* ev)
	{
		// NEw process created? So means we should exit.

		return S_OK;
	}

	//#tbd: if hibernate while running and then later resume, need to check for process create?
	STDMETHODIMP raw_OnProcessExit(IProcessExitEvent* pEvent)
	{
		if ( pEvent->ProcessId == m_processID )
		{
			m_exitCode = pEvent->ExitCode;
		}
		else
		{
			RED_DBG_TRACE( "OnProcessExit: Mismatching processID %u vs %u", m_processID, pEvent->ProcessId );
			m_exitCode = -1;
		}
		m_processID = 0;
		return S_OK;
	}
	
	STDMETHODIMP raw_OnStopNotification(IStopNotificationEvent*)
	{ 
		return S_OK;
	}

	STDMETHODIMP raw_OnCoredumpCompleted(ICoredumpCompletedEvent*) { return S_OK; }
	
	STDMETHODIMP raw_OnCoredumpInProgress(ICoredumpInProgressEvent*) 
	{
		// Try without a debugger attached...
		return S_OK;
	}

	STDMETHODIMP raw_OnProcessLoading(IProcessLoadingEvent*) { return S_OK; }
	STDMETHODIMP raw_OnSettingsChanged(ISettingsChangedEvent*) { return S_OK; }
	STDMETHODIMP raw_OnPowerState(IPowerStateEvent*) { return S_OK; }
	STDMETHODIMP raw_OnProcessKill(IProcessKillEvent*) { return S_OK; }
	STDMETHODIMP raw_OnThreadCreate(IThreadCreateEvent*) { return S_OK; }
	STDMETHODIMP raw_OnThreadExit(IThreadExitEvent*) { return S_OK; }
	STDMETHODIMP raw_OnProgress(IProgressEvent*) { return S_OK; }
	
	
	STDMETHODIMP raw_OnDynamicLibraryLoad(IDynamicLibraryLoadEvent*) { return S_OK; }
	STDMETHODIMP raw_OnDynamicLibraryUnload(IDynamicLibraryUnloadEvent*) { return S_OK; }

private:
	Uint32				m_processID;
	Int32				m_exitCode;
	HANDLE				m_processCreate;
	HANDLE				m_processExit;
};

static HRESULT FindProcessForID( ITargetPtr target, Uint32 processID, IProcess** outPtr )
{
	if ( !target || !outPtr )
	{
		return E_INVALIDARG;
	}

	HRESULT hr = S_OK;
	
	ITarget9Ptr target9;
	hr = target->QueryInterface( __uuidof(ITarget9), reinterpret_cast<void**>( &target9 ) );
	if ( FAILED(hr) )
	{
		return hr;
	}

	CComVariant varApps;
	hr = target9->raw_AppList( &varApps );
	if ( FAILED(hr) )
	{
		return hr;
	}

	Collection< IApplicationInfoPtr > arr( varApps );
	for ( LONG i = 0; i < arr.GetCount(); ++i )
	{
		IApplicationInfoPtr app = arr.GetItem( i );
		Collection< IProcessPtr > procs( app->Processes );
		for ( LONG j = 0; j < procs.GetCount(); ++j )
		{
			IProcessPtr p = procs.GetItem( j );
			if ( p->Id == processID )
			{
				_bstr_t name( app->Name, true );
				RED_TOUCH(name);
				RED_DBG_TRACE( "ProcessId %u found in app <%hs>", processID, static_cast<const wchar_t*>( name ) );
				*outPtr = p;
				return S_OK;
			}
		}
	}

	return E_FAIL;
}

struct CoInitializeGuard
{
	HRESULT m_hr;

	CoInitializeGuard()
	{
		m_hr = ::CoInitializeEx(nullptr, COINIT_MULTITHREADED);
	}

	~CoInitializeGuard()
	{
		::CoUninitialize();
	}
};

namespace dbgutils
{
namespace ps4
{
	// #fixme: move into errorReporterPS4 (not IPC)
	Int32 ErrorReporterMainLoop(const char* connectionString)
	{
		Uint32 processID = 0;
		if (!red::StringToInt(processID, connectionString, nullptr, red::BaseTen) || processID == 0)
		{
			RED_DBG_TRACE("Failed to parse process ID from cmdLine '%hs'", connectionString);
			return 1;
		}

		RED_DBG_TRACE("Parsed process ID %u", processID);

		CoInitializeGuard comGuard;
		if (FAILED(comGuard.m_hr))
			return 1;

		CComModule _Module;
		_pAtlModule = &_Module;

		IORTMAPIPtr tm;
		HRESULT hr = ::CoCreateInstance(__uuidof(ORTMAPI), nullptr, CLSCTX_INPROC_SERVER, __uuidof(IORTMAPI), reinterpret_cast<void**>(&tm));

		if (FAILED(hr))
		{
			RED_DBG_TRACE("Failed to initialize ORTMAPI: HRESULT=0x%08X", hr);
			return 1;
		}

		Int32 exitCode = 0;

		hr = tm->raw_CheckCompatibility(BuildVersion);
		if (FAILED(hr))
		{
			RED_DBG_TRACE("CheckCompatibility failed: HRESULT=0x%08X", hr);
			return 1;
		}

		ITargetPtr target = tm->DefaultTarget;

		if (!target)
		{
			RED_DBG_TRACE("No default target!");
			return 1;
		}

		hr = target->raw_RequestConnection();
		if (FAILED(hr))
		{
			RED_DBG_TRACE("Failed to connect: HRESULT=0x%08X", hr);
			return 1;
		}

		IProcessPtr process;
		hr = FindProcessForID(target, processID, &process);
		if (FAILED(hr))
		{
			RED_DBG_TRACE("Failed to find procID %u: HRESULT=0x%08X", processID, hr);
			return 1;
		}

		// Advise process events, THEN scan. although not exactly totally async, is it? Well, it is multithread apartment... hm.

		return 0;
	}
} // namespace dbgutils::ps4
} // namespace dbgutils

// Can leace the deci4h stuff in dbgUtilsPS4. Can extern then into here, but generally not expose them?
// Generally don't use readmemory API since assist mode apparently can't handle it??? So send all the data... bleh.
// Query protocol in use for assert proto? But then chicken and egg.

// #tbd: PS4 sample kills process or shutdown, guaranteed processexit?
//ITarget9::GenerateSystemCoreDump vs TriggerCoreDump
		// #tbd: ITarget7 CoreFileUserData


// Ideally launched before app, so know it's running but meh.
// Can gather info like "did make core dump file" etc, and set a flag. So then don't have to even pass in target etc into handler. Just get it when process exits
// or we kill it.
// Launched with the procID to get, but could have exited, but assume wouldn't relaunch or be reused too soon, but system might
// so should have SOME sanity check?
//ITarget8 generic protocol
//ITarget9 AppList
// 	CComSafeArray<BSTR> appList;
// 	otherPrograms.Create();
// 	CComVariant args(otherPrograms);
// 	hr = target->raw_QueryProtocolInUse(protocol, &args);

// 	namespace ps4
// 	{
// #pragma pack(push, 1)
// 		struct Deci4hMessage
// 		{
// 			ErrMsgArgs m_args;
// 			ErrButtonArgs m_buttonArgs;
// 			ErrDialogResult m_outResult;
// 			StackTrace m_stackTrace;
// 		};
// #pragma pack(pop)
// 	}


# endif // RED_USE_PS4_ORTMAPI

#endif // RED_PLATFORM_WINPC
