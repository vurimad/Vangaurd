/**
* Copyright (c) 2014 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"
#include "bufferAsyncProxy.h"

#include "../../redFileSystem/include/file.h"
#include "../../redJobs2/include/jobRunner.h"
#include "../../redJobs2/include/jobBuilder.h"
#include "../../redCore/include/globalModeInfo.h"

#include "resourceDepot.h"
#include "serializationAsyncSource.h"
#include "serializationLoader.h"
#include "serializationDecompressor.h"
#include "resourceLoaderThrottler.h"
#include "resourceMetricsBank.h"
#include "resourceLoader.h"

namespace serialization
{

IBufferAsyncProxy::IBufferAsyncProxy()
{
}

IBufferAsyncProxy::~IBufferAsyncProxy()
{
}

namespace helper
{
//------------------------------------------------------------------------------

// buffer read from memory
class MemoryFileBufferAsyncProxy final : public IBufferAsyncProxy
{
public:
	MemoryFileBufferAsyncProxy( const MemoryFileBufferAsyncProxySetup& setup );
	virtual ~MemoryFileBufferAsyncProxy() = default;

	virtual Uint32 GetSize() const override;
	virtual Bool IsMemoryFileBuffer() const override;

	virtual void DispatchLoadingJobs( const BufferLoadingContext& context ) const override;

#if 1//ndef NO_EDITOR
	virtual BufferHandleRO Editor_LoadContentDirect() const override;
#endif

#ifndef NO_EDITOR
	virtual BufferHandleRO Editor_LoadCompressedDataForCookingDirect( Uint32& outSizeInMemory ) const override;
#endif

private:
	Bool LoadData( const MemoryFileBufferAsyncProxySetup& setup );

	MemoryFileBufferAsyncProxySetup m_setup; // save mainly for debugability if loading fails; see initial setup params
	BufferHandleRO m_inMemoryData;
	BufferHandleRO m_onDiskData;
};

MemoryFileBufferAsyncProxy::MemoryFileBufferAsyncProxy( const MemoryFileBufferAsyncProxySetup& setup )
	: m_setup( setup )
{
	// Save current offset before reading buffers, will have to restore
	const Uint64 prevOffset = m_setup.file->GetOffset();

	// Get the data
	if ( !LoadData(setup) )
	{
		RED_LOG_ERROR("Failed to load buffer");
	}

	// Restore offset after loading buffer from file
	m_setup.file->Seek( prevOffset );
}

Uint32 MemoryFileBufferAsyncProxy::GetSize() const
{
	return m_inMemoryData.GetSize();
}

bool MemoryFileBufferAsyncProxy::IsMemoryFileBuffer() const
{
	return true;
}

void MemoryFileBufferAsyncProxy::DispatchLoadingJobs( const BufferLoadingContext& context ) const
{
	RED_FATAL("Should not call directly. Need to immediately invoke context callback to update load state, but will deadlock currently");
}

#if 1//ndef NO_EDITOR
BufferHandleRO MemoryFileBufferAsyncProxy::Editor_LoadContentDirect() const
{
	return m_inMemoryData;
}
#endif

#ifndef NO_EDITOR
BufferHandleRO MemoryFileBufferAsyncProxy::Editor_LoadCompressedDataForCookingDirect( Uint32& outSizeInMemory ) const
{
	BufferHandleRO ret;
	outSizeInMemory = 0;

	const bool isCompressed = m_setup.sizeOnDisk < m_setup.sizeInMemory;
	if ( isCompressed )
	{
		outSizeInMemory = m_inMemoryData.GetSize();
		ret = m_onDiskData;
	}
	return ret;
}
#endif

Bool MemoryFileBufferAsyncProxy::LoadData( const MemoryFileBufferAsyncProxySetup& setup )
{
	setup.file->Seek( setup.fileOffset );
	if ( setup.file->HasErrors() )
	{
		return false;
	}

	const auto& memoryPool = setup.file->GetInternalMemoryPool();

	BufferHandleRO inMemoryData;
	BufferHandleRO onDiskData;

	const Bool isCompressed = setup.sizeOnDisk < setup.sizeInMemory;
	if ( isCompressed )
	{
		auto compressedBuffer = red::CreateUniqueBuffer( memoryPool, setup.sizeOnDisk, DeferredDataBuffer::c_defaultAlignment );
		setup.file->Serialize( compressedBuffer.Get(), setup.sizeOnDisk );
		if ( setup.file->HasErrors() )
		{
			return false;
		}

		auto decompressedBuffer = red::CreateUniqueBuffer( memoryPool, setup.sizeInMemory, DeferredDataBuffer::c_defaultAlignment );
		RED_FATAL_ASSERT(compressedBuffer.Get());

		const char* debugLogicalFileName = setup.file->GetFileNameForDebug();
		if ( !Decompressor::DecompressData( debugLogicalFileName, red::MakeBlobView( compressedBuffer ), red::MakeBlobSpan( decompressedBuffer ) ) )
		{
			return false;
		}

		inMemoryData = BufferHandleRO( std::move( decompressedBuffer ) );
		onDiskData = BufferHandleRO( std::move( compressedBuffer ) );
	}
	else
	{
		auto readBuffer = red::CreateUniqueBuffer( memoryPool, setup.sizeInMemory, DeferredDataBuffer::c_defaultAlignment );
		setup.file->Serialize( readBuffer.Get(), setup.sizeInMemory );
		if ( setup.file->HasErrors() )
		{
			return false;
		}

		inMemoryData = BufferHandleRO( std::move( readBuffer ) );
		onDiskData = inMemoryData;
	}

	m_inMemoryData = inMemoryData;
	m_onDiskData = onDiskData;

	return true;
}

//------------------------------------------------------------------------------

struct DeferredBufferAsyncContext
{
	RED_USE_MEMORY_POOL( red::PoolResource );

	AsyncSourcePtr source;
	IBufferAsyncProxy::BufferLoadingContext loadingContext;
	AsyncSourceReadBuffer readFromDiskResult;
	Uint32 fileOffset{ 0 };
	AsyncSourceReadSizes readSizes;
};

static DeferredBufferAsyncContext* CreateDeferredBufferAsyncContext()
{
	return RED_NEW( DeferredBufferAsyncContext );
}

static void DestroyDeferredBufferAsyncContext( DeferredBufferAsyncContext* context )
{
	RED_DELETE( context );
}

static void NotifyFinished(DeferredBufferAsyncContext* asyncContext)
{
	auto callback = asyncContext->loadingContext.callback;
	auto* const target = asyncContext->loadingContext.target;
	callback(target);
	DestroyDeferredBufferAsyncContext(asyncContext);
}

static void CopyBuffer( void* userData, const red::BlobSpan& )
{
	auto* asyncContext = static_cast<DeferredBufferAsyncContext*>(userData);

	ScopedProfilerChannel scopedProfiler{ PBC_IO };

	const auto& readSizes = asyncContext->readSizes;

	// Run jobs now, begin not sync'd externally
	// Callback notifies DDB, which handles synchronization finishing

	// NOTE: allocate the decompression buffer right before using it. This way if the buffer has a low memory budget and is only used temporarily
	// then we lessen the chance of OOM by first allocating it, and then getting throttled.
	RED_FATAL_ASSERT(asyncContext->loadingContext.destinationBuffer && asyncContext->loadingContext.destinationBuffer->Size() == readSizes.memorySize);
	auto& inMemoryBuffer = *asyncContext->loadingContext.destinationBuffer;

	const auto bufferForIOView = asyncContext->readFromDiskResult.ValidRegion();
	RED_FATAL_ASSERT( bufferForIOView.Data(), "Memory for DDB was freed accidentally" );

	if ( readSizes.IsCompressed() )
	{
		const char* debugLogicalFileName = asyncContext->source->Debug_GetResourcePath().ToDebugString();
		if ( !Decompressor::DecompressData( debugLogicalFileName, bufferForIOView, red::MakeBlobSpan( inMemoryBuffer ) ) )
		{
			RED_LOG_ERROR( "Failed  to decompress buffer!" );
			inMemoryBuffer.Reset();
		}
	}
	else
	{
		// Was padded on disk but not compressed; copy out of padded onDiskBuffer to unpadded inMemoryBuffer
		RED_FATAL_ASSERT( bufferForIOView.Size() == inMemoryBuffer.Size() );
		red::Memcpy( inMemoryBuffer.Get(), bufferForIOView.Data(), inMemoryBuffer.Size() );
	}

	NotifyFinished( asyncContext );
}

static void OnBufferDataLoaded(AsyncSourceReadBuffer buffer, red::UniqueBuffer memoryForDecompressor, void* userData, red::EAsyncResult result)
{
	auto* asyncContext = static_cast<DeferredBufferAsyncContext*>( userData );
	const auto& readSizes = asyncContext->readSizes;
	RED_FATAL_ASSERT( asyncContext );

	if (result == red::eAsyncResult_Canceled)
	{
		asyncContext->loadingContext.destinationBuffer->Reset();

		// #tbd: doesn't propogate the fact that cancelled, but right now nothing would be able to access the cancelled result
		NotifyFinished( asyncContext );
		return;
	}

	RED_FATAL_ASSERT(memoryForDecompressor.Size() == 0, "Should not have allocated IO memory for final buffer memory");
	RED_FATAL_ASSERT(!buffer.ValidRegion().Empty(), "Buffer empty. EAsyncResult: %d, memorySize=%u, diskSize=%u, bufferOffset=%u, bufferSize=%u",
		(Int32)result,
		readSizes.memorySize,
		readSizes.diskSize,
		readSizes.bufferOffset,
		readSizes.bufferSize);

	asyncContext->readFromDiskResult = std::move(buffer);

	RED_FATAL_ASSERT(asyncContext->loadingContext.destinationBuffer && asyncContext->loadingContext.destinationBuffer->Size() == readSizes.memorySize);
	asyncContext->loadingContext.throttler->Run(&CopyBuffer, asyncContext);
}

//------------------------------------------------------------------------------

// buffer read from a file
class AsyncSourceBufferAsyncProxy final : public IBufferAsyncProxy
{
public:
	AsyncSourceBufferAsyncProxy( const AsyncSourceBufferAsyncProxySetup& setup );
	virtual ~AsyncSourceBufferAsyncProxy() = default;

	virtual Uint32 GetSize() const override;
	virtual bool IsMemoryFileBuffer() const override;

	virtual void DispatchLoadingJobs( const BufferLoadingContext& context ) const override;

#if 1//ndef NO_EDITOR
	virtual BufferHandleRO Editor_LoadContentDirect() const override;
#endif

#ifndef NO_EDITOR
	virtual BufferHandleRO Editor_LoadCompressedDataForCookingDirect( Uint32& outSizeInMemory ) const override;
#endif

private:
	void BeginLoadBufferData( const BufferLoadingContext& context ) const;
	AsyncSourceReadSizes GetReadSizes() const;

	AsyncSourceBufferAsyncProxySetup m_setup;
};


AsyncSourceBufferAsyncProxy::AsyncSourceBufferAsyncProxy( const AsyncSourceBufferAsyncProxySetup& setup )
	: m_setup( setup )
{}

Uint32 AsyncSourceBufferAsyncProxy::GetSize() const
{
	return m_setup.sizeInMemory;
}

bool AsyncSourceBufferAsyncProxy::IsMemoryFileBuffer() const
{
	return false;
}

void AsyncSourceBufferAsyncProxy::DispatchLoadingJobs( const BufferLoadingContext& context ) const
{
	RED_FATAL_ASSERT( context.destinationBuffer );
	RED_FATAL_ASSERT( context.callback );
	RED_FATAL_ASSERT( context.target );

	BeginLoadBufferData( context );
}	

#if 1//ndef NO_EDITOR
BufferHandleRO AsyncSourceBufferAsyncProxy::Editor_LoadContentDirect() const
{
	BufferHandleRO ret;
	const auto& memoryPool = red::PoolEngine();

	const auto readSizes = GetReadSizes();
	RED_FATAL_ASSERT( !readSizes.IsPadded(), "Editor loose files should not have any on disk padding! Source '%hs'", m_setup.source->Debug_GetDescription().AsChar() );
	RED_FATAL_ASSERT( readSizes.bufferSize == readSizes.diskSize, "Editor loose files should not read extra data from disk! Source: '%hs'", m_setup.source->Debug_GetDescription().AsChar() );

	const bool isCompressed = readSizes.IsCompressed();
	if ( isCompressed )
	{
		// #tbd: alignment for compressed data? For now choose IO pool default
		auto compressedBuffer = red::CreateUniqueBuffer( memoryPool, m_setup.sizeOnDisk, 16 );
		RED_FATAL_ASSERT( compressedBuffer.Get() );
		if ( !m_setup.source->ReadInline( m_setup.fileOffset, m_setup.sizeOnDisk, red::MakeBlobSpan( compressedBuffer ), io::eAsyncPriority_High, io::RequestSource::ResourceSystem_BufferAsyncProxy_ReadInline, nullptr ) )
		{
			return ret;
		}

		auto decompressedBuffer = red::CreateUniqueBuffer( memoryPool, m_setup.sizeInMemory, DeferredDataBuffer::c_defaultAlignment );
		RED_FATAL_ASSERT( decompressedBuffer.Get() );

		const char* debugLogicalFileName = m_setup.source->Debug_GetResourcePath().ToDebugString();
		if ( !Decompressor::DecompressData( debugLogicalFileName, red::MakeBlobView( compressedBuffer ), red::MakeBlobSpan( decompressedBuffer ) ) )
		{
			return ret;
		}

		ret = BufferHandleRO( std::move( decompressedBuffer ) );
	}
	else
	{
		RED_FATAL_ASSERT( m_setup.sizeOnDisk == m_setup.sizeInMemory, "Size on disk shouldn't be padded for editor files! Source: '%hs'", m_setup.source->Debug_GetDescription().AsChar() );
		auto readBuffer = red::CreateUniqueBuffer( memoryPool, m_setup.sizeOnDisk, DeferredDataBuffer::c_defaultAlignment );
		if ( !m_setup.source->ReadInline( m_setup.fileOffset, m_setup.sizeOnDisk, red::MakeBlobSpan( readBuffer ), io::eAsyncPriority_High, io::RequestSource::ResourceSystem_BufferAsyncProxy_ReadInline, nullptr ) )
		{
			return ret;
		}

		ret = BufferHandleRO( std::move( readBuffer ) );
	}

	return ret;
}
#endif

#ifndef NO_EDITOR
BufferHandleRO AsyncSourceBufferAsyncProxy::Editor_LoadCompressedDataForCookingDirect( Uint32& outSizeInMemory ) const
{
	BufferHandleRO ret;
	outSizeInMemory = 0;

	const auto readSizes = GetReadSizes();
	RED_FATAL_ASSERT( !readSizes.IsPadded(), "Editor loose files should not have any on disk padding! Source '%hs'", m_setup.source->Debug_GetDescription().AsChar() );
	RED_FATAL_ASSERT( readSizes.bufferSize == readSizes.diskSize, "Editor loose files should not read extra data from disk! Source: '%hs'", m_setup.source->Debug_GetDescription().AsChar() );

	const bool isCompressed = readSizes.IsCompressed();
	if ( !isCompressed )
	{
		return ret;
	}

	const auto& memoryPool = red::PoolEngine();
	auto readBuffer = red::CreateUniqueBuffer(memoryPool, m_setup.sizeOnDisk, 16);
	if ( !m_setup.source->ReadInline( m_setup.fileOffset, m_setup.sizeOnDisk, red::MakeBlobSpan( readBuffer ), io::eAsyncPriority_High, io::RequestSource::ResourceSystem_BufferAsyncProxy_ReadInline, nullptr ) )
	{
		return ret;
	}

	outSizeInMemory = m_setup.sizeInMemory;
	ret = BufferHandleRO( std::move( readBuffer ) );
	return ret;
}
#endif

void AsyncSourceBufferAsyncProxy::BeginLoadBufferData( const BufferLoadingContext& context ) const
{
	RED_FATAL_ASSERT( m_setup.sizeOnDisk > 0, "Shouldn't have mapped an empty buffer!" );
	RED_FATAL_ASSERT( context.destinationBuffer && context.destinationBuffer->Size() == 0 );

	// load the data from async source directly into the final memory buffer for decompressed memory
	// allocate now so size is valid, although the contents aren't initialized yet. This simplifies things for use cases
	// like creating an ArraySpan view over a buffer during OnSerialize(), because the data may not
	// be loaded yet but we need to know how many elements there will be.
	auto& destBuffer = *context.destinationBuffer;
	destBuffer.Reallocate(m_setup.sizeInMemory);
	RED_FATAL_ASSERT(destBuffer.Data(),
		"CreateUniqueBuffer failed: numBytesForIO=%u, alignment=%u, pool=%hs",
		m_setup.sizeInMemory,
		context.alignment,
		red::memory::GetPoolName(destBuffer.GetPool().GetHandle())
	);

	if (context.inlineData)
	{
		const auto& asyncSource = m_setup.source;
		const Uint32 offset = m_setup.fileOffset;
		const Uint32 size = m_setup.sizeOnDisk;

		IAsyncSource::ReadAsyncParams params;
		params.requestSource = io::RequestSource::ResourceSystem_BufferAsyncProxy;
		params.offset = offset;
		params.size = size;

		red::BlobView inlineDataView;
		if (asyncSource->TryGetMemorySourceFromInlineData(params, *context.inlineData, inlineDataView))
		{
			if (GResourceLoader->GetResourceMetricsBank())
			{
				res::ResourceMetricsBank::DeferredDataBufferBeginLoadParams params;
				params.bufferOffset = offset;
				params.bufferSizeOnDisk = size;
				params.isInlineRead = true;
				params.resourcePath = asyncSource->Debug_GetResourcePath();//???? 
				GResourceLoader->GetResourceMetricsBank()->OnDeferredDataBufferBeginLoad(params);
			}

			const Bool isCompressed = m_setup.sizeOnDisk < m_setup.sizeInMemory;
			if (isCompressed)
			{
				const char* debugLogicalFileName = asyncSource->Debug_GetResourcePath().ToDebugString();
				if (!Decompressor::DecompressData(debugLogicalFileName, inlineDataView, red::MakeBlobSpan(destBuffer)))
				{
					RED_FATAL("Failed to decompress buffer!");
					destBuffer.Reset();
				}
			}
			else
			{
				// Was padded on disk but not compressed; copy out of padded onDiskBuffer to unpadded inMemoryBuffer
				RED_FATAL_ASSERT(destBuffer.Size() == inlineDataView.Size());
				red::Memcpy(destBuffer.Get(), inlineDataView.Data(), destBuffer.Size());
			}

			auto* const target = context.target;
			const auto reentrantCallback = context.reentrantCallback;
			RED_FATAL_ASSERT(reentrantCallback);
			reentrantCallback(target);

			return;
		}
	}

	//auto* asyncContext = RED_NEW( DeferredBufferAsyncContext );
	auto* asyncContext = helper::CreateDeferredBufferAsyncContext();
	asyncContext->source = m_setup.source;
	asyncContext->loadingContext = context;
	asyncContext->fileOffset = m_setup.fileOffset;
	asyncContext->readSizes = GetReadSizes();

	auto& readSizes = asyncContext->readSizes;
	RED_FATAL_ASSERT( readSizes.diskSize == m_setup.sizeOnDisk );
	RED_FATAL_ASSERT( readSizes.memorySize == m_setup.sizeInMemory );

	// if failed, then need to know if had I/O pool memory or not... and etc.
	// and if the I/O buffer is empty in the assert, then what was the read status, WTF did it get to it. Did I/O perhaps fail??

	IAsyncSource::CallbackContext callbackContext;
	callbackContext.userData = asyncContext;
	callbackContext.callback = &OnBufferDataLoaded;

	const auto& asyncSource = asyncContext->source;
	const Uint32 offset = asyncContext->fileOffset;
	const Uint32 size = readSizes.diskSize;
	const auto priority = asyncContext->loadingContext.priority;
	
	IAsyncSource::ReadAsyncParams params;
	params.requestSource = io::RequestSource::ResourceSystem_BufferAsyncProxy;
		
	// just decompress into the final buffer; don't need a temp one from the I/O system
	// #tbd: in the case of garlic mapped memory on consoles we may yet require it unless we remap the bus type
	params.allocDeserializationMemoryForDecompressorIfCompressed = false;

	params.offset = offset;
	params.size = size;
	params.priority = priority;	
	params.HACK_mightBeTerrainAndNeedsATonOfMemoryForBuffers = true;
	params.ioContext = context.ioContext;

	if (GResourceLoader->GetResourceMetricsBank())
	{
		res::ResourceMetricsBank::DeferredDataBufferBeginLoadParams params;
		params.bufferOffset = offset;
		params.bufferSizeOnDisk = size;
		params.isInlineRead = false;
		params.resourcePath = asyncSource->Debug_GetResourcePath();//???? 
		GResourceLoader->GetResourceMetricsBank()->OnDeferredDataBufferBeginLoad(params);
	}

	asyncSource->ReadAsync( params, AsyncSourceReadBuffer(readSizes), callbackContext);
}

AsyncSourceReadSizes AsyncSourceBufferAsyncProxy::GetReadSizes() const
{
	auto result = m_setup.source->GetReadSizes( m_setup.fileOffset, m_setup.sizeOnDisk );
	if ( result.memorySize != m_setup.sizeInMemory )
	{
		result.memorySize = m_setup.sizeInMemory;
	}
	return result;
}

} // helper

//------------------------------------------------------------------------------

BufferAsyncPtr IBufferAsyncProxy::CreateFromAsyncSource( const AsyncSourceBufferAsyncProxySetup& setup )
{
	// Should not have been mapped if empty
	RED_FATAL_ASSERT( setup.source, "Unable to create async proxy with no source file." );
	RED_FATAL_ASSERT( setup.sizeOnDisk != 0, "Unable to create async proxy for NO DATA." );
	RED_FATAL_ASSERT( setup.sizeInMemory != 0, "Unable to create async proxy for NO DATA." );
	return red::CreateUniquePtr< helper::AsyncSourceBufferAsyncProxy >( setup );
}

BufferAsyncPtr IBufferAsyncProxy::CreateFromMemoryFile( const MemoryFileBufferAsyncProxySetup& setup )
{
	// Should not have been mapped if empty
	RED_FATAL_ASSERT( setup.file && setup.file->GetSize() > 0 , "Unable to create file proxy for NO DATA" );
	return red::CreateUniquePtr< helper::MemoryFileBufferAsyncProxy >( setup );
}

} // fs
