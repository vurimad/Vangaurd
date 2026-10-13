/**
* Copyright (c) 2017 CD Projekt Red. All Rights Reserved.
*/

#pragma once

#include "../../../common/generalEditorBackend/include/tree.h"

#include "../../../common/interop/include/interopUtilsManual.h"
#include "../../../common/interopServices/include/interopClient.h"


namespace interop
{
	namespace srv
	{
		class NotificationDispatcher;
	}
}

namespace tools
{

class ResourceBackend;
struct FunctionalTestsSinkData;
typedef red::UniquePtr<FunctionalTestsSinkData> FunctionalTestsSinkDataPtr;

//////////////////////////////////////////////////////////////////////////

struct BACKEND_ENGINE_API DebuggerCreateContext
{
	ResourceBackend* m_resourceBackend = nullptr;
	interop::srv::InteropClient* m_interop = nullptr;
};

struct BACKEND_ENGINE_API DebuggerInitContext
{
	ResourceBackend* m_resourceBackend = nullptr;
	THandle< CResource > m_resource = nullptr;
};

//////////////////////////////////////////////////////////////////////////

class BACKEND_ENGINE_API IDebuggerBackendImplementation
{
	RED_USE_MEMORY_POOL( red::PoolBackend );

public:

	virtual ~IDebuggerBackendImplementation() {}
	virtual interop::ObjectPtr AsInteropObject() = 0;
	virtual Bool Init( const DebuggerInitContext& context ) = 0;
	virtual void GetRequiredRuntimeDebuggers( red::DynArray< red::String >& ids ) const = 0;
	virtual void OnConnected() = 0;
	virtual void OnDisconnected() = 0;
};

//////////////////////////////////////////////////////////////////////////

class BACKEND_ENGINE_API IDebuggerBackendFactory
{
	RTTI_DECLARE_TYPE( IDebuggerBackendFactory );

public:

	virtual ~IDebuggerBackendFactory() {}
	virtual red::String GetId() const = 0;
	virtual red::UniquePtr< IDebuggerBackendImplementation > Create( const DebuggerCreateContext& context ) const = 0;
};

//////////////////////////////////////////////////////////////////////////

class BACKEND_ENGINE_API DebuggerBackend final : public interop::srv::InteropConnectionListener
{
	DECLARE_INTEROP( DebuggerBackend );

	DebuggerBackend( ResourceBackend& resourceBackend, comm::ChannelFactory& channelFactory );
	virtual ~DebuggerBackend();

	static interop::srv::InteropPath GetDebuggerPath( const red::String& debuggerId );

	void ProcessCommunication();

private:
	typedef red::UniquePtr< IDebuggerBackendImplementation > DebuggerPtr;
	typedef red::Map< red::String, DebuggerPtr > DebuggersMap;
	typedef red::DynArray< red::String > DebuggerIds;
	typedef red::UniquePtr< interop::srv::InteropSink > SinkPtr;
	typedef red::HashMap< red::String, SinkPtr > RuntimeDebuggersSinks;
	
	// enum with the same values should be defined on frontend side
	enum ConnectionResult : Uint32
	{
		Success = 0,
		Wait = 1,
		Error = 2,
	};

	// debuggers implementations
	red::UniquePtr< IDebuggerBackendImplementation > CreateDebugger( const red::String& debuggerId );
	IDebuggerBackendImplementation* GetDebugger( const red::String& debuggerId, Bool create );
	ConnectionResult StartDebugger( const red::String& debuggerId, const red::String& resourcePath );
	Bool StartRuntimeDebuggers( const red::String& debuggerId, IDebuggerBackendImplementation* debugger );
	void StopDebugger( const red::String& debuggerId );
	Bool StopRuntimeDebuggers( const red::String& debuggerId );
	Bool IsFullyConnected( const red::String& debuggerId ) const;

	// InteropConnectionListener
	virtual void OnConnecting() override;
	virtual void OnConnected() override;
	virtual void OnDisconnected() override;

	// interop communication with game engine
	void ConnectInterop( const red::String& gameIpAddress, Uint16 startingPort );
	Bool DisconnectInterop( Bool notifyDisconnected, Bool waitForOtherDebuggers );

	// runtime debuggers management
	interop::srv::InteropSink* GetRuntimeDebuggerSink( const red::String& runtimeDebuggerId );
	void ConnectRuntimeDebugger( const red::String& runtimeDebuggerId );
	void DisconnectRuntimeDebugger( const red::String& runtimeDebuggerId );
	void OnRuntimeDebuggerConnected( const red::String& debuggerId );
	void OnRuntimeDebuggerDisconnected( const red::String& debuggerId );

	void ClearInternals();

	template < typename... Args >
	ConnectionResult EmitError( Args... args );

	// debuggers implementations
	ResourceBackend& m_resourceBackend;
	DebuggersMap m_debuggers;

	// runtime debuggers management
	DebuggerIds m_connectedRuntimeDebuggers;
	red::HashMap< red::String, DebuggerIds > m_requiredRuntimeDebuggers;
	red::HashMap< red::String, DebuggerIds > m_runtimeDebuggersClients;
	RuntimeDebuggersSinks m_runtimeDebuggersSinks;
	
	red::String m_lastErrorMsg;

	FunctionalTestsSinkDataPtr m_functionalTestsData;//use only when FT is running
	HANDLE                     m_mappedDataHandle;
	void FTRegisterSharedMemoryInfo( red::Uint16 port );
	void FTCheckInstanceGUIDParameter( );

	// interop communication with game engine
	comm::ChannelFactory& m_channelFactory;
	interop::srv::InteropClient m_interopClient;

	// frontend interop
	interop::InteropNotifier m_onInteropConnected;
	interop::InteropNotifier m_onInteropDisconnected;
	interop::InteropNotifier m_onDebuggerConnected;
	interop::InteropNotifier m_onDebuggerDisconnected;
};

} // tools