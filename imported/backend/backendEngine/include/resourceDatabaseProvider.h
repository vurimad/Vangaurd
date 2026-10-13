/*
* Copyright (c) 2019 CD Projekt Red. All Rights Reserved.
*/

#pragma once

namespace res
{
class IBackendResourceDepot;
}

namespace job
{
class Counter;
}

namespace resdb
{
	// Wrapper for the Resource Database for use in the editor only
	// If you need to use this in a commandlet go use resdb::Database directly!
	class BACKEND_ENGINE_API IResourceDatabaseProvider
	{
		RTTI_DECLARE_POLYMORPHIC_TYPE(IResourceDatabaseProvider);
		RED_USE_POLYMORPHIC_MEMORY_POOL(red::PoolEngine);

	public:
		virtual ~IResourceDatabaseProvider() = 0;
		virtual red::AbsolutePath GetDefaultFilePath() const = 0;
		virtual void SetReadOnly(Bool value) = 0;
		virtual void Initialise(res::IBackendResourceDepot& depot, const red::AbsolutePath& cachePath, job::Counter& counter) = 0;
		virtual void Shutdown() = 0;
		virtual void Update() = 0;
		virtual void GetForwardLinks(const res::ResourcePath& path, red::DynArray<res::ResourcePath>& outLinks ) const = 0;
		virtual void GetBackwardLinks(const res::ResourcePath& path, red::DynArray<res::ResourcePath>& outLinks) const = 0;
	};
}