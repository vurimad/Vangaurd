/*
* Copyright (c) 2019 CD Projekt Red. All Rights Reserved.
*/

#pragma once

#include "visualTags.h"

namespace tools
{
	class TweakDbInteropHelper
	{
		RED_USE_MEMORY_POOL( red::PoolBackend );

		DECLARE_INTEROP( TweakDbInteropHelper );

	public:
		TweakDbInteropHelper();
		virtual ~TweakDbInteropHelper();

	private:
		VisualTagHelper m_visualTags;
	};
}
