/*
 * Copyright (c) 2018 CD Projekt Red. All Rights Reserved.
 */

#include "build.h"
#include "cookerTask.h"
#include "cooker.h"
#include "dataErrorHandler.h"

#include "resourceDependency.h"

#include "../../../common/redFileSystem/include/fileSys.h"

namespace cooker
{

static constexpr Uint32 g_globalCookerVersion = 8;

Task::Task( const res::ResourcePath& resourcePath, data::SystemContext& system )
	: data::Task( resourcePath, system )
{
}

Task::~Task()
{
}

bool Task::InitialiseTool( job::Builder& builder )
{
	m_cooker = ICooker::Create( m_path );
	if ( m_cooker )
	{
		const Uint32 combinedVersion = ( g_globalCookerVersion << 16 ) | m_cooker->GetVersion();
		m_currentDeps.SetTool( m_cooker->GetName(), combinedVersion );
	}
	else
	{
		m_currentDeps.SetTool( CName(), ( g_globalCookerVersion << 16 ) );
	}

	m_currentDeps.SetOutputDepot( data::DepotLocation::Cooked );
	m_outputCollector.SetPrimary( m_path, data::DepotLocation::Cooked );
	m_buildSettings = data::BuildSettings::CLIENT | data::BuildSettings::SERVER;

	return true;
}

bool Task::GetDependenciesFromTool( job::Builder& builder )
{
	if ( m_cooker )
	{
		return m_cooker->CollectDependencies( m_path, builder, m_depsCollector, m_taskContext );
	}
	else
	{
		m_depsCollector.Add( MakeSourceDependency( m_path, dependency::Requirement::Required ) );
		return true;
	}
}

bool Task::BuildWithTool( job::Builder& builder )
{
	if ( m_cooker )
	{
		if ( !ResourceExistsOnDisk( m_path ) )
		{
			m_taskContext.ErrorHandler().ReportError( "Cooking: Resource '%hs' does not exist on disk! (Is it a link file?)", m_path.ToDebugString() );
			return false;
		}

		return m_cooker->Cook( m_path, builder, m_taskContext );
	}
	else
	{
		THandle< CResource > resource = cooker::LoadResource( m_path, m_taskContext.SourceDepot() );
		if ( !resource )
		{
			m_taskContext.ErrorHandler().ReportError( "Cooking: Failed to load '%hs' for cooking", m_path.ToDebugString() );
			return false;
		}

		bool saveResult = cooker::SaveResource( m_path, resource, m_taskContext.CookedDepot(), m_taskContext.TargetPlatform() );
		if ( !saveResult )
		{
			m_taskContext.ErrorHandler().ReportError( "Cooking: Failed to save resource '%hs'", m_path.ToDebugString() );
			return false;
		}

		return true;
	}
}

bool Task::ResourceExistsOnDisk( const res::ResourcePath& path ) const
{
	red::AbsolutePath fullPath = m_system.SourceDepot().TranslateResourcePath( path );
	if ( !fullPath.Empty() )
	{
		return GFileManager->FileExist( fullPath );
	}
	return false;
}

//------------------------------------------------------------------------------

data::TaskUniquePtr CreateTask( const res::ResourcePath& path, data::SystemContext& system )
{
	return red::CreateUniquePtr< cooker::Task >( path, system );
}

} // cooker
