/**
* Copyright (c) 2016 CD Projekt Red. All Rights Reserved.
*/
#pragma once

#include "../../redMemory/include/atomicSharedPtr.h"
#include "../../redReflection/include/handle.h"


namespace vis
{

	class IOccluderResource;

	enum class EOccluderImportance
	{
		// Small occluder, generally not big enough to offer significant occlusion against _other occluders_.
		Detail,

		// Larger occluder, and can occlude other occluders pretty well, but only from close distances.
		Minor,

		// Large occluder, provides a high level of occlusion against other occluders.
		Major
	};


	struct RENDER_DATA_API OccluderInstanceInitInfo
	{
		THandle<IOccluderResource>	m_resource;
		Float						m_autoHideDistanceScale;
		Float						m_nearAutoHideDistance;
		EOccluderImportance			m_importance;
	};


	class RENDER_DATA_API IOccluderInstance
	{
		RED_USE_MEMORY_POOL( rend::PoolVisibilityScene );

	public:
		virtual ~IOccluderInstance();
	};

	typedef red::AtomicSharedPtr<IOccluderInstance> OccluderInstancePtr;


	struct OccluderAddInfo
	{
		OccluderInstancePtr occluder;
		math::Matrix localToWorld;
	};
	
}
