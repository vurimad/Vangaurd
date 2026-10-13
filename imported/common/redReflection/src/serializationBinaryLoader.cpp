/**
* Copyright (c) 2026 RED Vanguard, All Rights Reserved.
*/

#include "build.h"
#include "serializationMapping.h"
#include "serializationLoader.h"
#include "serializationBinaryLoader.h"
#include "serializationFileTables.h"
#include "serializationAsyncSource.h"
#include "serializationBinaryRuntimeTables.h"
#include "resourceLoader.h"
#include "resourceToken.h"

#include "../../../common/redJobs2/include/jobRunner.h"
#include "../../../common/redFileSystem/include/memoryFileReader.h"
#include "../../../common/redFileSystem/include/fileVersionList.h"

#include "../../../common/redContainers/include/blob.h"

#include "../../redMemory/include/atomicSharedPtr.h"
#include "../../redSystem/include/crc.h"
#include "../../redConfig/include/configVar.h"
#include "../../redJobs2/include/jobDeferral.h"
#include "resourceLoaderThrottler.h"
#include "serializationDecompressor.h"
#include "resource.h"
#include "../../redJobs2/include/jobMemoryPools.h"
#include "../../redIO/include/redIOAsyncIO.h"

namespace Config
{
// validate CRC of all loaded files, this is an easy test to capture corrupted data (within reason)
TConfigVar< Bool > cvValidateCRCOfLoadedFiles( "Serialization", "ValidateCRC", false );
}

namespace serialization
{

BinaryLoader::BinaryLoader()
{
}

BinaryLoader::~BinaryLoader()
{
}

bool BinaryLoader::UseInCookedGame() const
{
	return true;
}

//------------------------------------------------------------------------------

namespace helper
{

struct BinaryLoaderAsyncContext : public red::NonCopyable
{
	RED_USE_MEMORY_POOL( red::PoolResource );

	Bool IsCancelRequested() const
	{
		return ioContext && ioContext->IsCancelRequested();
	}

	ILoader::CallbackContext callbackContext;
	job::CompletionDeferral deferral;

	serialization::Decompressor* decompressor{ nullptr };
	AsyncSourceReadBuffer onDiskBuffer;
	red::UniqueBuffer bufferForDecompressor;

	RuntimeTables runtimeTables{ RuntimeTables::eNoLoading };
	
	// passed into ReadAsync() and rely on red::eAsyncResult_Cancelled.
	// We'll only check it ourselves to cancel deserialization or update debu loading state info.
	red::SharedPtr<io::IOContext> ioContext;

	AsyncSourcePtr source;
	AsyncSourceReadSizes readSizes;
	LoadingContext loadingContext;
	LoadingResult result;
	res::ResourceTokenHandle keepAliveToken;
	Bool hasNonBlockingPostload{ false }; // TBD: something safe that can be run instead of deferring it to a job
};

BinaryLoaderAsyncContext* CreateBinaryLoaderAsyncContext()
{
	return RED_NEW( helper::BinaryLoaderAsyncContext );
}

void DestroyBinaryLoaderAsyncContext( BinaryLoaderAsyncContext* context )
{
	RED_DELETE( context );
}

static void NotifyFinished( BinaryLoaderAsyncContext* asyncContext )
{
	const auto callback = asyncContext->callbackContext.callback;
	const auto& result = asyncContext->result;
	const auto& tokenUserData = asyncContext->callbackContext.tokenUserData;
	auto& deferral = asyncContext->deferral;
	void* const userData = asyncContext->callbackContext.userData;
	const auto& loadingContext = asyncContext->loadingContext;


	if (asyncContext->ioContext)
	{
		asyncContext->ioContext->SetLoadingState(io::IOContext::LoadingState::Finished);
	}

	// If somebody's interested in registering this as an event in some tool
#if 0
	if (asyncContext->keepAliveToken && asyncContext->keepAliveToken.GetRefCount() == 1)
	{
		RED_LOG_SPAM("We did it team, we saved the I/O!");
	}
#endif

	callback( result, tokenUserData, loadingContext, std::move( deferral ), userData );

	DestroyBinaryLoaderAsyncContext( asyncContext );
}

static void BuildImportDependencyChain( job::Counter& loadingCounter, const RuntimeTables& runtimeTables )
{
	for ( const auto& import : runtimeTables.m_mappedImports )
	{
		// skip over invalid files
		// wait for the dependency loading to complete before calling post load
		if ( import.m_loadingToken && import.waitOnImport )
		{
			loadingCounter += import.m_loadingToken->GetWaitCounter();
		}
	}
}

static Bool TryLoadTables( const void* decompressedData, Uint32 decompressedDataSize, const LoadingContext& context, FileTables& fileTables, RuntimeTables& runtimeTables, Uint32& outFileVersion )
{
	CMemoryFileReaderExternalBuffer dataReader( decompressedData, decompressedDataSize, context.m_resourcePath.ToDebugString() );

	Uint32 version = 0;
	if ( fileTables.Load( dataReader, version ) )
	{
		RED_FATAL_ASSERT(version >= VER_MINIMAL && version <= VER_CURRENT, "Invalid file version %d detected in data", version);

		outFileVersion = version;
		runtimeTables.Resolve( context, fileTables );
		return true;
	}
	return false;
}

static void ProcessPostLoad(void* userData, const red::BlobSpan&)
{
	BinaryLoaderAsyncContext* asyncContext = static_cast<BinaryLoaderAsyncContext*>(userData);

	if (asyncContext->ioContext)
	{
		asyncContext->ioContext->SetLoadingState(io::IOContext::LoadingState::PostLoad);
	}

	RuntimeTables& runtimeTables = asyncContext->runtimeTables;

	// before proceeding to post load call make sure that all resource dependencies are loaded

	// final notification on objects
	{
		const auto& context = asyncContext->loadingContext;

		job::Builder builder{ job::Priority::Latent };
		Bool isPostLoadCalled = runtimeTables.PostLoad( asyncContext->loadingContext, builder );

		if( isPostLoadCalled && !asyncContext->result.m_loadedRootObjects.Empty() )
		{
			const auto resource = Cast< CResource >( asyncContext->result.m_loadedRootObjects[0] );
			if( resource && resource->HACK_GetPostLoadWaitCounter() )
			{
				builder.DispatchWait(*resource->HACK_GetPostLoadWaitCounter());
			}
		}

		if( isPostLoadCalled )
		{
			builder.DispatchJob( "Serialization/WaitForEditorPostLoadJobs", [asyncContext]( const job::RunContext& )
			{
				NotifyFinished( asyncContext );
			} );
		}
		else
		{
			NotifyFinished(asyncContext);
		}
	}

	// Our quest is done!
}

static void BuildDeserializationJobChain(BinaryLoaderAsyncContext* asyncContext, red::BlobView& decompressedData)
{
	const AsyncSourcePtr& asyncSourceForBuffers = asyncContext->source;
	auto& context = asyncContext->loadingContext;
	auto& runtimeTables = asyncContext->runtimeTables;

	runtimeTables = RuntimeTables(asyncSourceForBuffers);

	if (asyncContext->ioContext)
	{
		asyncContext->ioContext->SetLoadingState(io::IOContext::LoadingState::Deserialization);
	}

	FileTables fileTables;
	Uint32 fileVersion;

	// load file tables, kicks off import loading in runtimeTables->Resolve()
	{
		const auto& context = asyncContext->loadingContext;
		if (!helper::TryLoadTables(decompressedData.Data(), decompressedData.Size(), context, fileTables, runtimeTables, fileVersion))
		{
			asyncContext->result.SetFailed();
			NotifyFinished(asyncContext);
			return;
		}
	}

	if (asyncContext->IsCancelRequested())
	{
		asyncContext->result.SetCancelled();
		NotifyFinished(asyncContext);
		return;
	}

	// Create and load objects
	{
		// create dependencies out of resolved files
		job::Counter loadingCounter{ job::Priority::Latent };
		BuildImportDependencyChain(loadingCounter, runtimeTables);

		runtimeTables.CreateExports(context, fileTables);
		runtimeTables.RegisterInplaceResources( context, fileTables, loadingCounter );
		
		CMemoryFileReaderExternalBuffer dataReader(decompressedData.Data(), decompressedData.Size(), context.m_resourcePath.ToDebugString());
		dataReader.m_version = fileVersion; // THIS IS UBER IMPORTANT

		runtimeTables.LoadExports(0, dataReader, loadingCounter, context, fileTables, asyncContext->result, std::move(asyncContext->onDiskBuffer));

		// !!!NOTE: too late to process cancellation now unless syncing on loadingCounter first, because DDBs (deferred data buffers) could still be loading.

		// Clear the buffer to release memory back to the serialization::MemoryAllocator
		decompressedData = red::BlobView();
		asyncContext->onDiskBuffer.Reset(); // release again here, if wasn't actually compressed (and no bufferForDecompressor), then decompressedData BlobView actually pointed to this
		asyncContext->bufferForDecompressor.Reset();
	
		// Run on a job so the throttler can get back to processing other items ASAP, so they can release deserialization memory ASAP.
		job::Builder builder{ job::Priority::Latent };
		builder.DispatchWait(loadingCounter); // #tbd: be clever here and check if already zero... last time did that messed sth up

		builder.DispatchJob("Serialization/WaitForDependencies", [asyncContext](const job::RunContext&) {
			auto* throttler = asyncContext->loadingContext.m_resourceLoader->GetThrottler();
			RED_FATAL_ASSERT(throttler);
			throttler->Run(&ProcessPostLoad, asyncContext, true);
		});
	}
}

static void BuildCoreJobChain( void* userData, const red::BlobSpan& dedicatedThrottlerDecompressionMemory )
{
	ScopedProfilerChannel scopedProfilerChannel{ PBC_IO };

	auto* asyncContext = static_cast<BinaryLoaderAsyncContext*>(userData);

	const AsyncSourcePtr& source = asyncContext->source;
	const LoadingContext& context = asyncContext->loadingContext;

	if (asyncContext->IsCancelRequested())
	{
		asyncContext->result.SetCancelled();
		NotifyFinished(asyncContext);
		return;
	}

	red::BlobView decompressedData;

	// Check if has decompression buffer. If editor loose file, then SizeOnDisk() is whole file size so can't just check size on disk vs in memory now
 	if ( !asyncContext->readSizes.IsCompressed() )
 	{
		RED_FATAL_ASSERT(!asyncContext->bufferForDecompressor);
 		decompressedData = asyncContext->onDiskBuffer.ValidRegion();

		// NOTE: don't release onDiskBuffer until after deserialization
 	}
	else
	{
		if (asyncContext->ioContext)
		{
			asyncContext->ioContext->SetLoadingState(io::IOContext::LoadingState::Decompression);
		}

		red::BlobSpan inMemoryBuffer;

		if (!asyncContext->bufferForDecompressor)
		{
			ALWAYSENABLED_RED_MEMORY_FATAL_ASSERT(asyncContext->readSizes.memorySize <= dedicatedThrottlerDecompressionMemory.Size(),
				"If no decompresion memory allocated, "
				"then should have been small enough for dedicated decompression memory: "
				"%u > %u",
				asyncContext->readSizes.memorySize,
				dedicatedThrottlerDecompressionMemory.Size());

			inMemoryBuffer = dedicatedThrottlerDecompressionMemory;
		}
		else
		{
			inMemoryBuffer = red::MakeBlobSpan( asyncContext->bufferForDecompressor );
		}

		const auto& onDiskMemory = asyncContext->onDiskBuffer.ValidRegion();
		RED_FATAL_ASSERT( onDiskMemory.Size() < inMemoryBuffer.Size(), "Bad compression: %u < %u", onDiskMemory.Size(), inMemoryBuffer.Size() );
		const auto compressionType = compression::GetCompressionTypeFromData( onDiskMemory.Data(), onDiskMemory.Size() );
				
		const char* debugLogicalFileName = asyncContext->loadingContext.m_resourcePath.ToDebugString();
		if ( asyncContext->decompressor->DecompressData( debugLogicalFileName, onDiskMemory, inMemoryBuffer ) )
		{
			decompressedData = inMemoryBuffer;

			// can release I/O memory now if don't need it for inlined DDB buffer exports later
			if (!asyncContext->onDiskBuffer.GetReadSizes().isOverReadForInlineBuffers)
			{
				asyncContext->onDiskBuffer.Release();
			}
		}
		else
		{
			RED_LOG_ERROR( "BinaryLoader: Failed to decompress object buffer!" );
			asyncContext->result.SetFailed();
			NotifyFinished( asyncContext );	
			return;
		}


		if (asyncContext->IsCancelRequested())
		{
			asyncContext->result.SetCancelled();
			NotifyFinished(asyncContext);
			return;
		}
	}
	
	BuildDeserializationJobChain( asyncContext, decompressedData );
}

static void OnObjectDataLoaded(AsyncSourceReadBuffer buffer, red::UniqueBuffer memoryForDecompressor, void* userData, red::EAsyncResult result)
{
	auto* asyncContext = static_cast<BinaryLoaderAsyncContext*>( userData );
	RED_FATAL_ASSERT( asyncContext );
	RED_FATAL_ASSERT(asyncContext->bufferForDecompressor.Data() == nullptr, "Shouldn't have decompression memory from another source");

	asyncContext->bufferForDecompressor = std::move(memoryForDecompressor);
	asyncContext->onDiskBuffer = std::move(buffer);

	if (result == red::eAsyncResult_Canceled)
	{
		asyncContext->result.SetCancelled();
		NotifyFinished(asyncContext);
		return;
	}

	if (asyncContext->onDiskBuffer.IsEmpty())
	{
		asyncContext->result.SetFailed();
		NotifyFinished(asyncContext);
		return;
	}

	if (asyncContext->ioContext)
	{
		asyncContext->ioContext->SetLoadingState(io::IOContext::LoadingState::Throttled);
	}

	auto* throttler = asyncContext->loadingContext.m_resourceLoader->GetThrottler();
	RED_FATAL_ASSERT(throttler);
	throttler->Run(&BuildCoreJobChain, asyncContext);
}

static void LoadObjectData( BinaryLoaderAsyncContext* asyncContext )
{
	IAsyncSource::CallbackContext callbackContext;
	callbackContext.userData = asyncContext;
	callbackContext.callback = &OnObjectDataLoaded;

	// If we're going to do the I/O for it and it can go in the game cache, then might as well go the whole nine yards
	// The PS4 can cancel I/O in flight, but the Xbox doesn't support it as well.
	// There's a fair chance we'll use it again soon anyway, and perhaps even prevent wasted reloading.
	if (asyncContext->loadingContext.m_useGameCache && asyncContext->ioContext)
	{
		// #tbd: could check now if already expired, but with archives that's 99.9% unlikely
		auto weakToken = asyncContext->callbackContext.tokenUserData;

		// The asyncContext outlives any I/O request, since it's notified on the result.
		asyncContext->ioContext->SetIOStartCallback([asyncContext, weakToken]() {
			asyncContext->keepAliveToken = weakToken.Lock();
		});
	}

	// Yes, diskSize (i.e., unpadded size)... because we'll convert to buffer size on the fly. CONFUSINGLY here to expose it and what to alloc normally.
	const AsyncSourceReadSizes& readSizes = asyncContext->readSizes;
	RED_FATAL_ASSERT( readSizes.IsValid() );
	const auto priority = asyncContext->loadingContext.m_priority;
	IAsyncSource::ReadAsyncParams params;
	params.requestSource = io::RequestSource::ResourceSystem;
	params.offset = 0;
	params.size = readSizes.diskSize;
	params.priority = priority;
	params.ioContext = asyncContext->ioContext;
	asyncContext->source->ReadAsync(params, AsyncSourceReadBuffer(readSizes), callbackContext);
}

static void OnFileTableHeaderLoaded(AsyncSourceReadBuffer buffer, red::UniqueBuffer, void* userData, red::EAsyncResult result)
{
	auto* asyncContext = static_cast<BinaryLoaderAsyncContext*>(userData);
	RED_FATAL_ASSERT(asyncContext);

	if (result == red::eAsyncResult_Canceled)
	{
		asyncContext->result.SetCancelled();
		NotifyFinished(asyncContext);
		return;
	}

	const auto& bufferView = buffer.ValidRegion();
	RED_FATAL_ASSERT( bufferView.Size() >= sizeof( FileTables::Header ), "Invalid buffer size for file tables header passed in" );

	const char* fileNameForDebug = asyncContext->loadingContext.m_resourcePath.ToDebugString();
	const FileTables::Header& header = bufferView.As<const FileTables::Header>();
	Uint32 version = 0;

	FileTables fileTables;
	if ( fileTables.CheckHeader( fileNameForDebug, header, version ) )
	{
		// Make sure file version is valid
		RED_FATAL_ASSERT( version >= VER_MINIMAL && version <= VER_CURRENT, "Invalid file version %d detected in data", version );

		// No compression from loose files
		asyncContext->readSizes = asyncContext->source->GetReadSizes( 0, header.m_objectsEnd );
		LoadObjectData( asyncContext );
	}
	else
	{
		// Trying to load a non-W2RC file (e.g., video file) through a loader. No point since we'll read the whole file in from disk only to fail when we
		// try again to read the header in the post-processing jobs.
		RED_LOG_ERROR( "BinaryLoader: Refusing to load unrecognized binary resource: %s", asyncContext->source->Debug_GetDescription().AsChar() );
		asyncContext->result.SetFailed();
		NotifyFinished( asyncContext );
		return;
	}
}

static void LoadFileTableHeader( BinaryLoaderAsyncContext* asyncContext )
{
	const auto& asyncSource = asyncContext->source;

	constexpr Uint32 headerSize = sizeof( FileTables::Header );

	const Uint32 sizeOnDisk = asyncSource->GetSizeOnDisk();
	if ( sizeOnDisk < headerSize )
	{
		RED_LOG_ERROR( "BinaryLoader: Data is too small to contain a binary file header: %u < %u", sizeOnDisk, headerSize );
		asyncContext->result.SetFailed();
		NotifyFinished( asyncContext );
		return;
	}

	// Allocate the memory for the header here since it's just a small fixed size
	io::ShareableIOMemory headerBuffer{ red::CreateUniqueBuffer< red::PoolResource >( headerSize, __alignof( FileTables::Header ) ) };

	IAsyncSource::CallbackContext callbackContext;
	callbackContext.userData = asyncContext;
	callbackContext.callback = &OnFileTableHeaderLoaded;
	IAsyncSource::ReadAsyncParams params;
	params.requestSource = io::RequestSource::ResourceSystem;
	params.offset = 0;
	params.size = headerSize;
	params.priority = asyncContext->loadingContext.m_priority;
	params.ioContext = asyncContext->ioContext;
	asyncSource->ReadAsync( params, AsyncSourceReadBuffer( std::move( headerBuffer ), 0 ), callbackContext );
}

static void KickOffImportsFromDependencyCache(res::ResourceToken& resourceToken, BinaryLoaderAsyncContext* asyncContext)
{
	const auto& context = asyncContext->loadingContext;
	if (context.m_skipImports)
	{
		return;
	}

	// #tbd: should make this a custom iterator instead
	red::DynArray<res::ResourcePath> deps{ job::PoolJobScope() };
	if (!asyncContext->source->TryGetResourceDependencies(deps))
	{
		return;
	}

	// Keep using the original request's ioHint. We may always use token loading from a different duplicated location,
	// but it shouldn't override the intent to load packed data
	// The I/O hints aren't so important elsewhere right now, so only done in this function for now.
	// "Streamed" (vs autoload) DDBs shouldn't have their data duplicated for now anyway.
	RawDiskPosition ioHint = context.ioHint;
	if (ioHint.fileHandle == io::INVALID_FILE_HANDLE)
	{
		(void)asyncContext->source->TryGetRawDiskPosition(ioHint);
	}

	resourceToken.Internal_KickOffImportsFromDependencyCache_NotThreadSafe(context, ioHint, deps);
}

} // helper

static thread_local Int32 g_tls_kickOffDepth = 0;

void BinaryLoader::LoadAsyncWithCallback( const AsyncSourcePtr& asyncSource, const LoadingContext& context, const CallbackContext& callbackContext, job::CompletionDeferral&& deferral ) const
{
	RED_FATAL_ASSERT( !context.m_initialResourcePath.ToStringView().EndsWith( ".bk2" ), "Refusing to async load video file: %s", context.m_initialResourcePath.ToDebugString() );
	RED_FATAL_ASSERT(context.m_resourceLoader);

	ScopedProfilerChannel scopedChannel( PBC_IO );
	PC_SCOPE( LoadAsyncWithCallback );

	RED_FATAL_ASSERT( callbackContext.callback );

	// Note: terrain and entity templates do something custom and don't use a token.
	//RED_FATAL_ASSERT( callbackContext.tokenUserData );

	helper::BinaryLoaderAsyncContext* asyncContext = helper::CreateBinaryLoaderAsyncContext();
	asyncContext->callbackContext = callbackContext;
	asyncContext->deferral = std::move( deferral );
	asyncContext->source = asyncSource;
	asyncContext->loadingContext = context;

	// Should 99.999% not have expired here yet. Guard against nullptr to avoid potential crashes, but don't overcomplicate by checking for cancellation yet.
	auto token = callbackContext.tokenUserData.Lock();

	if (token)
	{
		asyncContext->ioContext = token->ResourceLoaderInternal_GetIOContext();
	}

	const Uint32 diskSize = asyncSource->GetSizeOnDisk();
	if ( diskSize == 0 )
	{
		RED_LOG_ERROR( "BinaryLoader: File %hs is zero sized!", asyncSource->Debug_GetResourcePath().ToDebugString() );
		asyncContext->result.SetFailed();
		NotifyFinished( asyncContext );
		return;
	}

	// If a memory async source, then can just start deserialization now. The memory is to remain valid as long as we keep a strong reference to the async source.
	red::BlobView rawView;
	if ( asyncSource->TryGetMemorySource( rawView ) )
	{
		RED_FATAL_ASSERT(!asyncSource->RequiresHeaderPreParseStep(), "This should only be true for loose files");
		RED_FATAL_ASSERT(!asyncSource->GetReadSizes(0, asyncSource->GetSizeOnDisk()).IsCompressed(),
			"MemoryAsyncSource has no concept of in-memory buffer compresssion, and if it did we didn't get any memory from the AsyncIO system or anywhere else to decompress into");

		asyncContext->readSizes = asyncSource->GetReadSizes( 0, diskSize );
		asyncContext->onDiskBuffer = AsyncSourceReadBuffer(rawView);
		asyncContext->bufferForDecompressor = red::UniqueBuffer();
		
		// Kick off I/O but don't do all the postloading on the current unthrottled thread
		//asyncContext->forceThrottleOnPostLoad = true;

		// #tbd: just run it now without the throttler. Already decompressed and kicking off object loading/exporting
		// Best to get the I/O started ASAP
		red::BlobSpan noDedicatedDecompressionMemory; // nothing to decompress anyway
		helper::BuildCoreJobChain(asyncContext, noDedicatedDecompressionMemory);
	}
	else
	{
		constexpr Uint32 smallFileSize = static_cast<Uint32>( 128_KB ); // whether file small enough to do just one I/O read
		const bool sourceIsSmall = diskSize < smallFileSize;
		const bool sourceIsPacked = !asyncSource->RequiresHeaderPreParseStep();

		if ( sourceIsPacked || sourceIsSmall )
		{
			// Only archives have deps
			const Bool doBulkRead = (token != nullptr) && (g_tls_kickOffDepth == 0);
			if (doBulkRead)
			{
				io::GAsyncIO.ADVANCED_BeginBulkReadThreadLocal();
			}

			if (token)
			{
				// !!! IMPORTANT !!!
				// NOTE: Must kick off here before calling LoadObjectData() or LoadFileTableHeader
				// Because once we actually start loading, then we might actually finish and invalidate the asyncContext!

				g_tls_kickOffDepth += 1;
				helper::KickOffImportsFromDependencyCache(*token, asyncContext);
				g_tls_kickOffDepth -= 1;
			}

			asyncContext->readSizes = asyncSource->GetReadSizes( 0, diskSize );
			helper::LoadObjectData( asyncContext );

			if (doBulkRead)
			{
				io::GAsyncIO.ADVANCED_FinishBulkReadThreadLocal();
			}
		}
		else
		{
			helper::LoadFileTableHeader( asyncContext );
		}		
	}
}

// #tbd: this kicks of import loading but doesn't wait for it?!
Bool BinaryLoader::LoadFromMemory( const void* memory, const Uint32 size, const LoadingContext& context, LoadingResult& result, Bool doActiveWaiting )
{
	PC_SCOPE( LoadFromMemory );

	// no data
	if ( !memory || !size )
		return false;

	CMemoryFileReaderExternalBuffer dataReader( memory, size, context.m_resourcePath.ToDebugString() );

	// load
	Uint32 version = 0;
	FileTables fileTables;
	if ( !fileTables.Load( dataReader, version ) )
		return false;

	// setup version
	dataReader.m_version = version;

	// setup replication flag
	dataReader.SetCloner( context.m_replication );

	// resolve
	RuntimeTables runtimeTables{ RuntimeTables::eLoadExportsFromFile };
	runtimeTables.Resolve( context, fileTables );

	// build data
	runtimeTables.CreateExports( context, fileTables );

	// Unused since no asyncSource
	job::Counter nullCounter;

	// load data
	runtimeTables.LoadExports( 0, dataReader, nullCounter, context, fileTables, result );

	if ( doActiveWaiting )
	{
		runtimeTables.WaitUntilImportsAreLoaded_ACTIVELY();
	}

	job::Builder builder{ job::Priority::Latent };
	Bool isPostLoadCalled = runtimeTables.PostLoad( context, builder );

	// CResource only and assumes no embedded resources, so process the only the root object.
	// If necessary, could iterate and cast all objects to check. But so far a hack only needed for entities and meshes.
	if (isPostLoadCalled && !result.m_loadedRootObjects.Empty())
	{
		const auto resource = Cast< CResource >(result.m_loadedRootObjects[0]);
		if (resource && resource->HACK_GetPostLoadWaitCounter())
		{
			if (doActiveWaiting)
			{
				// This is very bad, but no choice here. Could even deadlock if doing this from multiple threads at once.
				// Note we do something similar anyway for imports above.
				const job::Counter* counter = resource->HACK_GetPostLoadWaitCounter();
				while (!counter->Internal_IsZeroSnapshot())
				{
					continue;
				}
			}
		}
	}

	return true;
}

const Bool BinaryLoader::ValidateTables( const Uint64 baseOffset, IFile& file, const FileTables& fileTables )
{
	Uint8 readBuffer[ 16 * 1024 ];

	// validate exports
	for ( const auto& ex : fileTables.m_exports )
	{
		// no CRC specified
		if ( !ex.m_crc )
			continue;

		// move to object position
		Uint32 objectSize = ex.m_dataSize;
		file.Seek( baseOffset + ex.m_dataOffset );

		// read data and compute CRC
		Uint32 crc = 0;
		while ( objectSize > 0 )
		{
			// read chunk
			Uint32 maxRead = Min< Uint32 >( RED_ARRAY_COUNT_U32( readBuffer ), objectSize );
			file.Serialize( readBuffer, maxRead );
			objectSize -= maxRead;

			// compute CRC
			crc = red::CalculateCRC32( readBuffer, maxRead, crc );
		}

		// compare it
		if ( ex.m_crc != crc )
		{
			const auto* className = &fileTables.m_strings[ ex.m_className ];
			const auto objectIndex = &ex - fileTables.m_exports.TypedData();

			RED_LOG_ERROR( "Core: !!! FILE CORRUPTION !!!" );
			RED_LOG_ERROR( "Core: Object %d of class '%hs' at offset %d, size %d in '%hs' is corrupted (CRC mismatch).",
						   objectIndex, className,
						   ex.m_dataOffset, ex.m_dataSize, file.GetFileNameForDebug() );

			return false;
		}
	}

	// validate buffers
	for ( const auto& ex : fileTables.m_buffers )
	{
		// no CRC specified
		if ( !ex.m_crc )
			continue;

		// move to object position
		Uint32 objectSize = ex.m_dataSizeOnDisk;
		file.Seek( baseOffset + ex.m_dataOffset );

		// read data and compute CRC
		Uint32 crc = 0;
		while ( objectSize > 0 )
		{
			// read chunk
			Uint32 maxRead = Min< Uint32 >( RED_ARRAY_COUNT_U32( readBuffer ), objectSize );
			file.Serialize( readBuffer, maxRead );
			objectSize -= maxRead;

			// compute CRC
			crc = red::CalculateCRC32( readBuffer, maxRead, crc );
		}

		// compare it
		if ( ex.m_crc != crc )
		{
			const auto bufferIndex = &ex - fileTables.m_buffers.TypedData();

			RED_LOG_ERROR( "Core: !!! FILE CORRUPTION !!!" );
			RED_LOG_ERROR( "Core: Buffer %d at offset %d, size %d in '%hs' is corrupted (CRC mismatch).",
						   bufferIndex, ex.m_dataOffset, ex.m_dataSizeOnDisk, file.GetFileNameForDebug() );

			return false;
		}
	}

	// No errors found
	return true;
}

} // serialization
