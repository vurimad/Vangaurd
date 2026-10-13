/**
* Copyright (c) 2016 CD Projekt Red. All Rights Reserved.
*/

#pragma once

#include "../../../common/interop/include/interopUtilsManual.h"

namespace red
{
	class ProfilerBackend
	{
		DECLARE_INTEROP( ProfilerBackend );
		RED_USE_MEMORY_POOL( red::PoolBackend );

	public:
		ProfilerBackend();
		virtual ~ProfilerBackend();

		void Update();

	private:
	};

}