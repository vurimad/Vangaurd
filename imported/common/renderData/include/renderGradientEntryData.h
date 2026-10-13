/**
* Copyright (c) 2019 CD Projekt Red. All Rights Reserved.
*/

#pragma once

#include "renderingMemory.h"

namespace rend
{
	struct RENDER_DATA_API GradientEntry : public ISerializable
	{
		RED_USE_MEMORY_POOL( rend::PoolRenderData );
		RTTI_DECLARE_TYPE( GradientEntry );
		RED_BASE_CLASS( ISerializable );

		Float	m_value;
		Color	m_color;
	};
} // rend
