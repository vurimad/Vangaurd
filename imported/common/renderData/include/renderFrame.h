/**
* Copyright (c) 2014 CD Projekt Red. All Rights Reserved.
*/
#pragma once


class IRenderFrame : public IRenderObject
{
	RED_USE_POLYMORPHIC_MEMORY_POOL( rend::PoolFrameInfo );

public:
	virtual ~IRenderFrame() {}

public:
	virtual CRenderFrameInfo& GetFrameInfo() = 0;
	virtual const CRenderFrameInfo& GetFrameInfo() const = 0;
};
