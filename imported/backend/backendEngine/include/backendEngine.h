/**
* Copyright (c) 2010 CD Projekt Red. All Rights Reserved.
*/

#pragma once
#include "../../../common/core/include/corePublic.h"
#include "../../../common/engine/include/baseEngine.h"
#include "../../../common/redMemory/include/sharedPtr.h"

#include "../../../common/renderData/include/renderDataPublic.h"

#include "profilerBackend.h"
#include "debuggerBackend.h"
#include "debugFilterHelper.h"
#include "streamingManagerInstance.h"
#include "backendTimer.h"

#include "houdiniEngineUtils.h"

class CSrtTreePropertiesManager;

namespace Services
{
	class PreviewServiceManager;
}

namespace tools
{
	class ResourceBackend;
	class IResourceEditor;
	class EngineStatsBackend;
	class StringIDGenerator;
	class RepositoryWriter;
}

namespace Config
{
	class ConfigBackend;
	typedef red::UniquePtr<ConfigBackend> ConfigBackendPtr;
}

namespace input
{
	class IInputContext;
}

namespace interop
{
	namespace srv
	{
		class InteropService;
	}
}

namespace loc
{
	class LocalizationManager;
}

namespace services
{
	class GameServicesGalaxy;
}

/// Engine for all editor backend applications
class BACKEND_ENGINE_API CBackendEngine : public CBaseEngine
{
	RTTI_DECLARE_TYPE( CBackendEngine );

public:
	CBackendEngine();
	~CBackendEngine();

public:
	// Process input events
	virtual Bool ProcessFrame( const Float scaledEngineTimeDelta, const tick::Info& tickInfo, input::IInputContext& input ) override;

	// Close engine
	virtual void Shutdown() override;

	virtual Bool IsBackend() const;

	RED_INLINE tools::ResourceBackend& GetResourceBackend() { return *m_resourceBackend; }

protected:
	virtual Bool OnRootSystemInitialization( const EngineInitParams& params ) override;
	virtual void OnRegularSystemInitialization( const EngineInitParams& params, const job::RunContext& context, red::Atomic< Uint32 >& keepRunning ) override;

	virtual Bool InitializeServices() override final;
	void ShutdownServices();

	// Initialize backend interop objects
	Bool InitializeInterop();

	// Initialize interop service
	Bool InitializeInteropService();

	// Initialize resource backend (allows resources to be edited)
	Bool InitializeResourceBackend();

	Bool InitializeInput();

	Bool InitializeLocalizationManager();

	void InitializeDebugFilters( rend::RenderFilterPreset preset );
	void InitializeDefaultEditorDebugFilters();

	bool LoadBackendGatheredResources();

	// Check editor connection (closing engine when connection lost, only in case when such connection was previously established)
	void CheckEditorConnection();

	// Process communication with outside world
	void ProcessCommunication();

	void AutoOpenEditors();

	void RequestDetach();

	void AfterAttach(const red::String& newCommandline);

	// Get interop root object
	virtual red::SharedPtr< interop::utils::Object > GetInteropRoot() const override;

	// Interop root for the whole engine
	red::SharedPtr< interop::utils::Object >		m_interopRoot;

	// Interop service
	red::UniquePtr< interop::srv::InteropService >	m_interopService;

	// Resource editor backend
	red::UniquePtr< tools::ResourceBackend > m_resourceBackend;

	// Streaming manager instance
	res::StreamingManagerInstance m_streamingManagerInstance;

	// Timer launching the streaming cache saving procedure
	tools::Timer m_streamingManagerTimer;

	// Config backend for exposing config vars via interop
	Config::ConfigBackendPtr m_configBackend;

	// Engine stats
	red::UniquePtr< tools::EngineStatsBackend > m_statsBackend;

	// Profiler
	red::UniquePtr< red::ProfilerBackend > m_profilerBackend;

	// Debugger
	red::UniquePtr< tools::DebuggerBackend > m_debuggerBackend;
	// Debug filter manager
	red::UniquePtr< DebugFilterHelper >  m_debugFilterHelper;

	// Quality level helper
	red::UniquePtr< QualityLevelHelper >  m_qualityLevelHelper;

	// Game services (login, etc)
	red::UniquePtr< services::GameServicesGalaxy > m_services;

	interop::InteropNotifier m_changedDebugFilterNotifier;
	red::Uint16              m_interopStartingPort;
	bool                     m_detachRequested;

	red::Map< red::String, rend::DebugFilterMask > m_savedDebugFilters;

	// HOUDINI ENGINE INTEGRATION
public:
	// Checks local availability of houdini engine incl. libs & dlls
	Bool IsHoudiniEngineAvailable();
	// Setup environment and initialize session
	void InitializeHoudiniEngine( red::String& msg = red::String() );
	// Runs houdini engine session & server instance
	Bool StartHoudiniEngine( red::String& msg = red::String() );
	// Clean up and shutdown houdini engine session & server instance
	Bool ShutdownHoudiniEngine();

	houdini_legacy::HoudiniEngineSession* GetHoudiniSession() { return &m_houdiniSession; }

private:
	houdini_legacy::HoudiniEngineSession m_houdiniSession;
};
