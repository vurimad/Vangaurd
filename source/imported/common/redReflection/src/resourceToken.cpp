/**
* Copyright (c) 2018 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"
#include "resourceToken.h"
#include "resourceClassLookup.h"
#include "gatheredResource.h"
#include "../../redJobs2/include/jobRunner.h"
#include "../../redSystem/include/unitTestMode.h"
#include "../../redJobs2/include/jobDeferral.h"
#include "../../redJobs2/include/jobBuilder.h"
#include "serializationLoader.h"

namespace res
{

	static WaitUntilLoadedRenderCallback s_debugWaitUntilLoadedRenderCallback;

	void SetDebugWaitUntilLoadedRenderCallback(const WaitUntilLoadedRenderCallback& renderDebugCallback)
	{
		s_debugWaitUntilLoadedRenderCallback = renderDebugCallback;
	}

	const char* GetResourceTokenErrorTypeText(ResourceTokenErrorType error)
	{
		switch ( error )
		{
		case ResourceTokenErrorType::None:
			return "No error";
		case ResourceTokenErrorType::InvalidPath:
			return "Invalid path";
		case ResourceTokenErrorType::InvalidExtension:
			return "Invalid extension";
		case ResourceTokenErrorType::ResourceNotFound:
			return "Resource not found";
		case ResourceTokenErrorType::UnknownError:
			return "Unknown error";
		case ResourceTokenErrorType::InternalError:
			return "Internal error";
		default:
			RED_FATAL( "Unknown Error Type" );
			return "";
		}
	}

	THandle< CResource > GetFallbackResource( const ResourcePath & path )
	{
		const rtti::ClassType* resourceClass = GetResourceClassForFile( path ); 
		if ( resourceClass )
		{
			const void * defaultResource = resourceClass->GetDefaultObject();
			if ( defaultResource )
			{
				const CResource * resource = static_cast< const CResource* >( defaultResource );
				const res::GatheredResource* gatheredResource = resource->GetDefaultResource();
				if ( gatheredResource )
				{
					return gatheredResource->Get();
				}
			}
		}

		return THandle< CResource >();	
	}

	ResourceToken::ResourceToken( const ResourcePath& path )
		: m_path( path )
		, m_waitableCounter( job::Priority::Latent, path.ToDebugString() )
		, m_error( ResourceTokenErrorType::None )
		, m_ioContext(red::CreateSharedPtr<io::IOContext>())
	{
	}

	ResourceToken::~ResourceToken()
	{
		if (!HasFinished())
		{
			m_ioContext->RequestCancel();
		}
	}

	bool ResourceToken::IsLoaded() const
	{
		return m_loadingCompleted.GetValue() && !HasFailed();
	}

	const ResourcePath & ResourceToken::GetPath() const
	{
		return m_path;
	}

	ResourceTokenErrorType ResourceToken::GetError() const
	{
		return m_error;
	}

	bool ResourceToken::HasFailed() const
	{
		return m_error != ResourceTokenErrorType::None;
	}

	bool ResourceToken::HasFinished() const
	{
		return IsLoaded() || HasFailed();
	}

	void ResourceToken::Internal_MarkAsFailed( ResourceTokenErrorType errorType, job::CompletionDeferral&& deferral )
	{
		m_resource = GetFallbackResource( m_path );
		m_error = errorType;
		deferral.FinishDeferral();
	}

	const THandle< CResource > & ResourceToken::GetResource() const
	{
		RED_FATAL_ASSERT( HasFinished(), "GetResource cannot be called until resource has finished loading." );
		return m_resource;
	}

	const THandle< CResource > & ResourceToken::WaitUntilLoaded() const
	{
		if( HasFinished() )
			return m_resource;

        ALWAYSENABLED_RED_FATAL_ASSERT( ::SIsMainThread(), "WaitUntilLoaded can only be called on main thread." );

		if (s_debugWaitUntilLoadedRenderCallback)
		{
			const Int32 timeoutMillseconds = 100;
			red::Timer timer;
			while (!job::FlushCounter(m_waitableCounter, true, timeoutMillseconds))
			{
				s_debugWaitUntilLoadedRenderCallback(m_path, (Float)timer.GetSeconds());
			}
		}
		else
		{
			job::FlushCounter(m_waitableCounter, true);
		}

		return m_resource;
	}

	job::Counter ResourceToken::OnLoaded(LoadedCallback&& callback)
	{
		ResourceTokenHandle token = SharedFromThis();

		job::Builder builder{ job::Priority::Latent };
		builder.DispatchWait(GetWaitCounter());
		builder.DispatchJob( "ResourceToken/OnLoaded", [callback=std::move( callback ), token]( const job::RunContext& )
		{
			RED_FATAL_ASSERT( token->HasFinished() );
			// kept a strong reference to the handle
			callback( token->GetResource() );
		} );

		return builder.ExtractWaitCounter();
	}

	void ResourceToken::Internal_AssignLoadedResource( const THandle< CResource > & resource, job::CompletionDeferral&& deferral )
	{
		{
			RED_SCOPE_LOCK( m_lock );
			if( !m_resource )
			{
				m_resource = resource;
				m_loadingCompleted.SetValue( true );
			}
		}

		deferral.FinishDeferral();
	}

	void ResourceToken::Internal_SetPriority(io::EAsyncPriority priority)
	{
		m_priority = priority;
	}

	const job::Counter& ResourceToken::GetWaitCounter() const
	{
		return m_waitableCounter;
	}

	void ResourceToken::Temp_SetLoadingCompletedForUnitTestOnly()
	{
		RED_FATAL_ASSERT( red::UnitTestMode() );
		m_loadingCompleted.SetValue( true );
	}

	red::SharedPtr< io::IOContext > ResourceToken::ResourceLoaderInternal_GetIOContext() const
	{
		return m_ioContext;
	}

	void ResourceToken::Internal_CollectAsyncOpIds(red::HashSet<Uint64>& collector) const
	{
		const Uint64 id = m_ioContext->GetAsyncOpId();
		if (!collector.Insert(id).IsSuccessful())
		{
			// avoid a dependency loop!
			return;
		}

		for (const auto& depToken : m_dependencyTokens)
		{
			depToken->Internal_CollectAsyncOpIds(collector);
		}
	}

	void ResourceToken::Internal_KickOffImportsFromDependencyCache_NotThreadSafe(const serialization::LoadingContext& context, const serialization::RawDiskPosition& ioHint, const red::DynArray<res::ResourcePath>& deps)
	{
		red::DynArray<res::ResourceTokenHandle> dependencyTokens{ red::PoolResource() };
		dependencyTokens.Reserve(deps.Size());

		for (const auto& it : deps)
		{
			RED_FATAL_ASSERT(it.IsValid());
			auto param = res::ResourceLoader::GetImportLoadingRequestParameter(it, context, ioHint);
			dependencyTokens.PushBack(context.m_resourceLoader->IssueLoadingRequest(param));
		}
		
		RED_SCOPE_LOCK(m_lock);
		RED_FATAL_ASSERT(m_dependencyTokens.Empty(), "Dependencies already kicked off!");
		m_dependencyTokens = std::move(dependencyTokens);
	}

	void ResourceToken::UpdateDistanceToObserverSquared_NotThreadSafe(Int32 quantizedDistanceSquared, Uint8 generation, Uint8 recursionLevel)
	{
		RED_FATAL_ASSERT(recursionLevel < 255, "What kind of resource is this? %hs", m_path.ToDebugString());
		const Bool shouldUpdateDependencies = m_ioContext->TryUpdateDistanceToObserverSquared(quantizedDistanceSquared, generation, recursionLevel);

		// Should never fail to lock 99.9% of the time, unless either some tokens are slow to kick off deps or something external to the streaming system created the token before us.
		// In any case, can try again next frame.
		Bool hasDependencyTokensAvailable = false;
		{
			if( m_lock.TryAcquireShared() )
			{
				hasDependencyTokensAvailable = m_dependencyTokens.Size() > 0;
				m_lock.ReleaseShared();
			}
		}

		// Either there are no dependencies, or dependencies haven't been kicked off yet.
		// The dependencies are updated atomically, so using this check we avoid someting silly like holding the lock and then calling a recursive function
		if (!hasDependencyTokensAvailable)
		{
			return;
		}

		if (shouldUpdateDependencies)
		{
			for (auto& token : m_dependencyTokens)
			{
				token->UpdateDistanceToObserverSquared_NotThreadSafe(quantizedDistanceSquared, generation, recursionLevel + 1);
			}
		}
	}

	ResourceTokenHandle CreateFailedResourceToken(const ResourcePath& path, ResourceTokenErrorType errorType)
	{
		ResourceTokenHandle token = red::CreateSharedPtr< ResourceToken >( path );	
		token->Internal_MarkAsFailed( errorType, token->m_waitableCounter.CreateDeferral( token.Get(), "ResourceToken" ) );
		return token;
	}

	std::pair< ResourceTokenHandle, job::CompletionDeferral > CreateResourceToken( const ResourcePath & path )
	{
		ResourceTokenHandle token = red::CreateSharedPtr< ResourceToken >( path );
		return std::make_pair( token, token->m_waitableCounter.CreateDeferral( token.Get(), "ResourceToken" ) );
	}
}
