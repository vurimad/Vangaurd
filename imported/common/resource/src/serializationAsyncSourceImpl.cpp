/*
 * Copyright (c) 2017 CD Projekt Red. All Rights Reserved.
 */
#include "build.h"
#include "serializationAsyncSourceImpl.h"
#include "../../redJobs2/include/jobRunner.h"
#include "../../redJobs2/include/jobDeferral.h"
#include "../../redReflection/include/serializationAsyncSource.h"
#include "../../redReflection/include/serializationLoader.h"
#include "../../redReflection/include/bufferHandle.h"
#include "../../redIO/include/redIOCommon.h"
#include "../../redIO/include/redIOAsyncIO.h"
#include "../../archive/include/common.h"
#include "../../archive/include/fileMetadata.h"
#include "../../archive/include/fileTable.h"
#include "../../redReflection/include/resourceLoaderThrottler.h"

namespace serialization
{
	namespace helper
	{
		// Used to smuggle non-owned memory in a uniquebuffer without freeing it afterwards
		struct NullBufferProxy
		{
			void* ReallocateAligned(void* inputBuffer, Uint32 size, Uint32 alignment)
			{
				ALWAYSENABLED_RED_FATAL("Not allowed");
				return nullptr;
			}

			static NullBufferProxy& GetInstance()
			{
				static serialization::helper::NullBufferProxy proxy;
				return proxy;
			}
		};
	}
}

namespace red
{
	namespace internal
	{
		template<>
		class UniqueBufferAllocator< serialization::helper::NullBufferProxy > : public UniqueBufferAllocatorInterface
		{
		public:
			UniqueBufferAllocator(serialization::helper::NullBufferProxy*)
			{}

			virtual void* ReallocateAligned(void* inputBuffer, Uint32 size, Uint32 alignment) override final
			{
				ALWAYSENABLED_RED_MEMORY_FATAL("Not allowed");
				return nullptr;
			}

			virtual void Free(void* buffer) override final
			{
				// No op
			}

			virtual const red::memory::Pool* GetPool() const override
			{
				ALWAYSENABLED_RED_MEMORY_FATAL("Not allowed");
				return nullptr;
			}
		};
	}
}

#ifndef NO_EDITOR
namespace io
{
	namespace hacks
	{
		extern REDIO_API void SetIsPriorityReadInline_ThreadLocal(Bool isReadInline);
	}
}

namespace job
{
	namespace prv
	{
		namespace hacks
		{
			REDJOBS2_API Bool IsDispatcherThread();
		}
	}
}
#endif

namespace serialization
{

//------------------------------------------------------------------------------

static thread_local Bool g_isBinkVideoOpen = false;
void TEMPHACK_SetBinkVideoOpen_ThreadLocal(Bool value)
{
	g_isBinkVideoOpen = value;
}

// Memory buffer based Async Source
class MemoryAsyncSource final : public IAsyncSource
{
public:
	explicit MemoryAsyncSource( red::UniqueBuffer&& buffer );
	explicit MemoryAsyncSource( const red::BlobView& bufferView );

	virtual String Debug_GetDescription() const override;

	virtual Uint32 GetSizeOnDisk() const override;
	virtual AsyncSourceReadSizes GetReadSizes( Uint32 offset, Uint32 size ) const override;

	virtual void ReadAsync( const ReadAsyncParams& params, AsyncSourceReadBuffer readBuffer, const CallbackContext& callbackContext ) const override;
	virtual bool ReadInline( Uint32 offset, Uint32 size, const red::BlobSpan& readBuffer, io::EAsyncPriority priority, io::RequestSource requestSource, red::ManualResetEvent* optionalWaitEvent ) const override;

	virtual Bool TryGetMemorySource(red::BlobView& outView) const override;

private:
	red::UniqueBuffer m_ownedBuffer;
	red::BlobView m_bufferView;
};

MemoryAsyncSource::MemoryAsyncSource( red::UniqueBuffer&& buffer )
	: m_ownedBuffer( std::move( buffer ) )
	, m_bufferView( red::MakeBlobView( m_ownedBuffer ) )
{
}

MemoryAsyncSource::MemoryAsyncSource( const red::BlobView& bufferView )
	: m_bufferView( bufferView )
{
}

String MemoryAsyncSource::Debug_GetDescription() const
{
	return String::Printf( "MemoryAsyncSource (%u)", m_bufferView.Size() );
}

Uint32 MemoryAsyncSource::GetSizeOnDisk() const
{
	return m_bufferView.Size();
}

AsyncSourceReadSizes MemoryAsyncSource::GetReadSizes( Uint32 offset, Uint32 size ) const
{
	AsyncSourceReadSizes result;
	if ( offset + size <= m_bufferView.Size() )
	{
		result.memorySize = result.diskSize = result.bufferSize = size;
	}
	return result;
}

void MemoryAsyncSource::ReadAsync( const ReadAsyncParams& params, AsyncSourceReadBuffer readBuffer, const CallbackContext& callbackContext ) const
{
	const auto size = params.size;
	const auto offset = params.offset;
	const auto priority = params.priority;

	// REMOVE THIS: possibly backend terrain could hit this, I don't know for sure yet
	// Just in case for now
#ifndef NO_EDITOR
	if (!IsGameMode())
	{
		if (!readBuffer.GetShareableIOMemory().Data() && size > 0)
		{
			RED_LOG_ERROR("Something didn't preallocate memory for a MemoryAsyncSource");
			auto tempBuffer = io::ShareableIOMemory(red::CreateUniqueBuffer<red::PoolResource>(size, 16));
			auto readSizes = readBuffer.GetReadSizes();
			readBuffer = AsyncSourceReadBuffer(std::move(tempBuffer), 0, readSizes);
		}
	}
#endif

	RED_FATAL_ASSERT( size <= readBuffer.GetShareableIOMemory().GetSize(), "MemoryAsyncSource: Buffer supplied is not large enough to read into %u < %u", readBuffer.GetShareableIOMemory().GetSize(), size );
	RED_FATAL_ASSERT( offset + size <= m_bufferView.Size(), "MemoryAsyncSource: Offset + size (%u + %u) exceeds memory size %u", offset, size, m_bufferView.Size() );
	RED_FATAL_ASSERT( callbackContext.callback );

	const bool isCompressed = readBuffer.GetReadSizes().IsCompressed();
	RED_FATAL_ASSERT(!params.allocDeserializationMemoryForDecompressorIfCompressed || !isCompressed, "FIXME? Or alloc on the fly from some pool (but not IO) if needed");

	// TODO: error checks and cancellations
	red::Memcpy(readBuffer.GetShareableIOMemory().Data(), m_bufferView.Data(), size);
	auto callback = callbackContext.callback;
	void* const userData = callbackContext.userData;
	callback(std::move(readBuffer), red::UniqueBuffer(), userData, red::eAsyncResult_Success);
}

bool MemoryAsyncSource::ReadInline( Uint32 offset, Uint32 size, const red::BlobSpan& readBuffer, io::EAsyncPriority, io::RequestSource requestSource, red::ManualResetEvent* optionalWaitEvent ) const
{
	RED_FATAL_ASSERT( size <= readBuffer.Size() );
	Bool ret = false;
	if ( offset + size <= m_bufferView.Size() )
	{
		red::Memcpy( readBuffer.Data(), m_bufferView.Data( offset ), size );
		ret = true;
	}

	// No point resetting the event
	if (optionalWaitEvent)
	{
		optionalWaitEvent->SetEvent();
	}

	return ret;
}

Bool MemoryAsyncSource::TryGetMemorySource(red::BlobView& outView) const
{
	outView = m_bufferView;
	return true;
}

//------------------------------------------------------------------------------

namespace helper
{

struct ReadFileInlineContext
{
	explicit ReadFileInlineContext(red::ManualResetEvent* optionalWaitEvent)
		: m_optionalWaitEvent(optionalWaitEvent)
		, m_status(-1)
	{}

	red::ManualResetEvent* m_optionalWaitEvent;
	red::Atomic<Int32> m_status;
};

static Bool ReadFileInline(const char* debugLogicalFileName, io::TFileHandle handle, Uint64 offset, Uint32 size, void* readBuffer, io::EAsyncPriority priority, io::RequestSource requestSource, red::ManualResetEvent* optionalWaitEvent)
{
	//job::FatalAssertIfDispatcherThread();

	RED_FATAL_ASSERT(readBuffer);

	ReadFileInlineContext context{ optionalWaitEvent };

	io::AsyncReadToken token;
	token.m_requestSource = requestSource;
	token.m_buffer = readBuffer;
	token.m_debugLogicalFileName = debugLogicalFileName;
	token.m_callback = [](const io::AsyncReadToken& asyncReadToken, red::EAsyncResult asyncResult, Uint32 numberOfBytesTransferred, io::ShareableIOMemory, Uint32, red::UniqueBuffer)
	{
		auto* context = static_cast<ReadFileInlineContext*>(asyncReadToken.m_userData);

		// Cache before setting the status. If no event, then another thread is spinning on status
		// and the context can become invalid as soon as we set status.
		// ATOMIC: because I don't want to wonder later if the optimizer was too clever
		const red::Atomic< Bool > hasWaitEvent{ context->m_optionalWaitEvent != nullptr };

		if (asyncResult == red::EAsyncResult::eAsyncResult_Success)
		{
			context->m_status.SetValue(1);
		}
		else
		{
			context->m_status.SetValue(0);
		}

		if (hasWaitEvent.GetValue())
		{
			context->m_optionalWaitEvent->SetEvent();
		}
	};

	token.m_numberOfBytesToRead = size;
	token.m_offset = offset;
	token.m_userData = &context;
	token.m_debugLogicalFileName = debugLogicalFileName;
	token.m_numberOfBytesToAllocateForDecompressor = 0;
	token.m_HACK_mightBeTerrainAndNeedsATonOfMemoryForBuffers = false;

	// Reset before beginning the async read
	if (optionalWaitEvent)
	{
		optionalWaitEvent->ResetEvent();
	}

#ifndef NO_EDITOR
	const Bool isPriorityReadInline = SIsMainThread() || job::prv::hacks::IsDispatcherThread();
	if (isPriorityReadInline)
	{
		io::hacks::SetIsPriorityReadInline_ThreadLocal(true);
	}
#endif

	io::GAsyncIO.BeginRead(handle, token, priority);

#ifndef NO_EDITOR
	if (isPriorityReadInline)
	{
		io::hacks::SetIsPriorityReadInline_ThreadLocal(false);
	}
#endif

	if (optionalWaitEvent)
	{
		optionalWaitEvent->Wait();
	}
	else
	{
		for (;;)
		{
			const Int32 currentStatus = context.m_status.GetValue();
			if (currentStatus != -1)
			{
				break;
			}

			//#tbd: don't want to change too much editor behavior here yet
			if (IsGameMode())
			{
				red::SleepOnCurrentThread(0);
			}
			else
			{
				red::YieldCurrentThread(); // <-- Bink IO thread won't actually yield to another thread on PS4, since it has higher priority
			}
		}
	}

	const Int32 finalStatus = context.m_status.GetValue();
	RED_FATAL_ASSERT(finalStatus != -1);
	if (finalStatus == 0)
	{
		return false;
	}
	return true;
}

} // helper

//------------------------------------------------------------------------------

namespace helper
{

struct AsyncReadFileContext : public red::NonCopyable
{
	RED_USE_MEMORY_POOL( red::PoolResource );

	AsyncSourceReadBuffer readBuffer;
	IAsyncSource::CallbackContext callbackContext;
	String debugName;
};

#ifdef RED_ENABLE_ASYNC_CONTEXT_DEBUGGING

red::DynArray< AsyncReadFileContext* > g_asyncReadFileContexts{ red::PoolDebug() };
red::RWSpinLock g_asyncReadFileContextsGuard;

static AsyncReadFileContext* CreateAsyncReadFileContext()
{
	RED_SCOPE_LOCK( g_asyncReadFileContextsGuard );
	auto* result = RED_NEW( AsyncReadFileContext );
	g_asyncReadFileContexts.PushBack( result );
	return result;
}

static void DestroyAsyncReadFileContext( AsyncReadFileContext* context )
{
	RED_SCOPE_LOCK( g_asyncReadFileContextsGuard );
	g_asyncReadFileContexts.Remove( context );
	RED_DELETE( context );
}

#else

static AsyncReadFileContext* CreateAsyncReadFileContext()
{
	return RED_NEW( AsyncReadFileContext );
}

static void DestroyAsyncReadFileContext( AsyncReadFileContext* context )
{
	RED_DELETE( context );
}

#endif

static void ReadAsyncDoneCallback( const io::AsyncReadToken& asyncReadToken, red::EAsyncResult asyncResult, Uint32 numberOfBytesTransferred,
	io::ShareableIOMemory internalMemoryForIO, Uint32 shareableIOMemoryOffset, red::UniqueBuffer internalMemoryForDecompressor )
{
	auto* asyncContext = static_cast<AsyncReadFileContext*>( asyncReadToken.m_userData );
	RED_FATAL_ASSERT( asyncContext );
	RED_FATAL_ASSERT( asyncResult == red::eAsyncResult_Success || numberOfBytesTransferred == 0, "Read buffer has data but failed: async result %d", (Int32)asyncResult );
	RED_FATAL_ASSERT( numberOfBytesTransferred == asyncReadToken.m_numberOfBytesToRead || asyncResult != red::eAsyncResult_Success, "Partial read %u/%u bytes", numberOfBytesTransferred, asyncReadToken.m_numberOfBytesToRead );

	if (internalMemoryForIO.Data())
	{
		RED_FATAL_ASSERT(asyncContext->readBuffer.GetShareableIOMemory().Data() == nullptr, "readBuffer already initialized");

		RED_FATAL_ASSERT(asyncContext->readBuffer.GetReadSizes().bufferSize <= internalMemoryForIO.GetSize() - shareableIOMemoryOffset);

		const auto readSizes = asyncContext->readBuffer.GetReadSizes();
		asyncContext->readBuffer = AsyncSourceReadBuffer(std::move(internalMemoryForIO), shareableIOMemoryOffset, readSizes);
		RED_FATAL_ASSERT(!asyncContext->readBuffer.IsEmpty());
	}

	auto callback = asyncContext->callbackContext.callback;
	void* const userData = asyncContext->callbackContext.userData;
	callback( std::move( asyncContext->readBuffer ), std::move(internalMemoryForDecompressor), userData, asyncResult );
	DestroyAsyncReadFileContext( asyncContext );
}

} // helper

//------------------------------------------------------------------------------

class FileAsyncSource final : public IAsyncSource
{
public:
	FileAsyncSource( const res::ResourcePath& resourcePathForDebug, io::TFileHandle fileHandle );
	virtual ~FileAsyncSource();

	virtual String Debug_GetDescription() const override;
	virtual res::ResourcePath Debug_GetResourcePath() const override;

	virtual Uint32 GetSizeOnDisk() const override;
	virtual bool RequiresHeaderPreParseStep() const override;

	virtual AsyncSourceReadSizes GetReadSizes( Uint32 offset, Uint32 size ) const override;

	virtual void ReadAsync( const ReadAsyncParams& params, AsyncSourceReadBuffer readBuffer, const CallbackContext& callbackContext ) const override;
	virtual bool ReadInline( Uint32 offset, Uint32 size, const red::BlobSpan& readBuffer, io::EAsyncPriority priority, io::RequestSource requestSource, red::ManualResetEvent* optionalWaitEvent ) const override;

private:
	io::TFileHandle m_handle;
	Uint32 m_size;
	res::ResourcePath m_resourcePathForDebug;
};

FileAsyncSource::FileAsyncSource( const res::ResourcePath& resourcePathForDebug, io::TFileHandle fileHandle )
	: m_resourcePathForDebug( resourcePathForDebug )
	, m_handle( fileHandle )
	, m_size( 0 )
{
	RED_FATAL_ASSERT( m_handle != io::INVALID_FILE_HANDLE );

	io::GAsyncIO.AddRefFile( m_handle );

	m_size = static_cast<Uint32>( io::GAsyncIO.GetFileSize( m_handle ) );
}

FileAsyncSource::~FileAsyncSource()
{
	io::GAsyncIO.ReleaseFile( m_handle );
	m_handle = io::INVALID_FILE_HANDLE;
}

String FileAsyncSource::Debug_GetDescription() const
{
	return String::Printf( "FileAsyncSource (%hs) (%hs)", io::GAsyncIO.GetFileName( m_handle ), m_resourcePathForDebug.ToDebugString() );
}

res::ResourcePath FileAsyncSource::Debug_GetResourcePath() const
{
	return m_resourcePathForDebug;
}

Uint32 FileAsyncSource::GetSizeOnDisk() const
{
	return m_size;
}

bool FileAsyncSource::RequiresHeaderPreParseStep() const
{
	return true;
}

AsyncSourceReadSizes FileAsyncSource::GetReadSizes( Uint32 offset, Uint32 size ) const
{
	AsyncSourceReadSizes result;
	if ( offset + size <= m_size )
	{
		result.memorySize = result.diskSize = result.bufferSize = size;
	}
	return result;
}

void FileAsyncSource::ReadAsync( const ReadAsyncParams& params, AsyncSourceReadBuffer readBuffer, const CallbackContext& callbackContext ) const
{
	const auto size = params.size;
	const auto offset = params.offset;
	const auto priority = params.priority;

	RED_FATAL_ASSERT( m_handle != io::INVALID_FILE_HANDLE );
	RED_FATAL_ASSERT( callbackContext.callback );

	RED_FATAL_ASSERT( offset + size <= m_size, "FileAsyncSource: Offset + size (%u + %u) exceeds size %u in file %s", offset, size, m_size, m_resourcePathForDebug.ToDebugString() );
	RED_FATAL_ASSERT( !readBuffer.GetShareableIOMemory().Data() || size <= readBuffer.GetShareableIOMemory().GetSize(), "FileAsyncSource: Buffer (size %u) not big enough read %u bytes in file %s",
		readBuffer.GetShareableIOMemory().GetSize(), size, m_resourcePathForDebug.ToDebugString() );

	auto* asyncContext = helper::CreateAsyncReadFileContext();
	asyncContext->readBuffer = std::move( readBuffer );
	asyncContext->callbackContext = callbackContext;
	asyncContext->debugName = m_resourcePathForDebug.ToDebugString();

	// Note: only allocate another buffer if compressed, not if padded but uncompressed.
	// Unlike deferred data buffers, all of the memory allocated here is from the I/O pool and not kept anyway.
	Uint32 numBytesForDecompressor = 0;
	const bool isCompressed = asyncContext->readBuffer.GetReadSizes().IsCompressed();
	if (isCompressed && params.allocDeserializationMemoryForDecompressorIfCompressed)
	{
		numBytesForDecompressor = asyncContext->readBuffer.GetReadSizes().memorySize;
	}

	io::AsyncReadToken token;
	token.m_requestSource = params.requestSource;
	token.m_buffer = asyncContext->readBuffer.GetShareableIOMemory().Data();
	token.m_numberOfBytesToRead = size;
	token.m_offset = offset;
	token.m_callback = &helper::ReadAsyncDoneCallback;
	token.m_userData = asyncContext;
	token.m_debugLogicalFileName = m_resourcePathForDebug.ToDebugString();
	token.m_numberOfBytesToAllocateForDecompressor = numBytesForDecompressor;
	token.m_HACK_mightBeTerrainAndNeedsATonOfMemoryForBuffers = params.HACK_mightBeTerrainAndNeedsATonOfMemoryForBuffers;
	token.m_ioContext = params.ioContext;

	io::GAsyncIO.BeginRead( m_handle, token, priority );
}

bool FileAsyncSource::ReadInline( Uint32 offset, Uint32 size, const red::BlobSpan& readBuffer, io::EAsyncPriority priority, io::RequestSource requestSource, red::ManualResetEvent* optionalWaitEvent ) const
{
	// Override if using GAME priority, because Bink can stall entire frame
	if (g_isBinkVideoOpen)
	{
		priority = io::eAsyncPriority_FULLSCREENVIDEO;
	}

	RED_FATAL_ASSERT( m_handle != io::INVALID_FILE_HANDLE );
	RED_FATAL_ASSERT( readBuffer.Data() != nullptr );
	RED_FATAL_ASSERT( size <= readBuffer.Size() );

	RED_FATAL_ASSERT( offset + size <= m_size, "FileAsyncSource: Offset + size (%u + %u) exceeds size %u in file %s", offset, size, m_size, m_resourcePathForDebug.ToDebugString() );

	if ( offset + size <= m_size )
	{
		return helper::ReadFileInline( m_resourcePathForDebug.ToDebugString(), m_handle, offset, size, readBuffer.Data(), priority, requestSource, optionalWaitEvent );
	}

	return false;
}

//------------------------------------------------------------------------------

class ArchiveAsyncSource final : public IAsyncSource
{
public:
	ArchiveAsyncSource( const red::BlobView& archiveBlobView, io::TFileHandle fileHandle, const archive::FileMetadata& metadata, const archive::FileTable& table );
	virtual ~ArchiveAsyncSource();

	virtual String Debug_GetDescription() const override;
	virtual res::ResourcePath Debug_GetResourcePath() const override;

	virtual Uint32 GetSizeOnDisk() const override;

	virtual AsyncSourceReadSizes GetReadSizes( Uint32 offset, Uint32 size ) const override;

	virtual void ReadAsync( const ReadAsyncParams& params, AsyncSourceReadBuffer readBuffer, const CallbackContext& callbackContext) const override;
	virtual bool ReadInline( Uint32 offset, Uint32 size, const red::BlobSpan& readBuffer, io::EAsyncPriority priority, io::RequestSource requestSource, red::ManualResetEvent* optionalEvent ) const override;
	virtual bool TryGetMemorySourceFromInlineData(const ReadAsyncParams& params, const AsyncSourceReadBuffer& inlineData, red::BlobView& outView) const override;

	virtual Bool TryGetResourceDependencies(red::DynArray<res::ResourcePath>& outDependencies) const override;

	virtual bool TryGetRawDiskPosition(RawDiskPosition& outPosition) const override;

private:
	bool FindFileSegment( Uint32 offset, archive::FileSegment& result, Uint32& segmentOffset ) const;

	archive::FileMetadata m_metadata;
	const archive::FileTable& m_table;
	red::BlobView m_archiveBlobView;
	io::TFileHandle m_handle;
};

ArchiveAsyncSource::ArchiveAsyncSource(const red::BlobView& archiveBlobView, io::TFileHandle fileHandle, const archive::FileMetadata& metadata, const archive::FileTable& table)
	: m_metadata(metadata)
	, m_table(table)
	, m_archiveBlobView(archiveBlobView)
	, m_handle(fileHandle)
{
	RED_FATAL_ASSERT(m_handle != io::INVALID_FILE_HANDLE);

	io::GAsyncIO.AddRefFile(m_handle);
}

ArchiveAsyncSource::~ArchiveAsyncSource()
{
	io::GAsyncIO.ReleaseFile( m_handle );
	m_handle = io::INVALID_FILE_HANDLE;
}

String ArchiveAsyncSource::Debug_GetDescription() const
{
	auto filename = m_metadata.ToDebugString();
	const char* archivePath = io::GAsyncIO.GetFileName( m_handle );

	const auto& segment = m_metadata.GetSegments( m_table ).Front();
	const Uint64 position = segment.position;

	return String::Printf( "ArchiveAsyncSource (%hs) - %hs : %llu", filename, archivePath, position );
}

res::ResourcePath ArchiveAsyncSource::Debug_GetResourcePath() const
{
	return res::ResourcePath::Build( m_metadata.GetIDCode() );
}

Uint32 ArchiveAsyncSource::GetSizeOnDisk() const
{
	return m_metadata.GetSegments( m_table ).Front().sizeOnDisk;
}

namespace helper
{

#ifdef RED_PLATFORM_ORBIS
static constexpr Uint32 c_readAlignment = 1;
#else
static constexpr Uint32 c_readAlignment = static_cast<Uint32>( 4_KB );
#endif

Uint64 AlignReadPosition( Uint64 offset )
{
	return red::AlignDown( offset, c_readAlignment );
}

Uint32 AlignReadSize( Uint32 size )
{
	return red::AlignUp( size, c_readAlignment );
}

bool IsAlignedForRead( Uint64 value )
{
	return red::IsAligned( value, c_readAlignment );
}

bool CheckSegmentRead( const archive::FileSegment& segment, Uint32 segmentOffset, Uint32 size )
{
	if ( segment.IsCompressed() )
	{
		// If we are compressed then we only support reads which exactly match
		// a segment we have in the archive. This is for normal resources and DDBs
		RED_FATAL_ASSERT( segmentOffset == 0 && size == segment.sizeOnDisk, 
			"ArchiveAsyncSource: Reads from compressed segments must read entire segment. Attempting to read from offset %u size %u from segment %llu size %u",
			segmentOffset, size, segment.position, segment.sizeOnDisk );

		return segmentOffset == 0 && size == segment.sizeOnDisk;
	}
	else
	{
		// Uncompressed files we support reading at arbitrary positions
		// but still check that we are completely within a single segment
		RED_FATAL_ASSERT( segmentOffset + size <= segment.sizeOnDisk, 
			"ArchiveAsyncSource: Read from uncompressed segments outside of bounds! %u + %u > %u", segmentOffset, size, segment.sizeOnDisk );

		return segmentOffset + size <= segment.sizeOnDisk;
	}
}

} // helper

AsyncSourceReadSizes ArchiveAsyncSource::GetReadSizes( Uint32 offset, Uint32 size ) const
{
	AsyncSourceReadSizes result;

	archive::FileSegment segment;
	Uint32 segmentOffset = 0;
	if ( FindFileSegment( offset, segment, segmentOffset ) && helper::CheckSegmentRead( segment, segmentOffset, size ) )
	{
		if ( segment.IsCompressed() )
		{
			result.memorySize = segment.sizeInMemory;
			result.diskSize = segment.sizeOnDisk;
			// Note: segmentOffset == 0 and size == segment.sizeOnDisk from helper::CheckSegmentRead above
		}
		else
		{
			result.diskSize = result.memorySize = size;
		}

		Uint32 sizeIncludingOverread = size;
		if ( offset == 0 && size == segment.sizeOnDisk )
		{
			const Uint32 numInlinedBufferSegments = m_metadata.GetNumInlinedBufferSegments();
			if ( numInlinedBufferSegments > 0 )
			{
				// Combine the size of the inline buffer segments into the buffer needed to read this file
				const auto& segments = m_metadata.GetSegments( m_table );
				RED_FATAL_ASSERT( numInlinedBufferSegments < segments.Size() );

				// Since the inline buffer segments are not at the start of the segments list
				// scan through and add a segment if it's directly after the previous one in this file
				Uint64 currentPos = segment.position + segment.sizeOnDisk;
				Uint32 segmentsAdded = 0;

				// First file segment we've already read, so skip it when iterating inline buffer segments
				for ( Uint32 i = 1; i < segments.Size() && segmentsAdded < numInlinedBufferSegments; ++i )
				{
					const auto& inlineSegment = segments[i];
					if ( inlineSegment.position == currentPos )
					{
						currentPos += inlineSegment.sizeOnDisk;
						sizeIncludingOverread += inlineSegment.sizeOnDisk;
						segmentsAdded++;
					}
				}
				RED_FATAL_ASSERT( segmentsAdded == numInlinedBufferSegments );

				result.isOverReadForInlineBuffers = true;
			}
		}

		const Uint64 fileReadPosition = segment.position + segmentOffset;
		const Uint64 bufferReadPosition = helper::AlignReadPosition( fileReadPosition );
		result.bufferOffset = static_cast<Uint32>( fileReadPosition - bufferReadPosition );
		result.bufferSize = helper::AlignReadSize( result.bufferOffset + sizeIncludingOverread );
	}

	return result;
}

bool ArchiveAsyncSource::TryGetMemorySourceFromInlineData(const ReadAsyncParams& params, const AsyncSourceReadBuffer& inlineData, red::BlobView& outView) const
{
	const auto size = params.size;
	const auto offset = params.offset;

	// We know the offset and size at this point and we will only ever read data from a single file segment
	archive::FileSegment segment;
	Uint32 segmentOffset = 0;
	if (!FindFileSegment(offset, segment, segmentOffset))
	{
		ALWAYSENABLED_RED_FATAL("ArchiveAsyncSource: Unknown file segment for read at offset %u, size %u", offset, size);
		return false;
	}

	if (!helper::CheckSegmentRead(segment, segmentOffset, size))
	{
		ALWAYSENABLED_RED_FATAL("ArchiveAsyncSource: Bad read attempted at file offset %u segment offset %u size %u in segment %llu size %u",
			offset, segmentOffset, size, segment.position, segment.sizeOnDisk);
		return false;
	}

	const Uint64 fileReadPosition = segment.position + segmentOffset;

	if (!inlineData.TryCreateValidRegion(fileReadPosition, size, outView))
	{
		return false;
	}

	return true;
}

void ArchiveAsyncSource::ReadAsync(const ReadAsyncParams& params, AsyncSourceReadBuffer readBuffer, const CallbackContext& callbackContext) const
{
	const auto size = params.size;
	const auto offset = params.offset;
	const auto priority = params.priority;

	RED_FATAL_ASSERT( m_handle != io::INVALID_FILE_HANDLE );
	RED_FATAL_ASSERT( size != 0, "Not expecting to read 0 sized file from archive!" );
	RED_FATAL_ASSERT( callbackContext.callback );

	// We know the offset and size at this point and we will only ever read data from a single file segment
	archive::FileSegment segment;
	Uint32 segmentOffset = 0;
	if ( !FindFileSegment( offset, segment, segmentOffset ) )
	{
		ALWAYSENABLED_RED_FATAL( "ArchiveAsyncSource: Unknown file segment for read at offset %u, size %u", offset, size );
		return;
	}

	if ( !helper::CheckSegmentRead( segment, segmentOffset, size ) )
	{
		ALWAYSENABLED_RED_FATAL( "ArchiveAsyncSource: Bad read attempted at file offset %u segment offset %u size %u in segment %llu size %u", 
			offset, segmentOffset, size, segment.position, segment.sizeOnDisk );
		return;
	}

	const AsyncSourceReadSizes standardReadSizes = GetReadSizes(offset, size);

	// Here if compressed segment then segmentOffset == 0 and size == segment.sizeOnDisk
	// If uncompressed then any read is ok
	const Uint64 fileReadPosition = segment.position + segmentOffset;
	const Uint64 bufferReadPosition = helper::AlignReadPosition(fileReadPosition);
	const Uint32 bufferFileOffset = standardReadSizes.bufferOffset;
	const Uint32 bufferReadSize = standardReadSizes.bufferSize;

	RED_FATAL_ASSERT( !readBuffer.GetShareableIOMemory().Data() || bufferReadSize <= readBuffer.GetShareableIOMemory().GetSize(), "ArchiveAsyncSource: Invalid buffer specified for reading provided: %u needed: %u",
		readBuffer.GetShareableIOMemory().GetSize(), bufferReadSize );

	auto* asyncContext = helper::CreateAsyncReadFileContext();
	asyncContext->readBuffer = std::move( readBuffer );
	asyncContext->callbackContext = callbackContext;
#if !defined( RED_CONFIGURATION_FINAL ) || defined( USE_PROFILER )
	asyncContext->debugName = res::ResourcePath::Build( m_metadata.GetID().GetCode() ).ToDebugString();
#else
	asyncContext->debugName = m_metadata.GetID().ToDebugString();
#endif

	// Note: only allocate another buffer if compressed, not if padded but uncompressed.
	// Unlike deferred data buffers, all of the memory allocated here is from the I/O pool and not kept anyway.
	Uint32 numBytesForDecompressor = 0;
	const bool isCompressed = asyncContext->readBuffer.GetReadSizes().IsCompressed();
	if (isCompressed && params.allocDeserializationMemoryForDecompressorIfCompressed)
	{
		numBytesForDecompressor = asyncContext->readBuffer.GetReadSizes().memorySize;
	}

	io::AsyncReadToken token;
	token.m_requestSource = params.requestSource;
	token.m_ioContext = params.ioContext;
	token.m_buffer = asyncContext->readBuffer.GetShareableIOMemory().Data();
	token.m_numberOfBytesToRead = bufferReadSize;
	token.m_offset = bufferReadPosition;
	token.m_callback = &helper::ReadAsyncDoneCallback;
	token.m_userData = asyncContext;
	token.m_debugLogicalFileName = asyncContext->debugName.AsChar();
	token.m_HACK_mightBeTerrainAndNeedsATonOfMemoryForBuffers = params.HACK_mightBeTerrainAndNeedsATonOfMemoryForBuffers;
	
	// NOTE: archives always use the binaryLoader, so will use the resourceThottler and have the dedicated I/O memory available
	token.m_numberOfBytesToAllocateForDecompressor = numBytesForDecompressor < res::ResourceLoaderThrottler::GetMaxNoDedicatedDecompressionMemory() ? 0 : numBytesForDecompressor;

	RED_FATAL_ASSERT(m_archiveBlobView.Empty() || token.m_numberOfBytesToAllocateForDecompressor == 0,
		"Too large a compressed file stored in this memory cached archive: %hs %hs: %u",
		io::GAsyncIO.GetFileName(m_handle), Debug_GetResourcePath().ToDebugString(), token.m_numberOfBytesToAllocateForDecompressor);

	if (!m_archiveBlobView.Empty() && token.m_numberOfBytesToAllocateForDecompressor == 0)
	{
		RED_FATAL_ASSERT(token.m_offset <= std::numeric_limits<Uint32>::max());

		const Uint32 archiveOffset = static_cast<Uint32>(token.m_offset);
		const void* archiveData = m_archiveBlobView.Data(archiveOffset);
		const Uint32 archiveDataSize = token.m_numberOfBytesToRead;

		io::ShareableIOMemory ioMemory;
		if (token.m_buffer)
		{
			red::Memcpy(token.m_buffer, archiveData, archiveDataSize);

			// Don't set ioMemory, since other asserts check to make sure we don't "allocate" I/O memory if user memory was already supplied
		}
		else
		{
			auto buffer = red::MakeUniqueBuffer<serialization::helper::NullBufferProxy>(const_cast<void*>(archiveData), archiveDataSize, 1);

			// No really deserialization memory, but fake it transparently.
			ioMemory = io::ShareableIOMemory( std::move(buffer), archiveOffset );
			token.m_buffer = ioMemory.GetUnderlyingBuffer().Data();
		}

		if (token.m_callback)
		{
			auto callback = token.m_callback;
			callback(token, red::eAsyncResult_Success, token.m_numberOfBytesToRead, std::move(ioMemory), 0, red::UniqueBuffer());
		}
	}
	else
	{
		io::GAsyncIO.BeginRead( m_handle, token, priority );
	}
}

bool ArchiveAsyncSource::ReadInline( Uint32 offset, Uint32 size, const red::BlobSpan& readBuffer, io::EAsyncPriority priority, io::RequestSource requestSource, red::ManualResetEvent* optionalEvent ) const
{
	// TODO if actually needed, otherwise we'll just read from disk still
	RED_FATAL_ASSERT(m_archiveBlobView.Empty(),
		"Not implemented for ReadInline in this memory cached archive: %hs %hs",
		io::GAsyncIO.GetFileName(m_handle), Debug_GetResourcePath().ToDebugString());

	// Override if using GAME priority, because Bink can stall entire frame
	if (g_isBinkVideoOpen)
	{
		priority = io::eAsyncPriority_FULLSCREENVIDEO;
	}

	RED_FATAL_ASSERT( m_handle != io::INVALID_FILE_HANDLE );
	RED_FATAL_ASSERT( readBuffer.Data() != nullptr );
	RED_FATAL_ASSERT( size <= readBuffer.Size() );

	archive::FileSegment segment;
	Uint32 segmentOffset = 0;
	if ( !FindFileSegment( offset, segment, segmentOffset ) )
	{
		return false;
	}

	const Uint64 fileOffset = segment.position + segmentOffset;
	const Uint64 alignedFileOffset = helper::AlignReadPosition( fileOffset );
	RED_FATAL_ASSERT( fileOffset == alignedFileOffset, "Reading from an archive inline must read from an aligned position" );
	const Uint32 fileSize = helper::AlignReadSize( size );
	RED_FATAL_ASSERT( fileSize <= readBuffer.Size() );

	return helper::ReadFileInline( m_metadata.GetID().ToDebugString().AsChar(), m_handle, fileOffset, fileSize, readBuffer.Data(), priority, requestSource, optionalEvent );
}

bool ArchiveAsyncSource::FindFileSegment( Uint32 offset, archive::FileSegment& result, Uint32& segmentOffset ) const
{
	const auto& segments = m_metadata.GetSegments( m_table );

	// The first segment is the file's object data which is compressed during archiving
	// therefore the offsets for the buffers stored inside are relative to this uncompressed size
	const auto& firstSegment = segments.Front();
	if ( offset < firstSegment.sizeInMemory )
	{
		result = firstSegment;
		segmentOffset = offset;
		return true;
	}

	Uint32 fileOffset = firstSegment.sizeInMemory;
	for ( Uint32 index = 1; index < segments.Size(); ++index )
	{
		const auto& segment = segments[index];
		if ( fileOffset <= offset && offset < fileOffset + segment.sizeOnDisk )
		{
			result = segment;
			segmentOffset = offset - fileOffset;
			return true;
		}
		fileOffset += segment.sizeOnDisk;
	}

	return false;
}

Bool ArchiveAsyncSource::TryGetResourceDependencies(red::DynArray<res::ResourcePath>& outDependencies) const
{
	outDependencies.Reserve(m_metadata.GetResourceDependencies( m_table ).Size());
	for (Uint64 hash : m_metadata.GetResourceDependencies( m_table ))
	{
		outDependencies.PushBack(res::ResourcePath::Build(hash));
	}
	return true;
}

bool ArchiveAsyncSource::TryGetRawDiskPosition(RawDiskPosition& outPosition) const
{
	RED_FATAL_ASSERT(!m_metadata.GetSegments( m_table ).Empty());
	outPosition.fileHandle = m_handle;
	outPosition.offset = m_metadata.GetSegments( m_table )[0].position;
	return true;
}

//------------------------------------------------------------------------------

AsyncSourceUniquePtr CreateMemoryAsyncSource( red::UniqueBuffer&& buffer )
{
	return red::CreateUniquePtr<MemoryAsyncSource>( std::move( buffer ) );
}

AsyncSourceUniquePtr CreateMemoryAsyncSource( const BufferHandleRO& bufferHandle )
{
	return red::CreateUniquePtr<MemoryAsyncSource>( bufferHandle.GetView() );
}

AsyncSourceUniquePtr CreateMemoryAsyncSource( const red::BlobView& bufferView )
{
	return red::CreateUniquePtr<MemoryAsyncSource>( bufferView );
}

AsyncSourceUniquePtr CreateFileAsyncSource( const res::ResourcePath& resourcePathForDebug, io::TFileHandle fileHandle )
{
	return red::CreateUniquePtr< FileAsyncSource >( resourcePathForDebug, fileHandle );
}

AsyncSourceUniquePtr CreateArchiveAsyncSource(const red::BlobView& archiveBlobView, io::TFileHandle fileHandle, const archive::FileMetadata& metadata, const archive::FileTable& table)
{
	return red::CreateUniquePtr< ArchiveAsyncSource >( archiveBlobView, fileHandle, metadata, table );
}


} // serialization
