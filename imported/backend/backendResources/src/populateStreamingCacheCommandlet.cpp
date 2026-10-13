/**
* Copyright (c) 2019 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"

#include "../../backendEngine/include/commandletEngineInstance.h"
#include "../../backendEngine/include/commandletUtils.h"
#include "../../backendEngine/include/streamingManagerInstance.h"
#include "../../../common/core/include/commandlet.h"
#include "../../../common/redCore/include/commandline.h"
#include "../../../common/resource/include/resourceEditorDepotImpl.h"
#include "../../../common/redReflection/include/streamedResourceManager.h"

class PopulateStreamingCacheCommandlet : public red::ICommandlet
{
	RTTI_DECLARE_TYPE( PopulateStreamingCacheCommandlet );

public:
	PopulateStreamingCacheCommandlet() : ICommandlet( RED_NAME_CONSTEXPR( "populate-streaming-cache" ) ) { }

	virtual Bool InitializeEnvironment() override;
	virtual void TeardownEnvironment() override;
	virtual void PrintHelp() const override { }
	virtual const char* GetOneLiner() const override { return "Populate streaming cache"; }
	virtual bool Execute( const red::CommandLine& options ) override;

private:
	res::StreamingManagerInstance m_streamingInstance;
	commandlet::EngineInstance m_engineInstance;
};


RTTI_BEGIN_TYPE( PopulateStreamingCacheCommandlet )
	RTTI_PARENT_TYPE( red::ICommandlet )
RTTI_END_TYPE()


Bool PopulateStreamingCacheCommandlet::InitializeEnvironment()
{
	return m_engineInstance.Initialise() && m_streamingInstance.Initialize();
}

void PopulateStreamingCacheCommandlet::TeardownEnvironment()
{
	m_streamingInstance.Shutdown();
	m_engineInstance.Shutdown();
}

bool PopulateStreamingCacheCommandlet::Execute( const red::CommandLine& options )
{

	res::StreamingResourceManager* streamingManager = GetStreamingResourceManager();

	auto resources = commandlet::ScanResourceDepotDirectories( *commandlet::GetEditorDepot(), "base\\", "*.world" );
	
	if ( options.HasOption( "gymsData" ) )
	{
		resources.PushBack( commandlet::ScanResourceDepotDirectories( *commandlet::GetEditorDepot(), "gyms\\", "*.world" ) );
	}

	if ( options.HasOption( "functionalTestsWorld" ) )
	{
		resources.PushBack( commandlet::ScanResourceDepotDirectories( *commandlet::GetEditorDepot(), "test\\func_test\\assets\\", "*.world" ) );
	}

	streamingManager->Load( false ); // Load for editor

	for ( const res::ResourcePath& path : resources )
	{
		RED_LOG_INFO( "PopulateStreamingCacheCommandlet: Loading streaming data for '%hs'", path.ToDebugString() );
		streamingManager->GetStreamingData( path );
	}

	streamingManager->Save();

	streamingManager->Load( true ); // Load for cmdlet
		 
	for( const res::ResourcePath& path : resources )
	{
		RED_LOG_INFO( "PopulateStreamingCacheCommandlet: Loading streaming data for '%hs'", path.ToDebugString() );
		streamingManager->GetStreamingData( path );
	}

	streamingManager->Save();

	return true;
}
