/*
 * Copyright (c) 2017 CD Projekt Red. All Rights Reserved.
 */

#include "build.h"
#include "generatorTask.h"

#include "generator.h"
#include "dataErrorHandler.h"

#include "../../../common/redFileSystem/include/fileVersionList.h"

namespace generator
{


Task::Task( const res::ResourcePath& resourcePath, data::SystemContext& system )
	: data::Task( resourcePath, system )
{
}

Task::~Task()
{
}

bool Task::InitialiseTool( job::Builder& builder )
{
	m_generator = IGenerator::Create( m_path );
	if ( !m_generator )
	{
		ErrorHandler().ReportError( "Could not find generator to create the resource %hs", m_path.ToDebugString() );
		return false;
	}

	if ( !m_generator->InitialiseEnvironment( m_path, builder, m_system ) )
	{
		ErrorHandler().ReportError( "Could not initialise the generator environment for %hs", m_path.ToDebugString() );
		return false;
	}

	m_currentDeps.SetTool( m_generator->GetName(), m_generator->GetVersion() );

	const auto outputDepot = m_generator->GetOutputDepot();
	m_currentDeps.SetOutputDepot( outputDepot );
	m_outputCollector.SetPrimary( m_path, outputDepot );

	m_cacheStrategy = m_generator->GetCacheStrategy();
	if ( m_cacheStrategy == data::CacheStrategy::GET_LATEST_CLIENT_EXACT_SERVER )
	{
		if ( m_system.IsServerBuild() )
		{
			m_cacheStrategy = data::CacheStrategy::GET_EXACT;
		}
		else
		{
			m_cacheStrategy = data::CacheStrategy::GET_LATEST;
		}
	}
	m_buildSettings = m_generator->GetBuildSettings();

	if ( IsIsolated() )
	{
		m_isolationGroup = GetIsolationGroup( m_generator->GetName() );
	}

	return true;
}

bool Task::GetDependenciesFromTool( job::Builder& builder )
{
	RED_FATAL_ASSERT( m_generator != nullptr );
	return m_generator->CollectDependencies( m_path, builder, m_depsCollector, m_taskContext );
}

bool Task::BuildWithTool( job::Builder& builder )
{
	RED_FATAL_ASSERT( m_generator != nullptr );
	return m_generator->Generate( m_path, builder, m_taskContext );
}

//------------------------------------------------------------------------------

data::TaskUniquePtr CreateTask( const res::ResourcePath& path, data::SystemContext& system )
{
	return red::CreateUniquePtr< generator::Task >( path, system );
}

} // generator
