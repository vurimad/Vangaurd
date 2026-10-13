/**
* Copyright (c) 2020 CD Projekt Red. All Rights Reserved.
*/
#pragma once

class RenderProxyHandle;

class IRenderProxySystemInterface
{
public:
	~IRenderProxySystemInterface() = default;

	virtual void OnRenderProxyHandleDestroyed( RenderProxyHandle* proxyHandle ) = 0;
};