/**
* Copyright (c) 2026 RED Vanguard, All Rights Reserved.
*/
#include "build.h"
#include "gatheredResource.h"
#include "resourceLoader.h"
#include "resourceToken.h"
#include "resourceClassLookup.h"

#include "../../redCore/include/globalModeInfo.h"
#include "../../redJobs2/include/jobSystem.h"
#include "../../redSystem/include/stopWatch.h"
#include "../../redSystem/include/bitUtils.h"

//------------------------------------------------------------------------------

namespace res
{

extern THandle< CResource > s_nullResource;

namespace prv
{
// Function to lazy-init the global list of static resources
// Used instead of just a static variable so static initialisation order is correct
red::DynArray< GatheredResource* >& GetGatheredResources()
{
	static red::DynArray< GatheredResource* > s_gatheredResources{ red::PoolEngine() };
	return s_gatheredResources;
}

#ifndef RED_CONFIGURATION_FINAL
red::DynArray< GatheredResourceReference* >& GetGatheredResourceReferences()
{
	static red::DynArray< GatheredResourceReference* > s_gatheredResourceReferences{ red::PoolEngine() };
	return s_gatheredResourceReferences;
}
#endif

//------------------------------------------------------------------------------

static red::DynArray< red::String >& Internal_GetGatheredResourceErrors()
{
	static red::DynArray< red::String > s_gatheredResourceErrors{ red::PoolEngine() };
	return s_gatheredResourceErrors;
}

static bool Internal_LoadGatheredResources( const red::DynArray< GatheredResource* >& resources )
{
	job::Counter gatheredCounter{ job::Priority::Latent };
	for ( GatheredResource* resource : resources )
	{
		resource->Internal_Load( gatheredCounter );
	}
	job::FlushCounter( gatheredCounter );

#if defined( RED_ASSERTS_ENABLED ) || defined( RED_LOGGING_ENABLED )
	bool allLoaded = true;
	for ( GatheredResource* resource : resources )
	{
		if ( !resource->IsLoaded() )
		{
			auto errorType = resource->Internal_GetLoadingToken()->GetError();
			const char* errorText = res::GetResourceTokenErrorTypeText( errorType );
			const char* debugPath = resource->GetPath().ToDebugString();
			RED_LOG_ERROR( "GatheredResource: '%hs' failed to load, reason: %u - %hs", debugPath, static_cast< Uint32 >( errorType ), errorText );

			if ( ! resource->IsDebug() )
			{
				auto errorStr = String::Printf( "Failed to load '%hs' reason %hs", debugPath, errorText );
				Internal_GetGatheredResourceErrors().PushBack( errorStr );
				allLoaded = false;
			}
		}
	}

	return allLoaded;
#else
	return std::all_of( resources.Begin(), resources.End(), []( const GatheredResource* resource ) { return resource->IsLoaded(); } );
#endif
}

//------------------------------------------------------------------------------

enum class GatheredSystemState
{
	INITIAL = 0,
	LOADED_ENGINE,
	LOADED_BACKEND,
	RELEASED,
};

static GatheredSystemState s_gatheredState = GatheredSystemState::INITIAL;

static bool IsGameRootGatheredResource( const GatheredResource* resource )
{
	return resource->GetPath().ToStringView().StartsWith( "base\\" );
}

bool LoadEngineGatheredResources( Uint32 flags )
{
	PC_SCOPE( LoadEngineGatheredResources );

	RED_FATAL_ASSERT( s_gatheredState == GatheredSystemState::INITIAL, "Loading gathered resources for engine must take place first" );

	// Do the initial mapping of the resource classes, this only needs to be done once and here is as good a spot as any
	ResourceClassLookup::GetInstance();

	red::StopWatch simpleTimer;

	red::DynArray< GatheredResource* > resources{ GetGatheredResources(), red::PoolEngine() };
	{
		auto iter = resources.End();
		iter = std::remove_if( resources.Begin(), iter, []( const GatheredResource* resource ) { return resource->IsDelayLoaded(); } );
		iter = std::remove_if( resources.Begin(), iter, []( const GatheredResource* resource ) { return ! resource->IsEngine(); } );
		if ( (flags & GRLF_OnlyHeadless) != 0 )
		{
			iter = std::remove_if( resources.Begin(), iter, []( const GatheredResource* resource ) { return ! resource->IsHeadless(); } );
		}
		if ( (flags & GRLF_SkipGameRoot) != 0 )
		{
			iter = std::remove_if( resources.Begin(), iter, IsGameRootGatheredResource );
		}
		resources.Remove( iter, resources.End() );
	}

	bool result = Internal_LoadGatheredResources( resources );

	s_gatheredState = GatheredSystemState::LOADED_ENGINE;

	RED_LOG( "Loading gathered resources for engine took %0.3lf ms", simpleTimer.GetDeltaMS() );
	return result;
}

bool LoadBackendGatheredResources( Uint32 flags )
{
	PC_SCOPE( LoadBackendGatheredResources );

	RED_FATAL_ASSERT( s_gatheredState == GatheredSystemState::LOADED_ENGINE, "Loading gathered resources for backend must be after engine resources are loaded" );

	red::StopWatch simpleTimer;

	red::DynArray< GatheredResource* > resources{ GetGatheredResources(), red::PoolEngine() };
	{
		auto iter = resources.End();
		iter = std::remove_if( resources.Begin(), iter, []( const GatheredResource* resource ) { return resource->IsDelayLoaded(); } );
		iter = std::remove_if( resources.Begin(), iter, []( const GatheredResource* resource ) { return ! resource->IsBackend(); } );
		if ( (flags & GRLF_OnlyHeadless) != 0 )
		{
			iter = std::remove_if( resources.Begin(), iter, []( const GatheredResource* resource ) { return ! resource->IsHeadless(); } );
		}
		resources.Remove( iter, resources.End() );
	}

	bool result = Internal_LoadGatheredResources( resources );

	s_gatheredState = GatheredSystemState::LOADED_BACKEND;

	RED_LOG( "Loading gathered resources for backend took %0.3lf ms", simpleTimer.GetDeltaMS() );
	return result;
}

void ReleaseAllGatheredResources()
{
	PC_SCOPE( ReleaseAllGatheredResources );

	RED_FATAL_ASSERT( s_gatheredState != GatheredSystemState::RELEASED );

	s_gatheredState = GatheredSystemState::RELEASED;
	for ( GatheredResource* resource : GetGatheredResources() )
	{
		resource->Internal_Release();
	}
}

const red::DynArray< red::String >& GetGatheredResourceErrors()
{
	return prv::Internal_GetGatheredResourceErrors();
}

red::DynArray< GatheredResource* > GetAllGatheredResources()
{
	return GetGatheredResources();
}

#ifndef RED_CONFIGURATION_FINAL
red::DynArray< GatheredResourceReference* > GetAllGatheredResourceRefernces()
{
	return GetGatheredResourceReferences();
}

red::DynArray< res::ResourcePath > GetCookedGatheredResourcePaths()
{
	const auto& gatheredResources = GetAllGatheredResourceRefernces();
	red::DynArray< res::ResourcePath > result{ red::PoolEngine() }; // Should be PoolBackend really
	result.Reserve( gatheredResources.Size() );

	for ( const auto& gathered : gatheredResources )
	{
		if ( gathered->IsCooked() )
		{
			result.PushBack( gathered->GetPath() );
		}
	}

	return result;
}

red::DynArray< res::ResourcePath > GetStartupGatheredResourcePaths()
{
	const auto& gatheredResources = GetAllGatheredResourceRefernces();
	red::DynArray< res::ResourcePath > result{ red::PoolEngine() }; // Should be PoolBackend really
	result.Reserve( gatheredResources.Size() );

	for ( const auto& gathered : gatheredResources )
	{
		if ( gathered->IsCooked() && gathered->IsLoadedOnStartup() )
		{
			result.PushBack( gathered->GetPath() );
		}
	}

	return result;
}
#endif

//------------------------------------------------------------------------------

#ifndef RED_CONFIGURATION_FINAL
void AddResource( GatheredResourceReference* resource )
{
	RED_FATAL_ASSERT( s_gatheredState == GatheredSystemState::INITIAL, "Can only construct GatheredResourceReferences statically!" );
	GetGatheredResourceReferences().PushBack( resource );
}

void RemoveResource( GatheredResourceReference* resource )
{
	RED_FATAL_ASSERT( s_gatheredState != GatheredSystemState::RELEASED, "Should only release GatheredResourceReferences before shutdown!" );
	GetGatheredResourceReferences().Remove( resource );
}
#endif

void AddResource( GatheredResource* resource )
{
	RED_FATAL_ASSERT( s_gatheredState == GatheredSystemState::INITIAL, "Can only construct GatheredResources statically!" );
	GetGatheredResources().PushBack( resource );
}

void RemoveResource( GatheredResource* resource )
{
	RED_FATAL_ASSERT( s_gatheredState != GatheredSystemState::RELEASED, "Should only release GatheredResources before shutdown!" );
	GetGatheredResources().Remove( resource );
}

} // prv

//------------------------------------------------------------------------------

GatheredResourceReference::GatheredResourceReference( const res::ResourcePath path, Uint32 flags )
	: m_path( path )
	, m_flags( flags )
{
	if ( (m_flags & GRF_Backend) != 0 )
	{
		m_flags |= GRF_NotCooked;
	}
	if ( ( m_flags & (GRF_Backend | GRF_Fallback | GRF_Debug) ) != 0 )
	{
		m_flags |= GRF_SkipOnHeadless;
	}

#ifndef RED_CONFIGURATION_FINAL
	prv::AddResource( this );
#endif
}

GatheredResourceReference::~GatheredResourceReference()
{
#ifndef RED_CONFIGURATION_FINAL
	if ( !IsClosingMode() )
	{
		prv::RemoveResource( this );
	}
#endif
}

const res::ResourcePath& GatheredResourceReference::GetPath() const
{
	return m_path;
}

Uint32 GatheredResourceReference::Internal_GetFlags() const
{
	return m_flags;
}

void GatheredResourceReference::Internal_SetFlags( Uint32 flags )
{
	m_flags = flags;
}

//------------------------------------------------------------------------------

GatheredResource::GatheredResource( const res::ResourcePath path, Uint32 flags )
	: GatheredResourceReference( path, flags )
{
	prv::AddResource( this );
}

GatheredResource::~GatheredResource()
{
	if ( !IsClosingMode() )
	{
		Internal_Release();
		prv::RemoveResource( this );
	}
}

const THandle< CResource >& GatheredResource::Get() const
{
	if ( IsLoaded() )
	{
		return m_token->GetResource();
	}
	return res::s_nullResource;
}

const THandle< CResource >& GatheredResource::GetSafe() const
{
	RED_FATAL_ASSERT( m_token && m_token->IsLoaded(), "Missing or unloaded critical resource '%hs'", m_path.ToDebugString() );
	return m_token->GetResource();
}

bool GatheredResource::IsLoaded() const
{
	return m_token && m_token->IsLoaded();
}

res::ResourceTokenHandle GatheredResource::IssueLoadingRequest( io::EAsyncPriority priority )
{
	if ( !m_token )
	{
		res::IssueLoadingRequestParameter param{ m_path };
		param.priority = priority;
		m_token = GResourceLoader->IssueLoadingRequest( param );
	}

	return m_token;
}

res::ResourceTokenHandle GatheredResource::IssueLoadingRequest( const LoadingOptions& options, io::EAsyncPriority priority )
{
	if ( !m_token )
	{
		auto param = Convert( m_path, options );
		param.priority = priority;
		m_token = GResourceLoader->IssueLoadingRequest( param );
	}

	return m_token;
}

// This is needed to load rendering dependencies so that the other resources can be loaded correctly
void GatheredResource::Internal_EnsureLoaded()
{
	if ( !m_token )
	{
		auto param = res::IssueLoadingRequestParameter( m_path );
		m_token = GResourceLoader->IssueLoadingRequest( param );
	}

	if ( m_token && !m_token->HasFinished() )
	{
		m_token->WaitUntilLoaded();
	}
}

void GatheredResource::Internal_Load( job::Counter& accumulator )
{
	if ( !m_token )
	{
		auto param = res::IssueLoadingRequestParameter( m_path );
		m_token = GResourceLoader->IssueLoadingRequest( param );
		accumulator += m_token->GetWaitCounter();
	}
}

void GatheredResource::Internal_Release()
{
	m_token.Reset();
}

const res::ResourceTokenHandle& GatheredResource::Internal_GetLoadingToken() const
{
	return m_token;
}

} // res

//------------------------------------------------------------------------------
