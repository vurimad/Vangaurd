/*
* Copyright (c) 2020 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"
#include "dlcSystem.h"
#include "resourceDepot.h"
#include "resourceLoader.h"
#include "resourceToken.h"
#include "../../redJobs2/include/jobRunner.h"
#include "../../redContainers/include/string/stringUtils.h"

namespace res
{

DlcSystem GDlcSystem;

DlcSystem::DlcSystem()
	: m_entries( red::PoolEngine() )
	, m_initialised( false )
{
}

DlcSystem::~DlcSystem()
{
}

bool DlcSystem::Initialise()
{
#ifdef RED_ENABLE_DLC
	RED_FATAL_ASSERT( m_initialised == false );
	RED_FATAL_ASSERT( GetResourceDepot() != nullptr );
	if ( !GetResourceDepot() )
	{
		return false;
	}

	auto& depot = *GetResourceDepot();
	const auto rootPaths = depot.GetDLCRootPaths();
	RED_LOG( "DlcSystem: Found %u DLCs", rootPaths.Size() );

	m_entries.Reserve( rootPaths.Size() );
	for ( const auto& rootPath : rootPaths )
	{
		auto& entry = m_entries.EmplaceBack();
		entry.m_manifestPath = DlcManifest::GetPathForDLC( rootPath );
		RED_LOG( "DlcSystem: DLC '%hs' Manifest '%hs'", rootPath.AsChar(), entry.m_manifestPath.ToDebugString() );
	}
#endif // RED_ENABLE_DLC

	m_initialised = true;

	return true;
}

void DlcSystem::Shutdown()
{
	m_entries.Clear();
	m_initialised = false;
}

job::Counter DlcSystem::LoadManifests()
{
	RED_FATAL_ASSERT( m_initialised );

	job::Counter noWait;
	job::Counter counter;

#ifdef RED_ENABLE_DLC
	// load manifest for each dlc entry
	for ( auto& entry : m_entries )
	{
		auto token = GResourceLoader->IssueLoadingRequest( entry.m_manifestPath );
		job::DispatchJob< red::PoolEngine >( "DlcSystem::LoadManifests", token->GetWaitCounter(), counter, [token, &entry]( const job::RunContext& )
		{
			if ( token->IsLoaded() )
			{
				entry.m_manifestResource = Cast<DlcManifest>( token->GetResource() );
			}
		} );
	}
#endif // RED_ENABLE_DLC

	return counter;
}

void DlcSystem::UnnloadManifests()
{
	for ( auto& entry : m_entries )
	{
		entry.m_manifestResource.Reset();
	}
}

red::DynArray< THandle< CResource > > DlcSystem::GetManifests() const
{
	RED_FATAL_ASSERT( m_initialised );
	red::DynArray< THandle< CResource > > result{ red::PoolEngine() };
	result.Reserve( m_entries.Size() );
	for ( const auto& entry : m_entries )
	{
		result.PushBack( entry.m_manifestResource );
	}
	return result;
}

} // red
