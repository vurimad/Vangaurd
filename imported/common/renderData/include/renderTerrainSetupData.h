/**
* Copyright (c) 2016 CD Projekt Red. All Rights Reserved.
*/

#pragma once

#include "renderingMemory.h"

namespace rend
{
// 	struct RENDER_DATA_API TerrainSetupEntry : public ISerializable
// 	{
// 		RED_USE_MEMORY_POOL( rend::PoolRenderData );
// 		RTTI_DECLARE_TYPE( TerrainSetupEntry );
// 		RED_BASE_CLASS( ISerializable );
// 
// 		Float	m_uvTiling;
// 		CName	m_physicalMaterialName;
// 	};

	struct TerrainMaterialData
	{
		RED_USE_MEMORY_POOL( rend::PoolRenderData );

		red::DynArray< Float > m_tiling{rend::PoolRenderData()};
	
		TerrainMaterialData()
		{
			m_tiling.Reserve( 10 );
		}
	
		void Invalidate()
		{
			m_tiling.Clear();
		}
	};
} // rend
