/*
* Copyright (c) 2019 CD Projekt Red. All Rights Reserved.
*/

#pragma once

namespace world
{
	class Prefab;
}

namespace tools
{

class BACKEND_ENGINE_API IPrefabProxyReconciler
{
	RTTI_DECLARE_POLYMORPHIC_TYPE(IPrefabProxyReconciler);
	RED_USE_POLYMORPHIC_MEMORY_POOL( red::PoolBackend );

public:
	virtual ~IPrefabProxyReconciler() = 0;

	virtual void GetAllPrefabPathsRecursively(const res::ResourcePath& rootPrefabPath, red::DynArray<res::ResourcePath>& outPrefabPaths) const = 0;
	virtual red::DynArray< res::ResourcePath > GetReconciledPrefabPaths() const = 0;
	virtual void ReconcilePrefabProxies(const red::DynArray<res::ResourcePath>& prefabPaths) = 0;
	virtual void FinishReconciledPrefabs() = 0;

};

}