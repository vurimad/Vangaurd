/**
* Copyright (c) 2026 RED Vanguard, All Rights Reserved.
*/
#pragma once

#include "resourcePath.h"
#include "handle.h"
#include "postLoadContext.h"
#include "../../redJobs/include/jobValue.h"
#include "../../redJobs2/include/jobStackTrace.h"
#include "../../redJobs2/include/jobDeferral.h"
#include "../../redMemory/include/sharedPtr.h"
#include "../../redIO/include/redIOCommon.h"
#include "resourceLoaderTypes.h"

namespace res
{
	class ResourceLoader;
	class ResourceLoaderThrottler;
}

namespace job
{
	class Builder;
}

namespace red
{
	class UniqueBuffer;
}

namespace serialization
{
	class IBufferAsyncProxy;
	typedef red::UniquePtr< IBufferAsyncProxy > BufferAsyncPtr;

	class IAsyncSource;
	typedef red::SharedPtr<IAsyncSource> AsyncSourcePtr;

	// Dependency loading context
	class RED_REFLECTION_API LoadingContext
	{
		RED_USE_MEMORY_POOL( red::PoolEngine );
	public:
		// Parent root to attach all loaded objects to
		THandle< ISerializable > m_parent;

		// Resource loader to load the imports from
		res::ResourceLoader* m_resourceLoader;

		// Auto incremented creation Id
		Uint32 m_creationId;

		// Records the job callstack at the point of initial resource loading request.
		// Otherwise the trail is lost after async IO and other processing jobs
		// Requires "-jobDebugger" on the command line
		job::StackTraceHandle m_debugStackTraceHandle;

		// Resource path of the loaded resource (for debugging)
		res::ResourcePath m_resourcePath;

		// Resource path of the initial resource that triggered loading of this resource (for debugging)
		res::ResourcePath m_initialResourcePath;

		io::EAsyncPriority m_priority;

		PostLoadFlags m_postLoadFlags;

		const rtti::ClassType * m_filterClassType; // ctremblay: In backend or cmdlet, we might need to find a certain type of object only. Skip everything that do not match.

		RawDiskPosition ioHint;

		Bool m_getAllLoadedObjects:1;	// Get all loaded objects regardless of everything (good for copy&paste and editor stuff)
		Bool m_skipPostLoad:1;			// Skip performing the post load actions (used in commandlets like resave)
		Bool m_skipImports:1;			// Skip resolving imports
		Bool m_skipBuffers:1;			// Skip loading buffers
		Bool m_skipBackendData:1;
		Bool m_verifyExportTypes:1;		// Verify that the resource contains only known types for export; only useful in cooked data
		Bool m_replication:1;			// This loading replicates objects
		Bool m_useGameCache:1;
		
		LoadingContext();
	};


	// Result of loading operation
	class RED_REFLECTION_API LoadingResult
	{
	public:
		typedef red::DynArray< THandle< ISerializable > > TLoadedRoots;
		typedef red::DynArray< THandle< ISerializable > > TLoadedObjects;

		// Root objects loaded from file (not a ISerializable ones)
		TLoadedRoots m_loadedRootObjects;

		// List of all loaded objects (only if m_getAllLoadedObjects was specified in the loading context)
		TLoadedObjects m_loadedObjects;

		LoadingResult();
		~LoadingResult();

		void SetFailed();
		void SetCancelled();
		bool IsFailed() const;
		bool IsCancelled() const;

	private:
		red::EAsyncResult m_result;
	};

	RED_INLINE LoadingResult::LoadingResult()
		: m_result( red::eAsyncResult_Pending )
		, m_loadedRootObjects( red::PoolEngine() )
		, m_loadedObjects( red::PoolEngine() )  
	{}

	RED_INLINE void LoadingResult::SetFailed()
	{
		RED_FATAL_ASSERT(m_result == red::eAsyncResult_Pending);
		m_result = red::eAsyncResult_Error;
	}

	RED_INLINE bool LoadingResult::IsFailed() const
	{
		return m_result == red::eAsyncResult_Error || m_result == red::eAsyncResult_Canceled;
	}

	RED_INLINE void LoadingResult::SetCancelled()
	{
		RED_FATAL_ASSERT(m_result == red::eAsyncResult_Pending);
		m_result = red::eAsyncResult_Canceled;
	}

	RED_INLINE bool LoadingResult::IsCancelled() const
	{
		return m_result == red::eAsyncResult_Canceled;
	}

	class ILoader;
	typedef red::SharedPtr<ILoader> LoaderPtr;

	// Abstract loader for serialized data
	class RED_REFLECTION_API ILoader
	{
		RED_USE_MEMORY_POOL( red::PoolEngine );

	public:
		ILoader();
		virtual ~ILoader() = 0;

		struct CallbackContext
		{
			using CallbackFunc = void( const LoadingResult& result, const res::ResourceTokenWeakHandle& tokenUserData, const serialization::LoadingContext& loadingContext, job::CompletionDeferral&& deferral, void* userData );
			CallbackFunc* callback{ nullptr };
			void* userData{ nullptr }; // additional user data for whatever purpose during the callback
			res::ResourceTokenWeakHandle tokenUserData; // not touched during loading
		};

		// Create a job chain loading the data from the file
		// NOTE: from now the only interface to load data is asynchronous, synchronous (IFile&) based interface is not coming back
		// The context data is copied so the context lifetime does not matter
		// The result is pushed into the context after the loading completes
		virtual void LoadAsyncWithCallback( const AsyncSourcePtr& data, const LoadingContext& context, const CallbackContext& callbackContext, job::CompletionDeferral&& deferral ) const = 0;

		virtual Bool UseInCookedGame() const = 0;

		// Register factory
		typedef std::function< ILoader*() > TLoaderFactory;

		template< typename TLoader >
		static void RegisterCustomFactory( const AnsiChar* constImmutableExt );

		// Create best serialization loader for given file extension
		// If no extension is passed then a default binary loader is created
		static LoaderPtr CreateLoader( const AnsiChar* formatExtension = nullptr );

	private:
		static void RegisterCustomFactory( const AnsiChar* constImmutableExt, const TLoaderFactory& factoryFunc );

		static const Uint32 MAX_FORMATS = 20;

		struct FormatPair
		{
			const AnsiChar* m_constImmutableExt;
			TLoaderFactory m_loaderFactory;
		};

		static red::StaticArray< FormatPair, MAX_FORMATS > st_formats;
	};

	template< typename TLoader >
	void ILoader::RegisterCustomFactory( const AnsiChar* constImmutableExt )
	{
		static_assert( std::is_base_of< ILoader, TLoader >::value, "Must be an ILoader" );
		RegisterCustomFactory( constImmutableExt, []() { return RED_NEW( TLoader ); } );
	}

	// Helper method to load from memory (binary serialization only)
	// Entire file must be loaded into memory, including all buffers if accessed
	extern RED_REFLECTION_API const Bool LoadFromMemory( const void* memoryData, const Uint32 memorySize, const LoadingContext& context, LoadingResult& outResult, Bool doActiveWaiting = false );

} // serialization