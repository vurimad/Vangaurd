/*
 * Copyright (c) 2026 RED Vanguard, All Rights Reserved.
 */
#include "build.h"
#include "serializationAsyncSource.h"

namespace serialization
{

IAsyncSource::~IAsyncSource() = default;

res::ResourcePath IAsyncSource::Debug_GetResourcePath() const
{
	return {};
}

bool IAsyncSource::RequiresHeaderPreParseStep() const
{
	return false;
}

Bool IAsyncSource::TryGetMemorySource(red::BlobView& outView) const
{
	outView = red::BlobView();
	return false;
}

Bool IAsyncSource::TryGetResourceDependencies(red::DynArray<res::ResourcePath>& outDependencies) const
{
	outDependencies.Clear();
	return false;
}

Bool IAsyncSource::TryGetMemorySourceFromInlineData(const ReadAsyncParams&, const AsyncSourceReadBuffer&, red::BlobView& outView) const
{
	outView = red::BlobView();
	return false;
}

bool IAsyncSource::TryGetRawDiskPosition(RawDiskPosition& outPosition) const
{
	return false;
}

AsyncSourceReadSizes::AsyncSourceReadSizes()
	: memorySize(0)
	, diskSize(0)
	, bufferOffset(0)
	, bufferSize(0)
	, isOverReadForInlineBuffers( false )
{}

void AsyncSourceReadSizes::Clear()
{
	memorySize = diskSize = bufferOffset = bufferSize = 0;
}

bool AsyncSourceReadSizes::IsCompressed() const
{
	return diskSize < memorySize;
}

bool AsyncSourceReadSizes::IsPadded() const
{
	return bufferSize != diskSize;
}

bool AsyncSourceReadSizes::IsValid() const
{
	return diskSize != 0 && memorySize != 0 && bufferSize >= diskSize;
}

bool AsyncSourceReadSizes::IsRawFile() const
{
	return !IsCompressed() && !IsPadded();
}

AsyncSourceReadBuffer::AsyncSourceReadBuffer()
	: m_shareableIOMemoryOffsetForDebug(0)
{
}

AsyncSourceReadBuffer::AsyncSourceReadBuffer(const io::ShareableIOMemory& memory, Uint32 shareableIOMemoryOffset)
	: m_shareableMemory(memory)
	, m_shareableIOMemoryOffsetForDebug(shareableIOMemoryOffset)
{
	if (m_shareableMemory.Data())
	{
		m_validRegion = red::MakeBlobView(m_shareableMemory).Range(shareableIOMemoryOffset);
	}
}

AsyncSourceReadBuffer::AsyncSourceReadBuffer(const io::ShareableIOMemory& memory, Uint32 shareableIOMemoryOffset, const AsyncSourceReadSizes& readSizes)
	: m_shareableMemory(std::move(memory))
	, m_readSizes(readSizes)
	, m_shareableIOMemoryOffsetForDebug(shareableIOMemoryOffset)
{
	if (m_shareableMemory.Data())
	{
		m_validRegion = red::MakeBlobView(m_shareableMemory).Range(shareableIOMemoryOffset).Range(m_readSizes.bufferOffset, m_readSizes.diskSize);
	}
}

AsyncSourceReadBuffer::AsyncSourceReadBuffer(const red::BlobView& bufferView)
	: m_validRegion(bufferView)
	, m_shareableIOMemoryOffsetForDebug(0)
{
}

AsyncSourceReadBuffer::AsyncSourceReadBuffer(const AsyncSourceReadSizes& readSizes)
	: m_readSizes(readSizes)
	, m_shareableIOMemoryOffsetForDebug(0)
{
}

AsyncSourceReadBuffer::AsyncSourceReadBuffer(AsyncSourceReadBuffer&& other)
	: m_shareableMemory(std::move(other.m_shareableMemory))
	, m_validRegion(std::move(other.m_validRegion))
	, m_readSizes(std::move(other.m_readSizes))
	, m_shareableIOMemoryOffsetForDebug(std::move(other.m_shareableIOMemoryOffsetForDebug))
{
	other.m_shareableMemory = io::ShareableIOMemory();
	other.m_validRegion = red::BlobView();
	other.m_readSizes = AsyncSourceReadSizes();
	other.m_shareableIOMemoryOffsetForDebug = 0;
}

AsyncSourceReadBuffer& AsyncSourceReadBuffer::operator=(AsyncSourceReadBuffer&& other)
{
	if (this != &other)
	{
		m_shareableMemory = std::move(other.m_shareableMemory);
		m_validRegion = std::move(other.m_validRegion);
		m_readSizes = std::move(other.m_readSizes);
		m_shareableIOMemoryOffsetForDebug = std::move(other.m_shareableIOMemoryOffsetForDebug);

		other.m_shareableMemory = io::ShareableIOMemory();
		other.m_validRegion = red::BlobView();
		other.m_readSizes = AsyncSourceReadSizes();
		other.m_shareableIOMemoryOffsetForDebug = 0;
	}

	return *this;
}

bool AsyncSourceReadBuffer::IsEmpty() const
{
	return m_validRegion.Empty();
}

const red::BlobView& AsyncSourceReadBuffer::ValidRegion() const
{
	return m_validRegion;
}

io::ShareableIOMemory AsyncSourceReadBuffer::Release()
{
	m_validRegion = red::BlobView();
	return std::move(m_shareableMemory);
}

void AsyncSourceReadBuffer::Reset()
{
	m_validRegion = red::BlobView();
	m_shareableMemory.Reset();
}

Bool AsyncSourceReadBuffer::TryCreateValidRegion(Uint64 absoluteBufferOffset, Uint32 size, red::BlobView& outView) const
{
	outView = red::BlobView();

	if (!m_shareableMemory)
	{
		// Can't know how to adjust the view
		return false;
	}

	const Uint64 baseOffset = m_shareableMemory.GetBaseFileOffset();
	if (absoluteBufferOffset < baseOffset)
	{
		// Buffer starts before the available data
		return false;
	}

	if (absoluteBufferOffset + size > baseOffset + m_shareableMemory.GetSize())
	{
		// Buffer would exceed available data
		return false;
	}

	const Uint64 bufferDiff = absoluteBufferOffset - baseOffset;
	RED_FATAL_ASSERT(bufferDiff <= UINT32_MAX, "bufferDiff: %llu", bufferDiff);
	outView = red::MakeBlobView(m_shareableMemory).Range((Uint32)bufferDiff, size);
	return true;
}

} // namespace serialization
