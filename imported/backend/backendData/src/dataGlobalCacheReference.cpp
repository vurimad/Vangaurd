/*
 * Copyright (c) 2018 CD Projekt Red. All Rights Reserved.
 */

#include "build.h"
#include "dataGlobalCacheReference.h"

#include "../../../common/redJobs2/include/jobRunner.h"

RTTI_BEGIN_ABSTRACT_TYPE_IN_NAMESPACE( GlobalCacheReference, data )
RTTI_END_TYPE()

namespace data
{

//------------------------------------------------------------------------------

GlobalCacheReference::~GlobalCacheReference() = default;

//------------------------------------------------------------------------------

bool GlobalCacheManager::s_initialised = false;

GlobalCacheManager::GlobalCacheManager()
	: m_globalCaches{ red::PoolBackend() }
	, m_lifetimesEnded( 0 )
{
}

GlobalCacheManager::~GlobalCacheManager()
{
	ShutAllGlobalCaches();
}

//------------------------------------------------------------------------------

void GlobalCacheManager::InitAllGlobalCaches()
{
	job::Counter counter;
	InitAllGlobalCaches( counter );
	job::FlushCounter( counter );
}

void GlobalCacheManager::ShutAllGlobalCaches()
{
	job::Counter counter;
	ShutAllGlobalCaches( counter );
	job::FlushCounter( counter );
}

void GlobalCacheManager::ShutGlobalCaches( Uint32 lifetimeMask )
{
	job::Counter counter;
	ShutGlobalCaches( lifetimeMask, counter );
	job::FlushCounter( counter );
}

void GlobalCacheManager::InitAllGlobalCaches( job::Counter& counter )
{
	RED_ASSERT( !s_initialised );
	s_initialised = true;

	red::DynArray< const rtti::ClassType* > cacheClasses;
	GetRttiSystem().EnumClasses( ClassID< GlobalCacheReference >(), cacheClasses );

	m_globalCaches.Reserve( cacheClasses.Size() );
	for ( const rtti::ClassType* cacheClass : cacheClasses )
	{
		m_globalCaches.PushBack( cacheClass->GetDefaultObject< GlobalCacheReference >() );
	}

	for ( auto* globalCache : m_globalCaches )
	{
		globalCache->Initialise( counter );
	}
}

namespace prv
{

static void ShutGlobalCaches( red::ArraySpan< GlobalCacheReference* > globalCaches, job::Counter& counter )
{
	job::Counter noWait;

	for ( auto* globalCache : globalCaches )
	{
		job::DispatchJob< red::PoolBackend >( "GlobalCache/ShutAll", noWait, counter, [globalCache]( const job::RunContext& context )
		{
			globalCache->Shutdown( *(context.continuationContext.counter) );
			const rtti::ClassType* classType = globalCache->GetClass();
			RED_FATAL_ASSERT( classType != nullptr, "Class type does not exist" );
			classType->DestroyObject( globalCache );
		} );
	}
}

} // prv

void GlobalCacheManager::ShutAllGlobalCaches( job::Counter& counter )
{
	prv::ShutGlobalCaches( m_globalCaches, counter );

	// Can clear the local array here since we've already copied the pointers to be shut down in jobs
	m_globalCaches.Clear();
}

void GlobalCacheManager::ShutGlobalCaches( Uint32 lifetimeMask, job::Counter& counter )
{
	m_lifetimesEnded |= lifetimeMask;

	auto shutIter = std::partition( m_globalCaches.Begin(), m_globalCaches.End(), [lifetimeMask = m_lifetimesEnded]( const GlobalCacheReference* globalCache ) -> bool
	{
		// Partition returns a range of [true, false]
		// so we return true if the cache should continue to live
		return ( globalCache->GetLifetime() & ~lifetimeMask ) != 0;
	} );

	prv::ShutGlobalCaches( red::MakeArraySpan( shutIter, m_globalCaches.End() ), counter );

	m_globalCaches.Remove( shutIter, m_globalCaches.End() );
}

//------------------------------------------------------------------------------

} // data


