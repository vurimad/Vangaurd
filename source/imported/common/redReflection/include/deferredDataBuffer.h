/**
* Copyright (c) 2014 CD Projekt Red. All Rights Reserved.
*/

#pragma once

#include "../../redSystem/include/readWriteSpinLock.h"
#include "../../redMemory/include/uniqueBuffer.h"
#include "../../redCompression/include/compression.h"
#include "../../redSystem/include/utility.h"
#include "../../redJobs2/include/jobCounterOwner.h"

#include "rttiCommon.h"
#include "rttiType.h"
#include "rttiMacrosUtils.h"
#include "rttiSimpleType.h"
#include "rttiClassBuilder.h"
#include "rttiClassDeclarationMacros.h"
#include "rttiTypeName.h"
#include "bufferHandle.h"
#include "serializationMapping.h"
#include "serializationLoadingToken.h"
#include "../../redMemory/include/sharedFromThis.h"

class DataBuffer;

namespace job
{
	class Builder;
	class Counter;
	class CompletionDeferral;
}

namespace io
{
	enum EAsyncPriority : Uint8;
}

namespace res { class ResourceLoaderThrottler; }

namespace serialization
{
	class IBufferAsyncProxy;
	typedef red::UniquePtr< IBufferAsyncProxy > BufferAsyncPtr;

	struct MapperLoadBufferParam;

	enum DeferredDataBufferFlags : Uint8
	{
		// The deferred data buffer will load as if it were actually not deferred at all - so CreateLoadingJobs() is a no-op unless you call Clear().
		// Useful in cases where you have deferred data buffers and you know at constructor time if you conditionally 
		// want it loaded or unloaded, which otherwise could have been a regular (non-deferred) data buffer.
		//
		// So currently more useful for the editor/backend; but could have gameplay/engine system applications as well.
		//
		// E.g., you have PC, Xbox One and PS4 texture blobs in the non-cooked game and only want to load the one for the matching platform.
		// While there's no direct concept of "platform" in the deferred data buffer, you can conditionally set this flag in the constructor using platform #define's.
		DDBF_None = 0,

		DDBF_AutoLoadPC = RED_FLAG(0),

		DDBF_AutoLoadXboxOne = RED_FLAG(1),

		DDBF_AutoLoadPS4 = RED_FLAG(2),

		DDBF_AutoLoadAll = DDBF_AutoLoadPC | DDBF_AutoLoadXboxOne | DDBF_AutoLoadPS4,

		// The buffer contents will be NOT be automatically compressed when saving. Useful if you know the data compresses so poorly as to not be worth it.
		// NOTE: data that compresses the same or worse won't be saved in compressed form even without this flag.
		// NOTE: if you retro-actively change the flag, then any data will still load and save OK (just that the data on disk won't change without a resave)
		DDBF_SaveNoCompression = RED_FLAG( 3 ),

		// Kraken compression is very slow, sometimes we want to use just LZ4 compression, the faster one.
		// NOTE: this is intended to be used only by the editor, not for data actually used in final game.
		// NOTE: if you retro-actively change the flag, then any data will still load and save OK (just that the data on disk won't change without a resave)
		DDBF_Editor_SaveFastCompression = RED_FLAG( 4 ),
	};

	class RED_REFLECTION_API LoadingTokenImmutable : public red::EnableSharedFromThis<LoadingTokenImmutable>
	{
		RED_USE_MEMORY_POOL(red::PoolResource);

	public:
		LoadingTokenImmutable()
			: m_ioContext(red::CreateSharedPtr<io::IOContext>())
		{}

		~LoadingTokenImmutable()
		{
			m_ioContext->RequestCancel();
		}

		const job::Counter& GetWaitCounter() const { return m_loadingCounter; }

		const BufferHandleRO& GetBuffer() const
		{
			RED_FATAL_ASSERT(m_loadingCounter.Internal_IsZeroSnapshot());
			return m_buffer;
		}

		void Internal_OnDataLoaded(red::UniqueBuffer&& loadedBuffer)
		{
			m_buffer = BufferHandleRO(std::move(loadedBuffer));
		}

		void Internal_OnDataLoaded(const BufferHandleRO& buffer)
		{
			m_buffer = buffer;
		}

		const red::SharedPtr<io::IOContext>& Internal_GetIOContext() const
		{
			return m_ioContext;
		}

		job::CompletionDeferral Internal_CreateDeferral()
		{
			return m_loadingCounter.CreateDeferral();
		}

	private:
		job::Counter m_loadingCounter{ job::Priority::Latent };
		BufferHandleRO m_buffer;
		red::SharedPtr< io::IOContext > m_ioContext;
	};

	/// data buffer that can be later loaded asynchronously
	class RED_REFLECTION_API DeferredDataBuffer : red::NonCopyable
	{
	public:
		static const Uint32 c_defaultAlignment = 16;
		static const compression::ECompressionType c_defaultCompression;

		explicit DeferredDataBuffer( const red::memory::Pool& memoryPool = red::PoolEngine(), Uint32 alignment = c_defaultAlignment, Uint8 flags = 0 );
		explicit DeferredDataBuffer( Uint8 flags );
		~DeferredDataBuffer();

		// Mainly so DynArray< DeferredDataBuffer >MoveAfterReallocation() works.
 		DeferredDataBuffer( DeferredDataBuffer&& other );
 		
		DeferredDataBuffer& operator=( DeferredDataBuffer&& rhs ) = delete;
		 	
		// Clear buffer content; can be reloaded with CreateLoadingJobs()
		void Clear();

		// Mark that this buffer is in use and we shouldn't clear it directly using Clear.
		/* Should be called before IssueLoadingJobs.
		   After you stop using the buffer call UnlockAndClear() */
		void LockClearing();
		// Called after we stop using loaded data.
		/* If we are the last user of the buffer also clears it. */
		void UnlockAndClear();
		
		Uint16 GetNumClearingLocks() const;

		// Creates jobs necessary to load the buffer. No-op if already loaded, but should synchronize
		// with a fence for correctness in the cases it isn't.
		LoadingToken IssueLoadingJobs(io::EAsyncPriority priority = io::eAsyncPriority_Normal);

		red::SharedPtr< LoadingTokenImmutable > IssueLoadingJobs_Immutable(io::EAsyncPriority priority = io::eAsyncPriority_Normal);

		// Loading/Saving
		void Serialize( IFile& f );

		// Access buffer data; fatal assert if not loaded.
		const BufferHandleRO& Data() const;

		Uint32 GetSize() const;

		const red::memory::Pool& GetPool() const;

		Uint32 GetAlignment() const;

		RED_FORCE_INLINE Bool IsLoaded() const { return m_loadingState == Loaded; }
		RED_FORCE_INLINE Bool IsCleared() const { return m_loadingState == Cleared; }

		const BufferHandleRO& Convert_SetContentDirect( const DataBuffer& dataBuffer );
#ifndef NO_EDITOR
		const BufferHandleRO& Editor_SetContentDirect( const void* data, Uint32 size, Uint32 alignment = c_defaultAlignment );
		const BufferHandleRO& Editor_SetContentDirect( Uint32 size );
		const BufferHandleRO& Editor_SetContentDirect( const BufferHandleRO& data );
		const BufferHandleRO& Editor_SetContentDirect( red::UniqueBuffer&& data );
		void Editor_Abandon();
#endif

		// Physics needs to cleanup using mesh backend data!!!
#if 1//ndef NO_EDITOR
		// Load the content synchronously.
		BufferHandleRO Editor_LoadContentDirect() const;
#endif

	private:
		enum LoadingState : Uint8
		{
			Cleared = 0,
			Pending = 1,
			Loaded = 2,
			Moved = 3, // for debuggability
			Abandoned = 4,
		};

		// Sharedptr to make it easiser for the consumer to swap for another buffer asynchronously and without copying everywhere
		// E.g., cleared in this DDB, start loading another DDB, then set the new data through some message queue
		struct MovableData
		{
			// Used as destination to read data async; moved into memoryBuffer when finished.
			// Also used to store the memory pool and alignment
			red::UniqueBuffer pendingDataBuffer;

			BufferHandleRO memoryBuffer;
		
			// Set during serialization; remains null if buffer was empty
			BufferAsyncPtr bufferAccess;

			// #tbd: could go from 64bit ref to 32bit hash if can map from handle back to pool
			Uint8 flags;

	#ifndef NO_EDITOR
			Bool editorIsCookerWriting; // once set, never cleared; resource should be reloaded
	#endif

			MovableData();
		};

		static LoadingToken StaticDispatchLoadingJobs( const MapperLoadBufferParam& param, IBufferAsyncProxy& proxy );
		LoadingToken DispatchLoadingJobsInternal_NoLock( res::ResourceLoaderThrottler* throttler, io::EAsyncPriority priority, IBufferAsyncProxy& proxy, const AsyncSourceReadBuffer* inlineData );

		// Internal callback for when the buffer has been loaded.
		static void OnLoadedDataCallback( void* target );
		static void OnLoadedDataCallback_NoLock( void* target );

		MovableData m_data;
		red::UniquePtr< job::Counter > m_loadingCounter;
		red::UniquePtr< job::CompletionDeferral > m_waitCallbackComplete;
		Uint32 m_numCreateLoadingJobsInFlight;
		LoadingState m_loadingState;
		mutable red::SpinLock m_loadingLock;
		Uint16 m_numClearingLocks;
	};

} // serialization

// Manual serialization
RED_FORCE_INLINE void operator<<( IFile& file, serialization::DeferredDataBuffer& val )
{
	val.Serialize( file );
}

RTTI_DECLARE_TYPE_NAME_IN_NAMESPACE( DeferredDataBuffer, serialization );

