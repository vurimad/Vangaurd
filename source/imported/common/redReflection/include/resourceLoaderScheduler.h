/**
* Copyright (c) 2018 CD Projekt Red. All Rights Reserved.
*/

#pragma once
#include "resourceLoaderTypes.h"
#include "serializationLoader.h"

namespace job { struct RunContext; }
namespace job{ enum class JobPriority : Uint8; }
namespace job{ class CounterChain; }
namespace job { class CompletionDeferral; }
namespace serialization { class LoadingResult; }

namespace res
{
	class ResourceLoader;
	class ResourceBank;
	class IResourceDepot;
	struct IssueLoadingRequestParameter;
	class ResourceLoaderThrottler;

	struct ResourceLoaderSchedulerParameter
	{
		ResourceLoader * resourceLoader{ nullptr };
		ResourceBank * resourceBank{ nullptr };
		IResourceDepot* depot{ nullptr };
		Bool isCooked{ false };
	};

	// Because mocks can't handle rvalue parameters
	struct DeferralRvalArgWorkaroundForMocks
	{
		job::CompletionDeferral* movableDeferral{ nullptr };
	};

	class RED_REFLECTION_API ResourceLoaderScheduler
	{
		RED_USE_MEMORY_POOL( red::PoolEngine );

	public:
		ResourceLoaderScheduler();
		RED_MOCKABLE ~ResourceLoaderScheduler();

		void Initialize( const ResourceLoaderSchedulerParameter & param );
		
		RED_MOCKABLE void ScheduleResourceLoadingJobs( const ResourceTokenHandle& token, DeferralRvalArgWorkaroundForMocks arg, const res::ResourcePath& resolvedPath, const IssueLoadingRequestParameter& param );
        RED_MOCKABLE void CantTouchThis_ScheduleUnregisteredResourceLoadingJobs( const ResourceTokenHandle& token, DeferralRvalArgWorkaroundForMocks arg, const res::ResourcePath& resolvedPath, const IssueLoadingRequestParameter& param );

		ResourceLoaderThrottler* GetThrottler() const { return m_throttler.Get(); }

	private:
		static void StaticResourceLoadedCallback( const serialization::LoadingResult& result, const ResourceTokenWeakHandle& tokenUserData, const serialization::LoadingContext& loadingContext, job::CompletionDeferral&& tokenUserDeferral,  void* userData );
        static void StaticUnregisteredResourceLoadedCallback( const serialization::LoadingResult& result, const ResourceTokenWeakHandle& tokenUserData, const serialization::LoadingContext& loadingContext, job::CompletionDeferral&& tokenUserDeferral, void* userData );


		ResourceLoader * m_resourceLoader;
		ResourceBank * m_resourceBank;
		IResourceDepot* m_depot;
		red::UniquePtr< ResourceLoaderThrottler > m_throttler;
		red::Atomic< Uint32 > m_creationId;
		Bool m_isCooked;
	};

	red::UniquePtr< ResourceLoaderScheduler > CreateResourceLoaderScheduler( const ResourceLoaderSchedulerParameter & param );

}

