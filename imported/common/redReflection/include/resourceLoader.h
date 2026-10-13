/**
* Copyright (c) 2018 CD Projekt Red. All Rights Reserved.
*/

#pragma once
#include "resourceLoaderTypes.h"
#include "handle.h"

namespace serialization { class MemoryAllocator; }
namespace serialization { class LoadingContext; }
namespace job { class CompletionDeferral; }

namespace res
{
	class IResourceDepot;
	class ResourceBank;
	class ResourceMetricsBank;
	class ResourceLoaderScheduler;
	class ResourceLoaderThrottler;
	class ResourceSnapshot;
	class ResourceSwapList;

	enum class ResourceTokenErrorType : Int8;

#ifdef RED_ENABLE_RENDER_DEBUG
	RED_REFLECTION_API Bool GetWasColdLoadedThreadLocal_ForRenderDebug();
#endif

	class RED_REFLECTION_API ResourceLoader
	{
		RED_USE_MEMORY_POOL( red::PoolEngine );

	public:
		static IssueLoadingRequestParameter GetImportLoadingRequestParameter(const res::ResourcePath& importPath, const serialization::LoadingContext& parentContext, const serialization::RawDiskPosition& ioHint = serialization::RawDiskPosition{} );

		ResourceLoader();
		RED_MOCKABLE ~ResourceLoader();

		void Initialize( IResourceDepot* depot, bool isGame );
		void Shutdown();

		void ApplyCensorship( red::UniquePtr< ResourceSwapList > censorSwapList );

		RED_MOCKABLE ResourceTokenHandle IssueLoadingRequest( const ResourcePath& path );
		RED_MOCKABLE ResourceTokenHandle IssueLoadingRequest( const IssueLoadingRequestParameter & param );

		void IssueLoadingBulkRequest(const IssueLoadingRequestParameter& param, const red::ArraySpan<const res::ResourcePath>& paths, red::DynArray<res::ResourceTokenHandle>& outResults);

		ResourceTokenHandle IssueReloadingRequest( const IssueLoadingRequestParameter & param );

		bool IsResourceLoading( const ResourcePath & path ) const;

		// Sync to the current set of resources loaded/loading.
		void SyncLoadingFence( job::Counter& counter );

		// START DEPRECATED INTERFACE

		ResourceLoaderThrottler* GetThrottler() const;

		// ctremblay ResourceBank function. ResourceBank should be used in future for those kind of operation.
		// If resource is fully loaded, TryAcquiringResource will return resource.
		// Else if it's in the process of begin loaded, or not loaded, it will return null.
		THandle< CResource > TryAcquiringLoadedResource( const ResourcePath & path );
		bool IsResourceLoaded( const ResourcePath & path ) const;

		void EnumLoadedResources( red::DynArray< ResourcePath >& outPaths ) const;

		void IsResourceLoadedBulkQuery( const red::ArraySpan< const ResourcePath >& paths, red::BitSet64Dynamic& outResults ) const;

		ResourceMetricsBank* GetResourceMetricsBank() const;

		ResourceTokenHandle RegisterExistingResource( const ResourcePath & path, const THandle< CResource > & resource );

		// STOP DEPRECATED INTERFACE

		void GetLoadingResourcesForDebug( red::DynArray< res::ResourcePath >& outResources ) const;
		void GetLockedResourceSnapshotForDebug(red::DynArray< res::ResourcePath >& outResources) const;

#ifndef RED_CONFIGURATION_FINAL
		void SetDebugBreakOnLoadResource(const res::ResourcePath& resourcePath);
#endif

		THandle< ResourceSnapshot > CreateResourceBankSnapshot(Bool includeExpired = false) const;

		void LockSingleResourceSnapshot( const THandle< ResourceSnapshot > & snapshot );
		void UnlockSingleResourceSnapshot();
		Bool IsSingleResourceSnapshotLocked() const;

		void Update();
		void EnableGameCacheLoadingMode(Bool value);

		void ShutdownGameCacheManuallyBecauesRenderingCrashesOtherwise();

		Uint32 GetNumGameCacheEntries() const;

		void FlushResourceCache();

        // If you don't know what the name mean, then you don't need that function.
        // However, If you still think you need that function, please contact auszakow or ctremblay.
        // Any unjustified use will be hunted ;)
        ResourceTokenHandle CantTouchThis_IssueUnregisteredLoading( const IssueLoadingRequestParameter & param );

		// Failed token are kept alive forever. This reduce hammering job scheduling that are going to result in failure anyway.
		// Also as a bonus, allow to quickly list all failed request !
		void Internal_RegisterFailedToken( const ResourceTokenHandle& token, ResourceTokenErrorType errorType, job::CompletionDeferral&& deferral );

		// Inject token. If succeed, return the one pass as parameter. Else return already existing one.
		ResourceTokenHandle Internal_TryRegisterResourceToken( const ResourceTokenHandle& token );
		ResourceTokenHandle Internal_GetResourceToken( const res::ResourcePath& path );
		void Internal_TrySchedulingLoadingJobs( const ResourceTokenHandle& token, job::CompletionDeferral&& deferral );
		
		void Internal_RegisterResource( const res::ResourcePath& path, const THandle< CResource > & resource );
		THandle< CResource > Internal_TryRegisterResource( const res::ResourcePath& path, const THandle< CResource >& resource );

		// UNIT TEST ONLY!
		void Internal_RegisterResourceToken( const ResourceTokenHandle& token, job::CompletionDeferral&& deferral );
		void Internal_SetResourceBank( red::UniquePtr< ResourceBank > bank );
		void Internal_SetResourceLoaderScheduler( red::UniquePtr< ResourceLoaderScheduler > scheduler );

		// For special case CYB-591516:
		// Localization manager asks for vo map file which don't exist yet, because it's still installing
		// and we're caching that token with `ResourceNotExist` error, and later when it does exist
		// we've got this bad token
		void Internal_UnregisterFailedToken( const ResourceTokenHandle& token );

	private:

		typedef red::HashMap< ResourcePath, ResourceTokenWeakHandle > ResourceTokenDictionary;
		typedef red::DynArray< ResourceTokenHandle > FailedResourceTokenDictionary;

		ResourceTokenHandle TrySchedulingLoadingJobs( const res::ResourcePath& resolvedPath, const IssueLoadingRequestParameter & param );
		void TrySchedulingLoadingJobsBulk(const red::ArraySpan< const res::ResourcePath >& resolvedPaths, red::DynArray<res::ResourceTokenHandle>& outResults, const IssueLoadingRequestParameter& param);
		void ScheduleLoadingJob( const ResourceTokenHandle& token, job::CompletionDeferral&& deferral, const res::ResourcePath& resolvedPath, const IssueLoadingRequestParameter & param );
		ResourceTokenHandle TryAcquireResourceToken_NoLock( const res::ResourcePath& resolvedPath );
		void UpdateLoadingStats(const res::ResourceTokenHandle& token, Bool wasLoadedCold);

		ResourceTokenDictionary m_resourceTokenDictionary{ red::PoolEngine() };
		FailedResourceTokenDictionary m_failedResourceTokenContainer{ red::PoolEngine() };
		mutable red::RWSpinLock m_resourceTokenLock;

		red::UniquePtr< ResourceBank > m_resourceBank;
		red::UniquePtr< ResourceLoaderScheduler > m_scheduler;
		red::UniquePtr< ResourceMetricsBank > m_resourceMetricsBank;

#ifndef RED_CONFIGURATION_FINAL
		red::Set< res::ResourcePath > m_debugBreakOnLoadingRequest{ red::PoolDebug() };
		mutable red::RWSpinLock m_debugBreakOnLoadingRequestLock;
#endif

		mutable red::RWSpinLock m_resourceSnapshotLock;
		red::DynArray< THandle< CResource > > m_lockedResourceSnapshot{ red::PoolEngine() };
		Bool m_isResourceSnapshotLocked;
	};

	RED_REFLECTION_API red::UniquePtr< ResourceLoader > CreateResourceLoader( IResourceDepot* depot, bool isCooked );
}

// ctremblay: This got to go. But one step at a time. Else refactor will take forever.
extern RED_REFLECTION_API res::ResourceLoader* GResourceLoader;
