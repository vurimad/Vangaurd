/*
* Copyright (c) 2019 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"
#include "containersSerialization.h"
#include "rttiClassBuilder.h"
#include "resourceSnapshot.h"

RTTI_BEGIN_TYPE_IN_NAMESPACE(ResourceSnapshot, res);
	RTTI_PARENT_TYPE(CResource);
	RTTI_PROPERTY(m_resources);
RTTI_END_TYPE();

namespace res
{

ResourceSnapshot::ResourceSnapshot(red::DynArray<TResAsyncRef<CResource>>&& resources)
	: m_resources(std::move(resources))
{
}

ResourceSnapshot::ResourceSnapshot()
	: m_resources( red::PoolEngine() )
{
}

ResourceSnapshot::~ResourceSnapshot() = default;

const red::DynArray<TResAsyncRef<CResource>>& ResourceSnapshot::GetResources() const
{
	return m_resources;
}

}
