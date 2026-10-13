/**
* Copyright (c) 2019 CD Projekt Red. All Rights Reserved.
*/

#pragma once

#include "handle.h"
#include "resource.h"
#include "resourceAsyncReference.h"

namespace res
{
	class RED_REFLECTION_API ResourceSnapshot final : public CResource
	{
		RTTI_DECLARE_TYPE(ResourceSnapshot);
		IMPLEMENT_RESOURCE_INTERFACE("rsnapshot", "rsnapshot", "Resource snapshot");
		RED_USE_MEMORY_POOL(red::PoolEngine);

	public:
		explicit ResourceSnapshot(red::DynArray<TResAsyncRef<CResource>>&& loadedResourceHashes);
		ResourceSnapshot();
		virtual ~ResourceSnapshot();

		const red::DynArray<TResAsyncRef<CResource>>& GetResources() const;

	private:
		red::DynArray<TResAsyncRef<CResource>> m_resources;
	};
}
