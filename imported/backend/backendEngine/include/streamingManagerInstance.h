/*
 * Copyright (c) 2017 CD Projekt Red. All Rights Reserved.
 */

#pragma once

namespace res
{

class StreamingResourceManager;

// Simple class to wrap up initialisation and shutdown of the streaming manager
class BACKEND_ENGINE_API StreamingManagerInstance
{
public:
	StreamingManagerInstance();
	~StreamingManagerInstance();

	bool Initialize();
	void Shutdown();

	void Save();

private:
	StreamingResourceManager* m_streamingManager;
};

} // res
