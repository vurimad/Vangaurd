/**
* Copyright (c) 2018 CD Projekt Red. All Rights Reserved.
*/

#pragma once

#include "handle.h"
#include "resourceLoaderTypes.h"
#include "../../redMemory/include/sharedFromThis.h"
#include "../../redJobs2/include/jobCounterOwner.h"
#include "serializationBinaryRuntimeTables.h"

class CResource;

namespace io
{
	class IOContext;
}

namespace serialization
{
	class LoadingContext;
	struct RawDiskPosition;
}

namespace res
{
	class ResourcePath;

	typedef red::Function< void( THandle< CResource > res ) > LoadedCallback; // Should be deprecated

	enum class ResourceTokenErrorType : Int8
	{
		None,
		InvalidPath,
		InvalidExtension,
		ResourceNotFound,
		UnknownError,
		InternalError,
		Cancelled,
	};

	using WaitUntilLoadedRenderCallback = red::Function<void(const res::ResourcePath& waitingForResource, Float waitingSeconds)>;
	RED_REFLECTION_API void SetDebugWaitUntilLoadedRenderCallback(const WaitUntilLoadedRenderCallback& renderDebugCallback);

	RED_REFLECTION_API const char* GetResourceTokenErrorTypeText( ResourceTokenErrorType error );

	RED_REFLECTION_API ResourceTokenHandle CreateFailedResourceToken( const ResourcePath & path, ResourceTokenErrorType errorType );
	RED_REFLECTION_API std::pair< ResourceTokenHandle, job::CompletionDeferral > CreateResourceToken( const ResourcePath & path );

	class RED_REFLECTION_API ResourceToken : public red::EnableSharedFromThis< ResourceToken >
	{
		RED_USE_MEMORY_POOL( red::PoolEngine );

		friend RED_REFLECTION_API ResourceTokenHandle CreateFailedResourceToken( const ResourcePath & path, ResourceTokenErrorType errorType );
		friend RED_REFLECTION_API std::pair< ResourceTokenHandle, job::CompletionDeferral > CreateResourceToken( const ResourcePath & path );

	public:
		explicit ResourceToken( const ResourcePath& path );
		RED_MOCKABLE ~ResourceToken();

		bool IsLoaded() const;
		bool HasFailed() const;
		bool HasFinished() const;

		ResourceTokenErrorType GetError() const;
		const ResourcePath & GetPath() const;
		
		const THandle< CResource > & GetResource() const;

		const THandle< CResource > & WaitUntilLoaded() const;

		template< typename T >
		THandle< T > WaitUntilLoaded() const
		{
			static_assert(std::is_base_of< CResource, T >::value, "T should derive from CResource");
			return Cast< T >(WaitUntilLoaded());
		}
		
		job::Counter OnLoaded( LoadedCallback&& callback );
		
		const job::Counter& GetWaitCounter() const;

		void Internal_MarkAsFailed( ResourceTokenErrorType errorType, job::CompletionDeferral&& deferral );
		void Internal_AssignLoadedResource( const THandle< CResource > & resource, job::CompletionDeferral&& deferral );
		void Internal_SetPriority(io::EAsyncPriority priority);

		io::EAsyncPriority Internal_GetPriority() const { return m_priority; }

		void Temp_SetLoadingCompletedForUnitTestOnly();

		red::SharedPtr< io::IOContext > ResourceLoaderInternal_GetIOContext() const;
		void Internal_CollectAsyncOpIds(red::HashSet<Uint64>& collector) const;

		void Internal_KickOffImportsFromDependencyCache_NotThreadSafe(const serialization::LoadingContext& context, const serialization::RawDiskPosition& ioHint, const red::DynArray<res::ResourcePath>& deps);

		void UpdateDistanceToObserverSquared_NotThreadSafe(Int32 quantizedDistanceSquared, Uint8 generation, Uint8 recursionLevel);


	private:
		// NOTE: Once created, do not clear (until the ResourceToken destructor naturally clears them). Because we now rely on them in UpdateDistanceToObserverSquared_NotThreadSafe().
		// Note also that UpdateDistanceToObserverSquared_NotThreadSafe() isn't 
		red::DynArray<res::ResourceTokenHandle> m_dependencyTokens{ red::PoolResource() };
		red::RWSpinLock m_lock;
		
		THandle< CResource > m_resource;
		red::SharedPtr< io::IOContext > m_ioContext;
		ResourcePath m_path;
		job::Counter m_waitableCounter;
		red::Atomic< Bool > m_loadingCompleted;
		ResourceTokenErrorType m_error;
		io::EAsyncPriority m_priority{ io::eAsyncPriority_Normal }; // Currently just for debug
	};
}



