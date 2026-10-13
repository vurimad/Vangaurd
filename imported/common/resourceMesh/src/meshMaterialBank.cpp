/*
* Copyright (c) 2018 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"
#include "../../resourceMaterial/include/materialInstance.h"
#include "meshMaterialBank.h"

namespace mesh
{

void MeshMaterialBank::RegisterMaterial(const res::ResourcePath& path, const THandle<CMaterialInstance>& material)
{
	RED_SCOPE_LOCK(m_resourceDictionaryLock);
	auto iter = m_resourceDictionary.Find(path);
	if (iter != m_resourceDictionary.End())
	{
		RED_FATAL_ASSERT(iter.Value().Expired(), "Internal Logic Error. Resource with same path already exist.");
		iter.Value() = material;
	}
	else
	{
		m_resourceDictionary.Insert(path, material);
	}
}

THandle< CMaterialInstance > MeshMaterialBank::FindMaterial(const res::ResourcePath& path) const
{
	RED_SCOPE_SHARED_LOCK(m_resourceDictionaryLock);

	auto iter = m_resourceDictionary.Find(path);
	if (iter != m_resourceDictionary.End())
	{
		const WeakHandle< CMaterialInstance > & resource = iter.Value();

		const THandle< CMaterialInstance > result = resource.ToHandle();
		return result;
	}

	return THandle< CMaterialInstance >();
}

void MeshMaterialBank::Clear()
{
	RED_SCOPE_LOCK(m_resourceDictionaryLock);
	m_resourceDictionary.Clear();
}

}
