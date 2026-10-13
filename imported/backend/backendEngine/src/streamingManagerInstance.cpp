/*
 * Copyright (c) 2017 CD Projekt Red. All Rights Reserved.
 */

#include "build.h"
#include "streamingManagerInstance.h"

#include "../../../common/redReflection/include/streamedResourceManager.h"

namespace res
{

StreamingManagerInstance::StreamingManagerInstance()
	: m_streamingManager()
{
}

StreamingManagerInstance::~StreamingManagerInstance()
{
	RED_FATAL_ASSERT( m_streamingManager == nullptr, "Streaming Manager was not shut down correctly!" );
}

bool StreamingManagerInstance::Initialize()
{
	m_streamingManager = GetStreamingResourceManager();
	if ( !m_streamingManager )
	{
		RED_LOG_ERROR( "Engine: No streaming manager." );
		return false;
	}

	m_streamingManager->InitializeDataExtractors();

	return true;
}

void StreamingManagerInstance::Shutdown()
{
	m_streamingManager = nullptr;
}

void StreamingManagerInstance::Save()
{
	if ( m_streamingManager )
	{
		m_streamingManager->Save();
	}
}

} // res