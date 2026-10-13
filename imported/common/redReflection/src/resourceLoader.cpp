/**
* Copyright (c) 2018 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"
#include "resource.h"
#include "resourceLoader.h"
#include "resourceToken.h"
#include "resourceBank.h"
#include "resourceLoaderScheduler.h"
#include "resourceMetricsBank.h"

#include "../../redSystem/include/unitTestMode.h"
#include "../../redJobs2/include/jobCounter.h"
#include "../../redJobs2/include/jobDeferral.h"
#include "../../redCore/include/absolutePath.h"
#include "resourceClassLookup.h"
#include "resourceSnapshot.h"

res::ResourceLoader* GResourceLoader = nullptr;

namespace res
{
	IssueLoadingRequestParameter ResourceLoader::GetImportLoadingRequestParameter( const res::ResourcePath& importPath, const serialization::LoadingContext& parentContext, const serialization::RawDiskPosition& ioHint )
	{
		auto param = IssueLoadingRequestParameter( importPath );
		{
			param.parentPath = parentContext.m_initialResourcePath;
			param.priority = parentContext.m_priority;
			param.skipPostLoad = parentContext.m_skipPostLoad;
			param.skipBuffers = parentContext.m_skipBuffers;
			param.postLoadFlags = parentContext.m_postLoadFlags;
			param.verifyExportTypes = parentContext.m_verifyExportTypes;
			param.ioHint = ioHint;
			param.useGameCache = parentContext.m_useGameCache;
		}
		return param;
	}

	ResourceLoader::ResourceLoader()
		: m_resourceTokenDictionary( red::PoolEngine() )
		, m_failedResourceTokenContainer( red::PoolBackend() )
		, m_isResourceSnapshotLocked( false )
	{}

	ResourceLoader::~ResourceLoader()
	{}

	void ResourceLoader::Initialize( IResourceDepot* depot, bool isCooked )
	{
		m_resourceBank = CreateResourceBank( depot );

		// Note: in the game, we shouldn't skip postload, so the resource platform property should be valid
		ResourceLoaderSchedulerParameter schedulerParam;
		{
			schedulerParam.resourceLoader = this;
			schedulerParam.resourceBank = m_resourceBank.Get();
			schedulerParam.depot = depot;
			schedulerParam.isCooked = isCooked;
		}

		m_scheduler = CreateResourceLoaderScheduler( schedulerParam );

#ifdef USE_RESOURCE_METRICS_BANK
		m_resourceMetricsBank = red::CreateUniquePtr< ResourceMetricsBank >();
#endif
	}

	void ResourceLoader::ApplyCensorship( red::UniquePtr< ResourceSwapList > censorSwapList )
	{
		m_resourceBank->ApplyCensorship( std::move( censorSwapList ) );
	}

	ResourceTokenHandle ResourceLoader::IssueLoadingRequest( const ResourcePath& path )
	{
		return IssueLoadingRequest( IssueLoadingRequestParameter( path ) );
	}

	namespace helper
	{
        static ResourceTokenErrorType CheckResolvedPath( const res::ResourcePath& path, const res::ResourcePath& resolvedPath )
        {
            if( !resolvedPath.IsValid() )
            {
                return ResourceTokenErrorType::InvalidPath;
            }

#if !defined( RED_CONFIGURATION_FINAL ) && defined( RED_USE_RESOURCEPATH_STRINGS )
            if( path != resolvedPath )
            {
                const red::StringView originalExt = red::paths::GetExtension( path.ToStringView() );
                const red::StringView resolvedExt = red::paths::GetExtension( resolvedPath.ToStringView() );
                if( originalExt != resolvedExt )
                {
                    const rtti::ClassType* originalClass = res::prv::ResourceClassLookup::GetInstance().GetResourceClassForFile( path );
                    const rtti::ClassType* resolvedClass = res::prv::ResourceClassLookup::GetInstance().GetResourceClassForFile( resolvedPath );
                    if( originalClass != resolvedClass )
                    {
                        RED_DATA_ERROR_BEGIN( red::LoggerCategory_Core, originalClass != resolvedClass, "Link files do not point to the same resource type. %hs != %hs", originalClass->GetName().AsChar(), resolvedClass->GetName().AsChar() );
                        RED_DATA_ERROR_ACTION_SHOW_CUSTOM( "Original Resource", path.ToDebugString() );
                        RED_DATA_ERROR_ACTION_SHOW_CUSTOM( "Resolved Resource", resolvedPath.ToDebugString() );
                        RED_DATA_ERROR_END();
                        return ResourceTokenErrorType::InvalidExtension;
                    }
                }
            }
#endif

            return ResourceTokenErrorType::None;
        }

		static ResourceTokenErrorType ResolveResourcePath( ResourceBank& resourceBank, const res::ResourcePath& path, res::ResourcePath& outResolvedPath )
		{
			// #tbd: otherwise for ResourceDepotMock::ResolveResourcePath: "The mock function has no default action set, and its return type has no default value set."
			outResolvedPath = !red::UnitTestMode() ? resourceBank.ResolvePath( path ) : path;
            return CheckResolvedPath( path, outResolvedPath );
		}

	}

#ifdef RED_ENABLE_RENDER_DEBUG
	thread_local Bool g_tls_debugIsColdLoaded = false;
	Bool g_debugIsLoadingMode = false;

	Bool GetWasColdLoadedThreadLocal_ForRenderDebug()
	{
		return g_tls_debugIsColdLoaded;
	}
#endif

	ResourceTokenHandle ResourceLoader::IssueLoadingRequest( const IssueLoadingRequestParameter & param )
	{
#ifdef RED_ENABLE_RENDER_DEBUG
		g_tls_debugIsColdLoaded = false;
#endif

		const ResourcePath & path = param.path;

		ResourcePath resolvedPath;
		{
			const auto err = helper::ResolveResourcePath( *m_resourceBank, param.path, resolvedPath );
			if ( err != ResourceTokenErrorType::None )
			{
				return CreateFailedResourceToken( resolvedPath, err );
			}
		}

        const ResourceTokenErrorType tokenError = helper::CheckResolvedPath( path, resolvedPath );
        if( tokenError == ResourceTokenErrorType::None )
        {
            return TrySchedulingLoadingJobs( resolvedPath, param );
        }
        else
        {
            auto failedToken = CreateFailedResourceToken( resolvedPath, tokenError );
			UpdateLoadingStats( failedToken, false );
            return failedToken;
        }
	}

	void ResourceLoader::IssueLoadingBulkRequest(const IssueLoadingRequestParameter& param, const red::ArraySpan<const res::ResourcePath>& paths, red::DynArray<res::ResourceTokenHandle>& outResults)
	{
		RED_FATAL_ASSERT(param.parentPath.IsEmpty());
		RED_FATAL_ASSERT(param.path.IsEmpty());

		red::DynArray<ResourcePath> resolvedPaths{ job::PoolJobScope() };
		resolvedPaths = paths;

		m_resourceBank->ResolvePathBulk(resolvedPaths);
		TrySchedulingLoadingJobsBulk(resolvedPaths, outResults, param);
	}

	ResourceTokenHandle ResourceLoader::CantTouchThis_IssueUnregisteredLoading(const IssueLoadingRequestParameter& param)
    {
        const ResourcePath & path = param.path;
        ResourcePath resolvedPath;
        {
            const auto err = helper::ResolveResourcePath( *m_resourceBank, param.path, resolvedPath );
            if( err != ResourceTokenErrorType::None )
            {
                return CreateFailedResourceToken( resolvedPath, err );
            }
        }

        const ResourceTokenErrorType tokenError = helper::CheckResolvedPath( path, resolvedPath );
        if( tokenError == ResourceTokenErrorType::None )
        {
            ResourceTokenHandle token;
            job::CompletionDeferral deferral;

            auto tokenDeferralPair = CreateResourceToken( resolvedPath );
            token = std::move( tokenDeferralPair.first );
            deferral = std::move( tokenDeferralPair.second );

            DeferralRvalArgWorkaroundForMocks arg;
            arg.movableDeferral = &deferral;
            m_scheduler->CantTouchThis_ScheduleUnregisteredResourceLoadingJobs( token, arg, resolvedPath, param );

            return token;
        }
        else
        {
            return CreateFailedResourceToken( resolvedPath, tokenError );
        }
    }

	ResourceTokenHandle ResourceLoader::TryAcquireResourceToken_NoLock(const res::ResourcePath& resolvedPath)
	{
		ResourceTokenWeakHandle weakToken;
		m_resourceTokenDictionary.Find(resolvedPath, weakToken);
		auto token = weakToken.Lock();
		return token;
	}

	void ResourceLoader::UpdateLoadingStats(const res::ResourceTokenHandle& token, Bool wasLoadedCold)
	{
		auto type = LoadingRequestEventType::Failed;
		if ( !token->HasFailed() )
		{
			// The token could be asynchronously updated.
			// So to know for sure whether it was loaded "cold", we ask isLoadedCold vs checking the current IsLoaded() state.
			// However, we'll otherwise check IsLoaded() to see if Hot or still Pending.
			//
			// It's possible for the token to now fail but considered "Pending", but that's an inherent race condition anyway, since at this stage it would also be Pending before Failed.
			// Possibly better to update the stats in the callback once loading completes, but then need to propagate and update the current number of loading requests and their timestamps.
			// Or lazily update the entry in the resourceMetricsBank. This slight inaccuracy makes things much simpler, and failed loading hides the final true metrics once corrected.
			if (wasLoadedCold)
			{
				type = LoadingRequestEventType::Cold;
			}
			else if (token->IsLoaded())
			{
				type = LoadingRequestEventType::Hot;
			}
			else
			{
				type = LoadingRequestEventType::Pending;
			}
		}

		if (m_resourceMetricsBank)
		{
			m_resourceMetricsBank->OnLoadingRequest(token->GetPath(), type );
		}
	}

	void ResourceLoader::TrySchedulingLoadingJobsBulk(const red::ArraySpan< const res::ResourcePath >& resolvedPaths, red::DynArray<res::ResourceTokenHandle>& outResults, const IssueLoadingRequestParameter& param)
	{
		// SHARED LOCK: Try to acquire already created token

		Bool allAcquired = true;
		outResults.Clear();
		{
			RED_SCOPE_SHARED_LOCK(m_resourceTokenLock);
			for (Uint32 i = 0, len = resolvedPaths.Size(); i < len; ++i)
			{
				auto token = TryAcquireResourceToken_NoLock(resolvedPaths[i]);
				allAcquired &= (token != nullptr);
				outResults.PushBack(std::move(token));
			}
		}

		red::BitSet64Dynamic isNewToken{ job::PoolJobScope() };
		isNewToken.Resize(resolvedPaths.Size());

		red::DynArray<job::CompletionDeferral> deferrals{ job::PoolJobScope() };
		deferrals.Reserve(resolvedPaths.Size());

		// EXCLUSIVE LOCK: Try to create new token
		if (!allAcquired)
		{
			RED_SCOPE_LOCK(m_resourceTokenLock);

			for (Uint32 i = 0, len = resolvedPaths.Size(); i < len; ++i)
			{
				// Got lock. Did someone beat us to it ?
				if (!outResults[i])
				{
					outResults[i] = TryAcquireResourceToken_NoLock(resolvedPaths[i]);
				}

				if (!outResults[i])
				{
					isNewToken.Set(i);

					// If we got here, we have the lock, no one managed to beat us to it also. Insert safely					
					auto tokenDeferralPair = CreateResourceToken(resolvedPaths[i]);
					auto token = std::move(tokenDeferralPair.first);
					token->Internal_SetPriority(param.priority);
					outResults[i] = token;
					deferrals.PushBack(std::move(tokenDeferralPair.second));

					m_resourceTokenDictionary[resolvedPaths[i]] = token;
				}
			}
		}

		IssueLoadingRequestParameter paramCopy = param;
		Uint32 deferralIndex = 0;
		for (auto it = red::BitSetIterator<decltype(isNewToken)>(isNewToken); it.IsValid(); it.FindNext())
		{
			const res::ResourcePath& resolvedPath = resolvedPaths[it.GetIndex()];
			// If we got here, we have token and we created it.
			// First step, Is the Resource already loaded?
			const THandle< CResource > resource = m_resourceBank->FindResource(resolvedPath);
			if (!resource)
			{
				// No? Kickstart loading job!
				paramCopy.path = resolvedPath;
				ScheduleLoadingJob(outResults[it.GetIndex()], std::move(deferrals[deferralIndex]), resolvedPath, param);
			}
			else
			{
				// Yes ? Assign to token.
				outResults[it.GetIndex()]->Internal_AssignLoadedResource(resource, std::move(deferrals[deferralIndex]));
			}

			deferralIndex += 1;
		}
	}

	ResourceTokenHandle ResourceLoader::TrySchedulingLoadingJobs(const res::ResourcePath& resolvedPath, const IssueLoadingRequestParameter& param)
	{
		ResourceTokenHandle token;
		job::CompletionDeferral deferral;
		Bool isNewToken = false;

#ifndef RED_CONFIGURATION_FINAL
		Bool callDebugBreakForResource = false;
		{
			RED_SCOPE_SHARED_LOCK(m_debugBreakOnLoadingRequestLock);
			callDebugBreakForResource = m_debugBreakOnLoadingRequest.Exist(resolvedPath);
		}

		if (callDebugBreakForResource)
		{
			RED_SCOPE_LOCK(m_debugBreakOnLoadingRequestLock);

			RED_DEBUG_BREAK();

			// Don't be annoying and constantly debugbreak, can skip over this line in the debugger
			m_debugBreakOnLoadingRequest.Remove(resolvedPath);
		}
#endif

		// SHARED LOCK: Try to acquire already created token
		{
			RED_SCOPE_SHARED_LOCK(m_resourceTokenLock);
			token = TryAcquireResourceToken_NoLock( resolvedPath );
		}

		// EXCLUSIVE LOCK: Try to create new token
		if ( !token )
		{
			// Create new resource token outside the lock.
			auto tokenDeferralPair = CreateResourceToken( resolvedPath );

			RED_SCOPE_LOCK( m_resourceTokenLock );

			// Got lock. Did someone beat us to it ?
			token = TryAcquireResourceToken_NoLock( resolvedPath );
			if (!token)
			{
				// If we got here, we have the lock, no one managed to beat us to it also. Insert safely
				token = std::move(tokenDeferralPair.first);
				token->Internal_SetPriority(param.priority);
				deferral = std::move(tokenDeferralPair.second);
				isNewToken = true;
				m_resourceTokenDictionary[ resolvedPath ] = token;
			}
		}

		Bool wasLoadedCold = false;
		if (isNewToken)
		{
			// If we got here, we have token and we created it.
			// First step, Is the Resource already loaded?
			const THandle< CResource > resource = m_resourceBank->FindResource( resolvedPath );
			if( !resource )
			{
				// No? Kickstart loading job!
				ScheduleLoadingJob( token, std::move(deferral), resolvedPath, param );
				wasLoadedCold = true;

#ifdef RED_ENABLE_RENDER_DEBUG
				g_tls_debugIsColdLoaded = true;
#endif
			}
			else
			{
				// Yes ? Assign to token.
				token->Internal_AssignLoadedResource( resource, std::move( deferral ) );
			}
		}

		UpdateLoadingStats(token, wasLoadedCold);
		return token;
	}

	void ResourceLoader::Internal_TrySchedulingLoadingJobs( const ResourceTokenHandle& token, job::CompletionDeferral&& deferral )
	{
		const res::ResourcePath path = token->GetPath();
		const THandle< CResource > resource = m_resourceBank->FindResource( path );
		if( !resource )
		{
			IssueLoadingRequestParameter param;
			// No? Kickstart loading job!
			ScheduleLoadingJob( token, std::move( deferral ), path, param );
		}
		else
		{
			// Yes ? Assign to token.
			token->Internal_AssignLoadedResource( resource, std::move( deferral ) );
		}
	}

	void ResourceLoader::ScheduleLoadingJob(const ResourceTokenHandle & token, job::CompletionDeferral && deferral, const res::ResourcePath & resolvedPath, const IssueLoadingRequestParameter & param)
	{
		DeferralRvalArgWorkaroundForMocks arg;
		arg.movableDeferral = &deferral;
		m_scheduler->ScheduleResourceLoadingJobs( token, arg, resolvedPath, param );
	}

	ResourceTokenHandle ResourceLoader::IssueReloadingRequest(const IssueLoadingRequestParameter & param)
	{
		// ctremblay: Reloading force disconnect current resource and follow with re-schedule loading.
		// If Resource is still referenced anywhere, it will still be valid. However this meansmore than one version of resource could be in memory at any point...

		ResourcePath resolvedPath;
		{
			const auto err = helper::ResolveResourcePath( *m_resourceBank, param.path, resolvedPath );
			if ( err != ResourceTokenErrorType::None )
			{
				return CreateFailedResourceToken( resolvedPath, err );
			}
		}

		auto tokenDeferralPair = CreateResourceToken( resolvedPath );
		const auto& token = tokenDeferralPair.first;
		auto deferral = std::move( tokenDeferralPair.second );

		ResourceTokenHandle ongoingToken;
		// First step, make sure we got ownership of token. No other thread can beat us.
		{
			RED_SCOPE_LOCK( m_resourceTokenLock );
			auto iter = m_resourceTokenDictionary.Find( resolvedPath );
			if ( iter != m_resourceTokenDictionary.End() )
			{
				ongoingToken = iter.Value().Lock();

				// Locks the resource loading, so any new  requests will use this newly reloaded token
				iter.Value() = token;
			}
			else
			{
				m_resourceTokenDictionary[ resolvedPath ] = token;
			}
		}

		// Second Step. If there was an ongoing token, gotta wait for it to complete unfortunately. Else we can't fully reload.
		if( ongoingToken )
		{
			ongoingToken->WaitUntilLoaded();
		}

		// Third Step, make sure no knowledge exist of previous resource.
		THandle< CResource > oldResource = m_resourceBank->UnregisterResource( resolvedPath );
		ongoingToken.Reset();
		oldResource.Reset();

		// Finally, we can schedule loading job!
		ScheduleLoadingJob( token, std::move( deferral ), resolvedPath, param );

		return token;
	}

	bool ResourceLoader::IsResourceLoading(const ResourcePath & path) const
	{
		ResourceTokenWeakHandle weakToken;
		{
			RED_SCOPE_SHARED_LOCK( m_resourceTokenLock );
			m_resourceTokenDictionary.Find( path, weakToken );
		}

		ResourceTokenHandle token = weakToken.Lock();
		if( token )
		{
			return !token->HasFinished();
		}

		return false;
	}

	THandle< CResource > ResourceLoader::TryAcquiringLoadedResource( const ResourcePath & path )
	{
		ResourcePath resolvedPath;
		if ( helper::ResolveResourcePath( *m_resourceBank, path, resolvedPath ) != ResourceTokenErrorType::None )
			return nullptr;

		return m_resourceBank->FindResource( resolvedPath );
	}

	bool ResourceLoader::IsResourceLoaded( const ResourcePath & path )const
	{
		ResourcePath resolvedPath;
		if ( helper::ResolveResourcePath( *m_resourceBank, path, resolvedPath ) != ResourceTokenErrorType::None )
			return false;

		return static_cast< Bool >( m_resourceBank->FindResource( resolvedPath ) );
	}

	void ResourceLoader::EnumLoadedResources( red::DynArray< ResourcePath >& outPaths ) const
	{
		m_resourceBank->EnumLoadedResources( outPaths );
	}

	void ResourceLoader::IsResourceLoadedBulkQuery( const red::ArraySpan< const ResourcePath >& paths, red::BitSet64Dynamic& outResults ) const
	{
		// Query directly
		// m_depot->ResolveResourcePath() is a no-op in the game
		// and censorship check will be done deeper
		// This also avoids resolving the path twice like the normal IsResourceLoaded() does in ResourceLoader and then again in ResourceBank.
		m_resourceBank->IsResourceLoadedBulkQuery( paths, outResults );
	}

	ResourceMetricsBank* ResourceLoader::GetResourceMetricsBank() const
	{
		return m_resourceMetricsBank.Get();
	}

	void ResourceLoader::Shutdown()
	{
		ResourceTokenDictionary tokenToRemove{ red::PoolEngine() };
		FailedResourceTokenDictionary failedTokenToRemove{ red::PoolEngine() };
		{
			RED_SCOPE_LOCK( m_resourceTokenLock );
			tokenToRemove = std::move( m_resourceTokenDictionary );
			failedTokenToRemove = std::move( m_failedResourceTokenContainer );
		}

		tokenToRemove.Clear();
		failedTokenToRemove.Clear();

		m_resourceBank->Shutdown();

		m_resourceBank.Reset();
		m_scheduler.Reset();
	}

	void ResourceLoader::SyncLoadingFence( job::Counter& counter )
	{
		RED_SCOPE_SHARED_LOCK( m_resourceTokenLock );
		for( auto iter = m_resourceTokenDictionary.Begin(), end = m_resourceTokenDictionary.End(); iter != end; ++iter )
		{
			const ResourceTokenHandle token = iter.Value().Lock();
			if( token )
			{
				counter += token->GetWaitCounter();
			}
		}
	}

	ResourceTokenHandle ResourceLoader::RegisterExistingResource( const ResourcePath & path, const THandle< CResource > & resource )
	{
		resource->Internal_SetPath( path );

		m_resourceBank->UnregisterResource( path );
		m_resourceBank->RegisterResource( path, resource );

		{
			RED_SCOPE_LOCK( m_resourceTokenLock );
			m_resourceTokenDictionary.Remove( path );
		}

		return IssueLoadingRequest( path );
	}

	ResourceTokenHandle ResourceLoader::Internal_TryRegisterResourceToken( const ResourceTokenHandle& token )
	{
		ResourceTokenHandle resultToken;
		const res::ResourcePath& path = token->GetPath();

		{
			RED_SCOPE_SHARED_LOCK( m_resourceTokenLock );
			resultToken = TryAcquireResourceToken_NoLock( path );
		}

		if( !resultToken )
		{
			RED_SCOPE_LOCK( m_resourceTokenLock );

			// Got lock. Did someone beat us to it ?
			resultToken = TryAcquireResourceToken_NoLock( path );
			if( !resultToken )
			{
				m_resourceTokenDictionary[ path ] = token;
				return token;
			}
		}

		return resultToken;
	}

	ResourceTokenHandle ResourceLoader::Internal_GetResourceToken( const res::ResourcePath& path )
	{
		RED_SCOPE_SHARED_LOCK( m_resourceTokenLock );
		return TryAcquireResourceToken_NoLock( path );	
	}

	void ResourceLoader::Internal_RegisterResource( const res::ResourcePath& path, const THandle< CResource >& resource )
	{
		m_resourceBank->RegisterResource( path, resource );
	}

	THandle< CResource > ResourceLoader::Internal_TryRegisterResource( const res::ResourcePath& path, const THandle< CResource >& resource )
	{
		//RED_LOG_CATEGORY( red::LoggerCategory::LoggerCategory_Resources, "++ %hs", path.ToDebugString() );
		return m_resourceBank->TryRegisterResource( path, resource );
	}

	void ResourceLoader::Internal_RegisterFailedToken( const ResourceTokenHandle & token, ResourceTokenErrorType errorType, job::CompletionDeferral&& deferral )
	{
		token->Internal_MarkAsFailed( errorType, std::move( deferral ) );
		if ( IsGameMode() && errorType != ResourceTokenErrorType::Cancelled )
		{
			RED_SCOPE_LOCK( m_resourceTokenLock );
			m_failedResourceTokenContainer.PushBack( token );
		}
	}

	void ResourceLoader::Internal_RegisterResourceToken( const ResourceTokenHandle & token, job::CompletionDeferral&& deferral )
	{
		{
			RED_SCOPE_LOCK( m_resourceTokenLock );
			m_resourceTokenDictionary.Insert( token->GetPath(), token );
		}

		deferral.FinishDeferral();
	}

	void ResourceLoader::Internal_SetResourceBank( red::UniquePtr< ResourceBank > bank )
	{
		m_resourceBank = std::move( bank );
	}

	void ResourceLoader::Internal_SetResourceLoaderScheduler( red::UniquePtr< ResourceLoaderScheduler > scheduler )
	{
		m_scheduler = std::move( scheduler );
	}

	void ResourceLoader::Internal_UnregisterFailedToken( const ResourceTokenHandle& token )
	{
		RED_SCOPE_LOCK( m_resourceTokenLock );
		m_failedResourceTokenContainer.Remove( token );
	}

	void ResourceLoader::GetLoadingResourcesForDebug( red::DynArray< res::ResourcePath >& outResources ) const
	{
		RED_SCOPE_SHARED_LOCK( m_resourceTokenLock );
		outResources.Reserve( m_resourceTokenDictionary.Size() );
		for ( auto iter = m_resourceTokenDictionary.Begin(), end = m_resourceTokenDictionary.End(); iter != end; ++iter )
		{
			const ResourceTokenHandle token = iter.Value().Lock();
			if ( token && !token->HasFinished() )
			{
				outResources.PushBack( token->GetPath() );
			}
		}
	}

	void ResourceLoader::GetLockedResourceSnapshotForDebug(red::DynArray< res::ResourcePath >& outResources) const
	{
		RED_SCOPE_SHARED_LOCK(m_resourceSnapshotLock);
		outResources.Reserve(m_lockedResourceSnapshot.Size());
		for (const auto& it : m_lockedResourceSnapshot)
		{
			outResources.PushBack(it->GetPath());
		}
	}

	Bool ResourceLoader::IsSingleResourceSnapshotLocked() const
	{
		RED_SCOPE_SHARED_LOCK(m_resourceSnapshotLock);

		return m_isResourceSnapshotLocked;
	}


	void ResourceLoader::Update()
	{
		if (m_resourceBank)
		{
			m_resourceBank->UpdateGameCache();
		}
	}

	void ResourceLoader::EnableGameCacheLoadingMode(Bool value)
	{
		if (m_resourceBank)
		{
			m_resourceBank->EnableGameCacheLoadingMode(value);
		}
	}

	void ResourceLoader::ShutdownGameCacheManuallyBecauesRenderingCrashesOtherwise()
	{
		if (m_resourceBank)
		{
			m_resourceBank->ShutdownGameCacheManuallyBecauesRenderingCrashesOtherwise();
		}
	}

	Uint32 ResourceLoader::GetNumGameCacheEntries() const
	{
		return m_resourceBank ? m_resourceBank->GetNumGameCacheEntries() : 0;
	}

#ifndef RED_CONFIGURATION_FINAL
	void ResourceLoader::SetDebugBreakOnLoadResource(const res::ResourcePath& resourcePath)
	{
		if (!resourcePath.IsValid())
		{
			return;
		}

		RED_SCOPE_LOCK(m_debugBreakOnLoadingRequestLock);
		m_debugBreakOnLoadingRequest.Insert(resourcePath);
	}
#endif

	THandle< ResourceSnapshot > ResourceLoader::CreateResourceBankSnapshot(Bool includeExpired) const
	{
		return m_resourceBank ? m_resourceBank->CreateSnapshot(includeExpired) : CreateHandle<ResourceSnapshot>();
	}

	void ResourceLoader::LockSingleResourceSnapshot(const THandle< ResourceSnapshot > & snapshot)
	{
		RED_SCOPE_LOCK(m_resourceSnapshotLock);

		RED_FATAL_ASSERT(!m_isResourceSnapshotLocked, "Snapshot already locked");
		m_isResourceSnapshotLocked = true;
		if (m_resourceBank)
		{
			m_resourceBank->TryLockResourcesInSnapshot(snapshot, m_lockedResourceSnapshot);
		}
	}

	void ResourceLoader::UnlockSingleResourceSnapshot()
	{
		RED_SCOPE_LOCK(m_resourceSnapshotLock);

		RED_FATAL_ASSERT(m_isResourceSnapshotLocked, "Snapshot not locked");
		m_isResourceSnapshotLocked = false;
		m_lockedResourceSnapshot.Clear();
	}

	ResourceLoaderThrottler* ResourceLoader::GetThrottler() const
	{
		return m_scheduler ? m_scheduler->GetThrottler() : nullptr;
	}

	void ResourceLoader::FlushResourceCache()
	{
		m_resourceBank->FlushGameCache();
	}

	red::UniquePtr< ResourceLoader > CreateResourceLoader(IResourceDepot* depot, bool isCooked)
	{
		red::UniquePtr< ResourceLoader > loader = red::CreateUniquePtr< ResourceLoader >();
		loader->Initialize( depot, isCooked );
		return loader;
	}

	bool operator==( const IssueLoadingRequestParameter & left, const IssueLoadingRequestParameter & right )
	{
		return left.path == right.path
			&& left.parentPath == right.parentPath
			&& left.priority == right.priority
			&& left.skipPostLoad == right.skipPostLoad
			&& left.skipBuffers == right.skipBuffers
			&& ( memcmp( &left.postLoadFlags, &right.postLoadFlags, sizeof( PostLoadFlags ) ) == 0 )
			&& left.verifyExportTypes == right.verifyExportTypes
			&& left.useGameCache == right.useGameCache
			&& left.ioHint == right.ioHint;
	}

	IssueLoadingRequestParameter Convert( const ResourcePath& path, const LoadingOptions & options )
	{
		auto param = IssueLoadingRequestParameter( path );
		{
			param.parentPath = options.initialResourcePath;
			param.skipPostLoad = options.skipPostLoad;
			param.skipBuffers = options.skipBuffers;
			param.postLoadFlags = options.postLoadFlags;
			param.verifyExportTypes = options.verifyExportTypes;
			param.useGameCache = options.useGameCache;
		};
		return param;
	}

}
