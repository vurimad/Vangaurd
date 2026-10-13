/*
* Copyright (c) 2018 CD Projekt Red. All Rights Reserved.
*/

#pragma once

class CMaterialInstance;

namespace mesh
{

// Custom lightweight version of resourcebank, since this will be in every mesh
// and wouldn't use half of the regular ResourceBank functionality
class MeshMaterialBank : red::NonCopyable
{
public:
	void RegisterMaterial(const res::ResourcePath& path, const THandle<CMaterialInstance>& material);

	THandle< CMaterialInstance > FindMaterial(const res::ResourcePath& path) const;

	void Clear();

private:
	red::Map< res::ResourcePath, WeakHandle< CMaterialInstance > > m_resourceDictionary{ rend::PoolMesh() };
	mutable red::RWSpinLock m_resourceDictionaryLock; //#tbd: share a lock with meshMaterialBuffer
};

}
