/**
* Copyright (c) 2017 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"
#include "debuggerBackend.h"
#include "resourceEditor.h"
#include "../../backend/backendEngine/include/resourceBackend.h"
#include "../../common/app/include/logging.h"
#include "../../../common/redSystem/include/loggerFileSink.h"

RTTI_BEGIN_ABSTRACT_TYPE_IN_NAMESPACE( IDebuggerBackendFactory, tools );
RTTI_END_TYPE();


namespace dd
{
	static red::CrashData<String, 128>       testName("FunctionalTests", "TestName");
	static red::CrashData< red::String, 64 > buildProcessName( "FunctionalTests", "BuildProcessName" );
	static red::CrashData< Uint64 >          buildNum( "FunctionalTests", "BuildNum" );
	static red::CrashData< Bool >            buildWithShelve( "FunctionalTests", "BuildWithShelve" );
	static red::CrashData< red::String, 16 > hostIP( "FunctionalTests", "HostIP" );
	static red::CrashData< red::String, 64 > hostName( "FunctionalTests", "HostName" );
	static red::CrashData< red::String, 64 > engineInstanceGUID( "FunctionalTests", "EngineInstanceGUID" );
	static red::String engineInstanceGUIDValue;//needed to return previously set value
}

namespace
{
	enum class RequestType : Uint32
	{
		Connect,
		Disconnect
	};
}

namespace tools
{

struct FunctionalTestsSinkData
{
	RED_USE_MEMORY_POOL( red::PoolInterop );

	bool                m_isDetached = false;
	red::LoggerFileSink m_sink;
	bool                m_sinkOpened = false;
	red::String         m_sinkPath;

	~FunctionalTestsSinkData( )
	{
		if( m_sinkOpened )
		{
			red::UnregisterLoggerSink( &m_sink );
			m_sink.CloseFile( );
		}
	}
};

DebuggerBackend::DebuggerBackend( ResourceBackend& resourceBackend, comm::ChannelFactory& channelFactory )
	: m_resourceBackend( resourceBackend )
	, m_channelFactory( channelFactory )
	, m_interopClient( m_channelFactory )
	, m_mappedDataHandle( INVALID_HANDLE_VALUE )
{
	FTCheckInstanceGUIDParameter( );
}

DebuggerBackend::~DebuggerBackend()
{
	DisconnectInterop( true, false );
	if( m_mappedDataHandle != INVALID_HANDLE_VALUE )
	{
		::CloseHandle( m_mappedDataHandle );
	}
}

//////////////////////////////////////////////////////////////////////////

interop::srv::InteropPath DebuggerBackend::GetDebuggerPath( const red::String& debuggerId )
{
	return interop::srv::InteropPath().GetRoot().GetChild( debuggerId );
}

red::UniquePtr< IDebuggerBackendImplementation > DebuggerBackend::CreateDebugger( const red::String& debuggerId )
{
	red::DynArray< const rtti::ClassType* > factories;
	GetRttiSystem().EnumClasses( ClassID< IDebuggerBackendFactory >(), factories );
	for ( const auto& factoryClass : factories )
	{
		IDebuggerBackendFactory* factoryObj = static_cast< IDebuggerBackendFactory* >( factoryClass->GetDefaultObject() );
		if ( factoryObj->GetId() == debuggerId )
		{
			DebuggerCreateContext debuggerContext;
			debuggerContext.m_resourceBackend = &m_resourceBackend;
			debuggerContext.m_interop = &m_interopClient;

			return factoryObj->Create( debuggerContext );
		}
	}
	return nullptr;
}

IDebuggerBackendImplementation* DebuggerBackend::GetDebugger( const red::String& debuggerId, Bool create )
{
	DebuggersMap::iterator it = m_debuggers.Find( debuggerId );
	if ( it == m_debuggers.End() )
	{
		if ( !create )
		{
			return nullptr;
		}
		red::UniquePtr< IDebuggerBackendImplementation > debugger = CreateDebugger( debuggerId );
		if ( debugger == nullptr )
		{
			return nullptr;
		}
		it = m_debuggers.Insert( debuggerId, std::move( debugger ) ).Iterator();
	}
	return it.Value().Get();
}

DebuggerBackend::ConnectionResult DebuggerBackend::StartDebugger( const red::String& debuggerId, const red::String& resourcePath )
{
	IDebuggerBackendImplementation* debugger = GetDebugger( debuggerId, true );
	if ( debugger == nullptr )
	{
		return EmitError( "Cannot find debugger for given id [%hs]", debuggerId.AsChar() );
	}

	// initialize debugger backend
	DebuggerInitContext initContext;
	initContext.m_resourceBackend = &m_resourceBackend;

	res::ResourceTokenHandle token = GResourceLoader->IssueLoadingRequest( res::ResourcePath::Build( resourcePath ) );
	initContext.m_resource = token->WaitUntilLoaded();

	if ( !debugger->Init( initContext ) )
	{
		return EmitError( "Cannot initialize debugger backend for resource [%hs]", resourcePath.AsChar() );
	}	

	// disconnect from runtime debuggers
	if( StopRuntimeDebuggers( debuggerId ) )
	{
		debugger->OnDisconnected();
		m_onDebuggerDisconnected.Notify< red::String >( debuggerId );
	}

	// connect to required runtime debuggers
	if ( StartRuntimeDebuggers( debuggerId, debugger  ) )
	{
		debugger->OnConnected();
		return ConnectionResult::Success;
	}

	return  ConnectionResult::Wait;
}

Bool DebuggerBackend::StartRuntimeDebuggers( const red::String& debuggerId, IDebuggerBackendImplementation* debugger )
{
	DebuggerIds& debuggerIds = m_requiredRuntimeDebuggers[ debuggerId ];
	debugger->GetRequiredRuntimeDebuggers( debuggerIds );

	Bool isConnected = true;
	for( const auto& runtimeDebuggerId :debuggerIds )
	{
		// runtime debugger with specified id wasn't connected yet
		if( !m_connectedRuntimeDebuggers.Exist( runtimeDebuggerId ) )
		{
			isConnected = false;
			ConnectRuntimeDebugger( runtimeDebuggerId );
		}
		red::alg::PushBackUnique( m_runtimeDebuggersClients[ runtimeDebuggerId ], debuggerId );
	}

	return isConnected;
}

void DebuggerBackend::StopDebugger( const red::String& debuggerId )
{
	IDebuggerBackendImplementation* debugger = GetDebugger( debuggerId, false );
	if ( debugger == nullptr )
	{
		return;
	}

	if( StopRuntimeDebuggers( debuggerId ) )
	{
		debugger->OnDisconnected();
	}

	m_debuggers.Remove( debuggerId );
}

Bool DebuggerBackend::StopRuntimeDebuggers( const red::String& debuggerId )
{
	Bool wasConnected = false;
	for( const auto& runtimeDebuggerId : m_requiredRuntimeDebuggers[ debuggerId ] )
	{
		// runtime debugger with specified id wasn't connected yet
		if( !m_connectedRuntimeDebuggers.Exist( runtimeDebuggerId ) )
		{
			continue;
		}
		DebuggerIds& clients = m_runtimeDebuggersClients[ runtimeDebuggerId ];
		clients.Remove( debuggerId );
		if( clients.Empty() )
		{
			DisconnectRuntimeDebugger( runtimeDebuggerId );
			m_connectedRuntimeDebuggers.Remove( runtimeDebuggerId );
			m_runtimeDebuggersClients.Remove( runtimeDebuggerId );
		}

		wasConnected = true;
	}

	return wasConnected;
}

Bool DebuggerBackend::IsFullyConnected( const red::String& debuggerId ) const
{
	for ( const red::String& runtimeDebuggerId : m_requiredRuntimeDebuggers[ debuggerId ] )
	{
		if ( !m_connectedRuntimeDebuggers.Exist( debuggerId ) )
		{
			return false;
		}
	}
	return true;
}

//////////////////////////////////////////////////////////////////////////

interop::srv::InteropSink* DebuggerBackend::GetRuntimeDebuggerSink( const red::String& runtimeDebuggerId )
{
	RuntimeDebuggersSinks::iterator it = m_runtimeDebuggersSinks.Find( runtimeDebuggerId );
	if ( it == m_runtimeDebuggersSinks.End() )
	{
		// no sink for specified runtime debugger, so we need to create one
		SinkPtr sink = red::CreateUniquePtr< interop::srv::InteropSink >( m_interopClient, GetDebuggerPath( runtimeDebuggerId ) );
		
		// setup its callback
		sink->SetCallback( [ this, runtimeDebuggerId ]( const interop::srv::InteropResult& result, const interop::srv::InteropCallbackDataPtr userData )
		{
			if ( result.HasErrors() )
			{
				return;
			}

			red::String dataId = result.GetDataDirect< String >();
			RED_ASSERT( runtimeDebuggerId == dataId, "Wrong runtime debugger id" );

			switch ( static_cast< RequestType >( userData->GetType() ) )
			{
			case RequestType::Connect:
				OnRuntimeDebuggerConnected( dataId );
				break;
			case RequestType::Disconnect:
				OnRuntimeDebuggerDisconnected( dataId );
				break;
			}
		});

		it = m_runtimeDebuggersSinks.Insert( runtimeDebuggerId, std::move( sink ) ).Iterator();
	}

	return it.Value().Get();
}

void DebuggerBackend::ConnectRuntimeDebugger( const red::String& runtimeDebuggerId )
{
	GetRuntimeDebuggerSink( runtimeDebuggerId )->Dispatch( interop::srv::InteropPath().Call( "connect" ), static_cast< Uint32 >( RequestType::Connect ) );
}

void DebuggerBackend::DisconnectRuntimeDebugger( const red::String& runtimeDebuggerId )
{
	GetRuntimeDebuggerSink( runtimeDebuggerId )->Dispatch( interop::srv::InteropPath().Call( "disconnect" ), static_cast< Uint32 >( RequestType::Disconnect ) );
}

void DebuggerBackend::OnRuntimeDebuggerConnected( const red::String& runtimeDebuggerId )
{
	if ( m_connectedRuntimeDebuggers.Exist( runtimeDebuggerId ) )
	{
		return;
	}

	m_connectedRuntimeDebuggers.PushBack( runtimeDebuggerId );

	for ( const auto& debuggerId : m_runtimeDebuggersClients[ runtimeDebuggerId ] )
	{
		IDebuggerBackendImplementation* debugger = GetDebugger( debuggerId, false );
		if ( debugger != nullptr )
		{
			debugger->OnConnected();
			m_onDebuggerConnected.Notify< red::String >( debuggerId );
		}
	}

	RED_LOG( "Runtime debugger connected: %hs", runtimeDebuggerId.AsChar() );
}

void DebuggerBackend::OnRuntimeDebuggerDisconnected( const red::String& runtimeDebuggerId )
{
	if ( !m_connectedRuntimeDebuggers.Exist( runtimeDebuggerId ) )
	{
		return;
	}

	for ( const auto& debuggerId : m_runtimeDebuggersClients[ runtimeDebuggerId ] )
	{
		IDebuggerBackendImplementation* debugger = GetDebugger( debuggerId, false );
		if ( debugger != nullptr )
		{
			debugger->OnDisconnected();
			m_onDebuggerDisconnected.Notify< red::String >( debuggerId );
		}
	}

	m_connectedRuntimeDebuggers.Remove( runtimeDebuggerId );

	RED_LOG( "Runtime debugger disconnected: %hs", runtimeDebuggerId.AsChar() );
}

//////////////////////////////////////////////////////////////////////////

template < typename... Args >
DebuggerBackend::ConnectionResult DebuggerBackend::EmitError( Args... args )
{
	m_lastErrorMsg = red::String::Printf( args... );
	return ConnectionResult::Error;
}

//////////////////////////////////////////////////////////////////////////

void DebuggerBackend::ConnectInterop( const red::String& gameIpAddress, Uint16 startingPort )
{
	if ( m_interopClient.IsConnected() )
	{
		red::Network::Utils::CommAddress newAddress( gameIpAddress, startingPort );
		red::Network::Utils::CommAddress oldAddress = m_interopClient.GetHostAddress();

		if ( newAddress.GetHost() == oldAddress.GetHost() && newAddress.GetPort() == oldAddress.GetPort() )
		{
			RED_LOG_INFO( "DebuggerBackend::ConnetInterop early exist (1)" );

			// backend is already connected with the same parameters
			m_onInteropConnected.Notify();
			return;
		}
	}

	
	if ( !DisconnectInterop( false, true ) )
	{
		RED_LOG_INFO( "DebuggerBackend::ConnetInterop early exist (2)" );

		// Prevents crash since the next statements rely on there being no listeners
		// Also can't use the same interop connector when the config is different
		return;
	}

	RED_LOG_INFO( "DebuggerBackend::ConnetInterop connecting" );

	m_interopClient.Intialize( { "debugger", gameIpAddress, startingPort } );
	m_interopClient.RegisterListener( this );
	m_interopClient.Connect();
}

void DebuggerBackend::ClearInternals()
{
	for ( const DebuggerPtr& debugger : m_debuggers.Values() )
	{
		if ( debugger != nullptr )
		{
			debugger->OnDisconnected();
		}
	}

	m_debuggers.Clear();
	m_connectedRuntimeDebuggers.Clear();
	m_requiredRuntimeDebuggers.Clear();
	m_runtimeDebuggersClients.Clear();
	m_runtimeDebuggersSinks.Clear();
}

Bool DebuggerBackend::DisconnectInterop( Bool notifyDisconnected, Bool waitForOtherDebuggers )
{
	if( waitForOtherDebuggers )
	{
		if( !m_debuggers.Empty() )
		{
			return false;
		}
	}

	if ( !notifyDisconnected )
	{
		m_interopClient.UnregisterListener( this );
		m_interopClient.Disconnect();
	}
	else
	{
		m_interopClient.Disconnect();
		m_interopClient.UnregisterListener( this );
	}

	ClearInternals();

	m_interopClient.Shutdown();

	return true;
}

//////////////////////////////////////////////////////////////////////////

void DebuggerBackend::ProcessCommunication()
{
	if ( !m_interopClient.IsDisconnected() )
	{
		m_interopClient.Update();
	}
}

void UnescapePath(red::String& path)
{
	path.ReplaceAll("@", "\"");
	path.ReplaceAll("!", "\\");
}

bool RotateFunctionalTestsLogs( const red::String& path, int num = 0 )
{
	red::String
		pathCurrent = ( num == 0 ? path : red::String::Printf( "%hs.%d", path.AsChar( ), num ) ),
		pathNext = red::String::Printf( "%hs.%d", path.AsChar( ), num + 1 );
	
	HANDLE nextFile = ::CreateFileA( pathNext.AsChar( ), GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL );
	if( INVALID_HANDLE_VALUE != nextFile )
	{
		::CloseHandle( nextFile );
		if( false == RotateFunctionalTestsLogs( path, num + 1 ) )
			return false;
	}
	//succeeded in making space or next file does not exist
	return ::MoveFileA( pathCurrent.AsChar( ), pathNext.AsChar( ) );
};

constexpr const char* GetConfigurationName2( )
{
#if defined( RED_CONFIGURATION_DEBUG )
	return "DEBUG";
#elif defined( RED_CONFIGURATION_RELEASE )
	return "RELEASE";
#elif defined( RED_CONFIGURATION_FINAL )

#if defined ( USE_PROFILER )
	return "PROFILING";
#else
	return "FINAL";
#endif

#else
	return "UNKNOWN";
#endif
}

//////////////////////////////////////////////////////////////////////////

void DebuggerBackend::SetupInterop( DebuggerBackend* ptr, ::interop::utils::Object& op )
{
	// debuggers map
	op.RegisterMap( "debuggers", [ ptr ]( interop::IContext& ctx, const String& elementName )
	{
		IDebuggerBackendImplementation* debugger = ptr->GetDebugger( elementName, false );
		return ( debugger != nullptr ) ? debugger->AsInteropObject() : interop::ObjectPtr();
	} );

	// global debug functors
	op.RegisterFunctor< void, red::String, red::Uint16 >( "connectInterop", [ ptr ]( interop::IContext& ctx, const red::String& gameIpAddress, red::Uint16 startingPort )
	{
		ptr->ConnectInterop( gameIpAddress, startingPort );
	} );
	op.RegisterFunctor< void >( "disconnectInterop", [ ptr ]( interop::IContext& ctx ) 
	{
		ptr->DisconnectInterop( true, true );
	} );
	op.RegisterFunctor< Uint32, red::String, red::String >( "start", [ ptr ]( interop::IContext& ctx, String debuggerId, String resourcePath ) 
	{
		return static_cast< Uint32 >( ptr->StartDebugger( debuggerId, resourcePath ) );
	} );
	op.RegisterFunctor< void, red::String >( "stop", [ ptr ]( interop::IContext& ctx, String debuggerId ) 
	{
		ptr->StopDebugger( debuggerId );
	} );
	op.RegisterFunctor< red::String >( "getLastError", [ ptr ]( interop::IContext& ctx ) 
	{
		return ptr->m_lastErrorMsg;
	} );

	op.RegisterFunctor< bool, red::String >("setFunctionalTestName", [ptr](interop::IContext& ctx, String testName)
	{
		RED_LOG_DEBUG(".debug.setFunctionalTestName(testName = [%hs])", testName.AsChar());
		dd::testName.Set(testName);

		return true;
	});

	op.RegisterFunctor< bool, red::String >("registerAttachmentForErrorReport", [ptr](interop::IContext& ctx, String path)
	{
		RED_LOG_DEBUG(".debug.registerAttachmentForErrorReport(path = [%hs])", path.AsChar());
		UnescapePath(path);
		static red::String firstPath = path;
		static bool firstRun = true;
		if (path != firstPath)
		{
			RED_HALT(
				"Single attachment for error report may be registered (assuming Repro Steps file) and path must be stable - now: [%hs] was: [%hs]",
				path.AsChar(),
				firstPath.AsChar()
			);
		}

		if (firstRun)
		{
			red::RegisterAttachmentForErrorReport(path.AsChar());
			firstRun = false;
		}

		return true;
	});

	op.RegisterFunctor< bool, red::Uint16 >("prepareForDetach", [ptr](interop::IContext& ctx, red::Uint16 port)
	{
		bool result = false;
		if ( ptr->m_functionalTestsData )//&& false == ptr->m_functionalTestsData->m_isDetached)
		{
			ptr->m_functionalTestsData->m_isDetached = true;
			static bool sharedDataRegistered{ false };
			if( false == sharedDataRegistered )
			{
				sharedDataRegistered = true;
				ptr->FTRegisterSharedMemoryInfo( port );
			}

			RED_LOG_INFO("Test Logger shutdown on FunctionalTestsFramework request - finishing current test" );

			if( ptr->m_functionalTestsData->m_sinkOpened )
			{
				RED_LOG_FLUSH_AND_WAIT( );
				red::UnregisterLoggerSink( &ptr->m_functionalTestsData->m_sink );
				ptr->m_functionalTestsData->m_sink.CloseFile( );
				red::String logTestPath = ptr->m_functionalTestsData->m_sinkPath;
				UnescapePath( logTestPath );
				if( false == RotateFunctionalTestsLogs( logTestPath ) )
				{
					RED_LOG_ERROR(
						"Failed to rotate current Functional Test log [%hs], error: [%u]",
						logTestPath.AsChar( ),
						::GetLastError( ) );
				}
				//open again same path, don't register again in error reporter
				ptr->m_functionalTestsData->m_sinkOpened = ptr->m_functionalTestsData->m_sink.OpenFile( logTestPath.AsChar( ) );
				if( false == ptr->m_functionalTestsData->m_sinkOpened )
				{
					RED_LOG_ERROR(
						"Failed to open Functional Tests file sink at [%hs], error: [%u]",
						logTestPath.AsChar( ),
						::GetLastError( ) );
				}
				else
				{
					red::RegisterLoggerSink( &ptr->m_functionalTestsData->m_sink );
				}
			}

			result = true;
		}

		return result;
	});

	op.RegisterFunctor< bool, red::String, red::String >( "afterAttach", [ ptr ](
		interop::IContext& ctx,
		String newCommandline,
		String logTestPath )
	{
		bool
			result = false;

		if( !ptr->m_functionalTestsData )
		{
			ptr->m_functionalTestsData = red::CreateUniquePtr< FunctionalTestsSinkData >( );
			RED_ASSERT( false == logTestPath.Empty( ), "Log test path can't be empty" );
			ptr->m_functionalTestsData->m_sinkPath = logTestPath;
		}

		//if( ptr->m_functionalTestsData->m_isDetached )
		{
			RED_ASSERT( logTestPath == ptr->m_functionalTestsData->m_sinkPath, "Log path must remain unchanged" );

			UnescapePath( logTestPath );
			UnescapePath( newCommandline );

			red::CommandLine::Get( ).Init( newCommandline.AsChar( ) );

			if( false == ptr->m_functionalTestsData->m_sinkOpened )
			{
				ptr->m_functionalTestsData->m_sinkOpened = ptr->m_functionalTestsData->m_sink.OpenFile( logTestPath.AsChar( ) );
				if( ptr->m_functionalTestsData->m_sinkOpened )
				{
					red::RegisterLoggerSink( &ptr->m_functionalTestsData->m_sink );
					red::RegisterAttachmentForErrorReport( ptr->m_functionalTestsData->m_sinkPath.AsChar( ) );
				}
			}

			RED_LOG_INFO("Running Functional Tests - new test started");

			RED_LOG_DEBUG(".debug.afterAttach(newCommandline = [%hs])", newCommandline.AsChar());

			dd::buildWithShelve.Set( red::CommandLine::Get( ).HasOption( "ftBuildWithShelve" ) );
			red::String value;
			if( red::CommandLine::Get( ).GetFirstOption( "ftBuildProcessName", value ) )
				dd::buildProcessName.Set( value );
			if( red::CommandLine::Get( ).GetFirstOption( "ftBuildNum", value ) )
				dd::buildNum.Set( ::FromStringDirect< Uint32 >( value ) );
			if( red::CommandLine::Get( ).GetFirstOption( "ftHostIP", value ) )
				dd::hostIP.Set( value );
			if( red::CommandLine::Get( ).GetFirstOption( "ftHostName", value ) )
				dd::hostName.Set( value );

			result = true;
			ptr->m_functionalTestsData->m_isDetached = false;
		}

		return result;
	});

	op.RegisterValueRO<String>( "engineInstanceGUID", dd::engineInstanceGUIDValue );

	op.RegisterFunctor<red::String, red::String>("sessionInfo", [ptr](interop::IContext& ctx, const red::String& sessionInfo)
	{
		//store on first call, return what we kept for caller
		static red::String mySessionInfo = sessionInfo + red::String::Printf( "*%hs", GetConfigurationName2( ) );

		return mySessionInfo;
	});

	// notifiers
	op.RegisterNotifier( "_onInteropConnected", ptr->m_onInteropConnected );
	op.RegisterNotifier( "_onInteropDisconnected", ptr->m_onInteropDisconnected );
	op.RegisterNotifier( "_onDebuggerConnected", ptr->m_onDebuggerConnected );
	op.RegisterNotifier( "_onDebuggerDisconnected", ptr->m_onDebuggerDisconnected );
}

void DebuggerBackend::FTRegisterSharedMemoryInfo( red::Uint16 port )
{
	RED_LOG_INFO( "Register shared memory info" );

	constexpr auto sharedDataSize = 4 * sizeof( red::Uint32 );

	::SetLastError( 0 );
	m_mappedDataHandle = CreateFileMapping(
		INVALID_HANDLE_VALUE,
		NULL,
		PAGE_READWRITE,
		0,
		sharedDataSize,
		TEXT( "Local\\R6Engine" )// local namespace, name must be same as in dev\src\win32\engineConnection\src\engineStartupJobWindows.cpp
	);

	if( INVALID_HANDLE_VALUE == m_mappedDataHandle )
	{
		RED_LOG_ERROR(
			"%hs: CreateFileMapping failed, error: [%u]",
			__FUNCTION__,
			::GetLastError( )
		);
		return;
	}

	LPVOID mappedView = MapViewOfFile(
		m_mappedDataHandle,
		FILE_MAP_ALL_ACCESS,
		0, //offset high
		0, //offset low
		sharedDataSize
	);

	if( nullptr == mappedView )
	{
		CloseHandle( m_mappedDataHandle );
		m_mappedDataHandle = INVALID_HANDLE_VALUE;

		RED_LOG_ERROR( "%hs: MapViewOfFile failed, error: [%u]",
			__FUNCTION__,
			::GetLastError( )
		);
		return;
	}

	red::Uint32* data = static_cast< red::Uint32* >( mappedView );
	data[ 0 ] = 0xbadf00d;
	data[ 1 ] = ::GetCurrentProcessId( );
	data[ 2 ] = port;
	data[ 3 ] = 1;//editor mode

	for( int i = 0; i != 4; ++i )
	{
		RED_LOG_DEBUG( "%hs: saved shared memory at [#%d] => [%u]", __FUNCTION__, i + 1, data[ i ] );
	}

	UnmapViewOfFile( mappedView );//finish setup, allow others to access it now

	RED_LOG_INFO( "%hs: sucessfully setup shared memory info", __FUNCTION__ );
}

void DebuggerBackend::FTCheckInstanceGUIDParameter( )
{
	const red::CommandLine& commandLine = red::CommandLine::Get( );
	red::String value;
	if( commandLine.GetFirstOption( "ftEngineInstanceGUID", value ) )
	{
		dd::engineInstanceGUID.Set( value );
		dd::engineInstanceGUIDValue = value;
		RED_LOG_DEBUG( "%hs: EngineInstancGUID: [%hs]", __FUNCTION__, dd::engineInstanceGUIDValue.AsChar( ) );
	}
}

void DebuggerBackend::OnConnecting()
{
	RED_LOG_INFO( "DebuggerBackend::OnConnecting." );
}

void DebuggerBackend::OnConnected()
{
	RED_LOG_INFO( "DebuggerBackend::OnConnected." );
	m_onInteropConnected.Notify();
}

void DebuggerBackend::OnDisconnected()
{
	RED_LOG_INFO( "DebuggerBackend::OnDisconnected." );

	m_onInteropDisconnected.Notify();
	ClearInternals();
}

} // tools