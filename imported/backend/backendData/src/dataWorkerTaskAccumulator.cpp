/*
 * Copyright (c) 2019 CD Projekt Red. All Rights Reserved.
 */

#include "build.h"
#include "dataWorkerTaskAccumulator.h"

#include "../../../common/redContainers/include/string/stringUtils.h"
#include "../../../common/redFileSystem/include/fileSys.h"
namespace data
{

WorkerTaskAccumulator::WorkerTaskAccumulator()
{
}

WorkerTaskAccumulator::~WorkerTaskAccumulator()
{
}

bool WorkerTaskAccumulator::SaveList( const red::AbsolutePath& outputPath )
{
	RED_SCOPE_LOCK( m_guard );

	// Note: Saving the result even if empty so that the file gets cleared

	std::sort( m_taskPaths.Begin(), m_taskPaths.End(), res::SortByPathString );

	red::DynArray< red::StringView > strings{ m_taskPaths.Size(), red::PoolBackend() };
	for ( Uint32 i = 0; i < m_taskPaths.Size(); ++i )
	{
		strings[i] = m_taskPaths[i].ToStringView();
	}
	red::String text = red::StrJoin( strings, "\n" );

	return red::SaveStringToFile( outputPath, text );
}

void WorkerTaskAccumulator::AddTask( const res::ResourcePath& path )
{
	RED_SCOPE_LOCK( m_guard );
	m_taskPaths.PushBack( path );
}

} // data

