/**
* Copyright (c) 2014 CD Projekt Red. All Rights Reserved.
*/

#pragma once

#include "../../redJobs/include/jobValue.h"
#include "../../redMemory/include/sharedPtr.h"
#include "../../redMemory/include/uniqueBuffer.h"

#include "deferredDataBuffer.h"
#include "serializationAsyncSource.h"
#include "reflectionPool.h"

namespace job
{
	class Builder;
}

namespace res
{
	class ResourceLoaderThrottler;
}

namespace serialization
{
	struct AsyncSourceBufferAsyncProxySetup
	{
		AsyncSourcePtr source;
		Uint32 fileOffset = 0;
		Uint32 sizeInMemory = 0;
		Uint32 sizeOnDisk = 0;
	};

	struct MemoryFileBufferAsyncProxySetup
	{
		IFile* file = nullptr;
		Uint32 fileOffset = 0;
		Uint32 sizeInMemory = 0;
		Uint32 sizeOnDisk = 0;
	};

	// A simple proxy for a buffer that is getting loaded
	// NOTE: may be promoted to a general buffer 
	// This is a replacement for "LatentLoadingToken"
	class RED_REFLECTION_API IBufferAsyncProxy : public red::NonCopyable
	{
		RED_USE_MEMORY_POOL( red::PoolResource );

	public:
		virtual ~IBufferAsyncProxy();

		struct BufferLoadingContext
		{
			typedef void (*SetLoadedDataCallback)( void* target );
			SetLoadedDataCallback callback{ nullptr };
			SetLoadedDataCallback reentrantCallback{ nullptr };
			void* target{ nullptr };
			red::UniqueBuffer* destinationBuffer;
			Uint32 alignment{ 0 };
			io::EAsyncPriority priority{ io::eAsyncPriority_Normal };
			res::ResourceLoaderThrottler* throttler{ nullptr };
			const AsyncSourceReadBuffer* inlineData{ nullptr };
			red::SharedPtr< io::IOContext > ioContext{ nullptr };
		};

		// get size of the data that will be loaded (in case we want to allocate memory)
		virtual Uint32 GetSize() const = 0;

		// dispatch a job chain that will load (and decompress) the data
		virtual void DispatchLoadingJobs( const BufferLoadingContext& context ) const = 0;

#if 1//ndef NO_EDITOR
		virtual BufferHandleRO Editor_LoadContentDirect() const = 0;
#endif

#ifndef NO_EDITOR
		virtual BufferHandleRO Editor_LoadCompressedDataForCookingDirect( Uint32& outSizeInMemory ) const = 0;
#endif

		virtual Bool IsMemoryFileBuffer() const = 0;

		// create an access proxy from a async source and specific buffer information
		// this assumes the buffer placement will never change (used by game only),
		// in editor the shit is way more complicated since the data may move
		// the data will be fetched from given location in file, optional decompression is possible if (sizeInMemory != sizeOnDisk)
		// THIS METHOD ASSUMES YOU HAVE THE EXACT KNOWLEDGE OF THE BUFFER WHEREABOUTS
		static BufferAsyncPtr CreateFromAsyncSource( const AsyncSourceBufferAsyncProxySetup& setup );
		
		static BufferAsyncPtr CreateFromMemoryFile( const MemoryFileBufferAsyncProxySetup& setup );
		
	protected:
		IBufferAsyncProxy();
	};

} // red