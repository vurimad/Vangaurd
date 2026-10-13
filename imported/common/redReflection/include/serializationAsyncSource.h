/**
* Copyright (c) 2026 RED Vanguard, All Rights Reserved.
*/
#pragma once

#include "../../redJobs/include/jobValue.h"
#include "../../redIO/include/redIOCommon.h"
#include "../../redMemory/include/uniqueBuffer.h"
#include "../../redContainers/include/blob.h"
#include "reflectionPool.h"
#include "resourcePath.h"

namespace job
{
class Builder;
} // job

namespace compression
{
enum ECompressionType : Uint8;
}

namespace archive
{
	class FileMetadata;
}

namespace serialization
{

// Information on how to read the data from disk and into memory
struct RED_REFLECTION_API AsyncSourceReadSizes
{
	Uint32 memorySize;		// Size of the data in memory (possibly after decompression), set to 0 if the source does not know
	Uint32 diskSize;		// Size of the data on disk
	Uint32 bufferOffset;	// Offset of the data in the buffer that gets read in
	Uint32 bufferSize;		// Size of the buffer to allocate to read the data into
	Bool isOverReadForInlineBuffers;

	AsyncSourceReadSizes();
	void Clear();
	bool IsCompressed() const;
	bool IsPadded() const;
	bool IsValid() const;
	bool IsRawFile() const;
};

struct RED_REFLECTION_API RawDiskPosition
{
	io::TFileHandle fileHandle{ io::INVALID_FILE_HANDLE };
	Uint64 offset{ 0 };

	Bool operator==(const RawDiskPosition& rhs) const
	{
		return fileHandle == rhs.fileHandle && offset == rhs.offset;
	}
};

// Abstraction over a UniqueBuffer so that we can allocate a larger padded buffer for doing the 
// actual reading from disk into as well as knowing where inside that buffer is the data we have requested
struct RED_REFLECTION_API AsyncSourceReadBuffer
{
public:
	AsyncSourceReadBuffer();
	AsyncSourceReadBuffer(const io::ShareableIOMemory& memory, Uint32 shareableIOMemoryOffset);
	explicit AsyncSourceReadBuffer(const AsyncSourceReadSizes& readSizes); // not allocated, but readSizes for intended different buffer that will be created later
	explicit AsyncSourceReadBuffer(const red::BlobView& unownedBufferView); // not owned, CAREFUL. Underlying memory must be kept alive.
	AsyncSourceReadBuffer(const io::ShareableIOMemory& memory, Uint32 shareableIOMemoryOffset, const AsyncSourceReadSizes& readSizes);
	AsyncSourceReadBuffer(AsyncSourceReadBuffer&& other);
	AsyncSourceReadBuffer& operator=(AsyncSourceReadBuffer&& other);

	bool IsEmpty() const;

	// View of the valid region of memory for the read 
	// This is referencing a portion of or the entire underlying owned memory buffer
	const red::BlobView& ValidRegion() const;

	io::ShareableIOMemory Release();

	void Reset();

	const AsyncSourceReadSizes& GetReadSizes() const { return m_readSizes; }

	Bool TryCreateValidRegion(Uint64 absoluteBufferOffset, Uint32 size, red::BlobView& outView) const;

	const io::ShareableIOMemory& GetShareableIOMemory() const { return m_shareableMemory; }
	io::ShareableIOMemory& GetShareableIOMemory() { return m_shareableMemory; }

private:
	io::ShareableIOMemory m_shareableMemory;
	red::BlobView m_validRegion;
	AsyncSourceReadSizes m_readSizes;
	Uint32 m_shareableIOMemoryOffsetForDebug;
};

// Generalized asynchronous data source for the serialization system, anything that can create a job to access the data is fine
class RED_REFLECTION_API IAsyncSource
{
	RED_USE_MEMORY_POOL( red::PoolResource );
public:
	virtual ~IAsyncSource() = 0;

	// Functions for debugging
	virtual String Debug_GetDescription() const = 0;
	virtual res::ResourcePath Debug_GetResourcePath() const;

	// Get the size of the serialised object portion of the resource on disk
	// If the source is unpacked this will return the total size of the resource on disk
	// If the source is packed (within an archive or another file) then this will return the size of the object data on disk
	virtual Uint32 GetSizeOnDisk() const = 0;

	// Returns true if this Async Source represents data that requires a separate header loading and 
	// parsing step inside of the serialization::BinaryLoader class.
	// Pre-processed resources such as in archives and from within entities do not need this step as the
	// sizes of the data are all that needs to be loaded
	virtual bool RequiresHeaderPreParseStep() const;

	// Gets the read info for the given offset and size
	// This must be called before doing any reading through ReadInline or ReadAsync methods
	// Returns an invalid structure if the offset + size are outside the valid range of this source
	virtual AsyncSourceReadSizes GetReadSizes( Uint32 offset, Uint32 size ) const = 0;

	virtual Bool TryGetMemorySource(red::BlobView& outView) const;

	virtual Bool TryGetResourceDependencies(red::DynArray<res::ResourcePath>& outDependencies) const;

	struct CallbackContext
	{
		using TCallbackFunc = void( AsyncSourceReadBuffer readBuffer, red::UniqueBuffer memoryForDecompressor, void* userData, red::EAsyncResult result );
		TCallbackFunc* callback{ nullptr };
		void* userData{ nullptr };
	};
	
	struct ReadAsyncParams
	{
		Uint32 offset{ 0 };
		Uint32 size{ 0 };
		io::EAsyncPriority priority{ io::eAsyncPriority_Background };
		Bool allocDeserializationMemoryForDecompressorIfCompressed{ true };
		Bool HACK_mightBeTerrainAndNeedsATonOfMemoryForBuffers{ false };
		red::SharedPtr< io::IOContext > ioContext{ nullptr };
		io::RequestSource requestSource{ io::RequestSource::UnknownAsyncSource };
	};

	virtual Bool TryGetMemorySourceFromInlineData(const ReadAsyncParams& params, const AsyncSourceReadBuffer& inlineData, red::BlobView& outView) const;

	// Reads data asynchronously and calls the callback when completed
	// The offset and size form the range of bytes that the calling function is actually interested in
	// The amount of data read in from disk will be equal to the bufferSize parameter returned by the GetReadSizes function above
	// The readBuffer should be at least bufferSize bytes in size
	virtual void ReadAsync( const ReadAsyncParams& params, AsyncSourceReadBuffer readBuffer, const CallbackContext& callbackContext ) const = 0;

	// Read the buffer synchronously
	// NOTE: for advanced/low level uses only!
	virtual bool ReadInline( Uint32 offset, Uint32 size, const red::BlobSpan& readBuffer, io::EAsyncPriority priority, io::RequestSource requestSource, red::ManualResetEvent* optionalWaitEvent ) const = 0;

	virtual bool TryGetRawDiskPosition(RawDiskPosition& outPosition) const;
};

typedef red::SharedPtr<IAsyncSource> AsyncSourcePtr;
typedef red::UniquePtr<IAsyncSource> AsyncSourceUniquePtr;

} // serialization
