/**
* Copyright (c) 2013 CD Projekt Red. All Rights Reserved.
*/
#ifndef RED_IO_COMMON_H
#define RED_IO_COMMON_H
#pragma once

#include "../../redMemory/include/sharedPtr.h"
#include "../../redMemory/include/uniqueBuffer.h"

namespace io
{

//////////////////////////////////////////////////////////////////////////
// Globals.
//////////////////////////////////////////////////////////////////////////
// Don't increase these unless you know the implementation details. The
// underlying OS has certain resource limits which these values were
// chosen for.
#ifdef RED_PLATFORM_WINPC
	const Uint32 REDIO_MAX_PATH_LENGTH	= 512;
#else
	const Uint32 REDIO_MAX_PATH_LENGTH = 256;
#endif

const Uint32 REDIO_MAX_ASYNC_OPS	= 64;

#if defined( RED_PLATFORM_WINPC )
	// NOTE: The number is so high to support running final with cooked data
	// TODO: Reduce this when shipping, can be as low as the other platforms
	const Uint32 REDIO_MAX_FILE_HANDLES = 65536;
#elif defined( RED_PLATFORM_LINUX )
	const Uint32 REDIO_MAX_FILE_HANDLES = 1024;
#elif defined( RED_PLATFORM_CONSOLE )
	const Uint32 REDIO_MAX_FILE_HANDLES = 192;
#else
#	error Unsupported platform
#endif

const Uint32 REDIO_MAX_DIR_HANDLES	= 256;

typedef Uint32 TFileHandle;
const TFileHandle INVALID_FILE_HANDLE = 0xFFFFFFFF;

struct BufferSlice
{
	Int64	m_offset;
	Uint32	m_size;
};

struct InitSetup
{
	Bool enableProfiler{ false };
};

//////////////////////////////////////////////////////////////////////////
// EOpenFlag
//////////////////////////////////////////////////////////////////////////
enum EOpenFlag
{
	eOpenFlag_Read					= RED_FLAG(0),
	eOpenFlag_Write					= RED_FLAG(1),
	eOpenFlag_Append				= RED_FLAG(2),
	eOpenFlag_Create				= RED_FLAG(3),
	eOpenFlag_Truncate				= RED_FLAG(4),
	eOpenFlag_Async					= RED_FLAG(5),
	eOpenFlag_Unbuffered			= RED_FLAG(6),
	eOpenFlag_ReadWrite				= eOpenFlag_Read | eOpenFlag_Write,
	eOpenFlag_ReadWriteNew			= eOpenFlag_ReadWrite | eOpenFlag_Create | eOpenFlag_Truncate,
	eOpenFlag_WriteNew				= eOpenFlag_Write | eOpenFlag_Create | eOpenFlag_Truncate,
};

//////////////////////////////////////////////////////////////////////////
// EAsyncFlag
//////////////////////////////////////////////////////////////////////////
enum EAsyncFlag : Uint8
{
	eAsyncFlag_None							= 0,
	eAsyncFlag_TryCloseFileWhenNotUsed		= RED_FLAG(0),
	eAsyncFlag_DeferredOpen					= RED_FLAG(1),
	eAsyncFlag_Unbuffered					= RED_FLAG(2),
};

//////////////////////////////////////////////////////////////////////////
// EAsyncPriority
//////////////////////////////////////////////////////////////////////////
enum EAsyncPriority : Uint8
{
	eAsyncPriority_GAME,
	eAsyncPriority_Background = eAsyncPriority_GAME,
	eAsyncPriority_Streaming = eAsyncPriority_GAME,
	eAsyncPriority_Normal = eAsyncPriority_GAME,
	eAsyncPriority_AboveNormal = eAsyncPriority_GAME,
	eAsyncPriority_High = eAsyncPriority_GAME,

	eAsyncPriority_UI,

    eAsyncPriority_AUDIO,
    
	eAsyncPriority_FULLSCREENVIDEO,

	eAsyncPriority_COUNT,

    eAsyncPriority_RESERVED_CRITICAL = 0xf0,
    eAsyncPriority_GAMEPLAY_CRITICAL = 0xfb,
    eAsyncPriority_AUDIO_CRITICAL = 0xfc,
    eAsyncPriority_COLLISION_CRITICAL = 0xfd,
	eAsyncPriority_INVALID = 0xff,
	eAsyncPriority_DEFAULT = eAsyncPriority_GAME,
};

static const char* const c_asyncPriorityNames[] = {"GAME", "UI", "AUDIO", "VIDEO"};
RED_STATIC_ASSERT( RED_ARRAY_COUNT( c_asyncPriorityNames ) == eAsyncPriority_COUNT );

//////////////////////////////////////////////////////////////////////////
// ESeekOrigin
//////////////////////////////////////////////////////////////////////////
enum ESeekOrigin
{
	eSeekOrigin_Set			= 0,
	eSeekOrigin_Current		= 1,
	eSeekOrigin_End			= 2,
};

class ShareableIOMemory
{
public:
	explicit ShareableIOMemory(red::UniqueBuffer&& buffer, Uint64 absoluteFileOffset = c_invalidOffset)
		: m_buffer(red::CreateSharedPtr<red::UniqueBuffer>(std::move(buffer)))
		, m_baseFileOffset(absoluteFileOffset)
	{
		RED_FATAL_ASSERT(m_buffer);
	}

	ShareableIOMemory()
		: m_buffer()
		, m_baseFileOffset(c_invalidOffset)
	{
	}

	ShareableIOMemory(ShareableIOMemory&& other, Uint64 absoluteFileOffset)
		: m_buffer(std::move(other.m_buffer))
		, m_baseFileOffset(absoluteFileOffset)
	{
	}

	ShareableIOMemory(const ShareableIOMemory& other) = default;
	
	ShareableIOMemory& operator=(const ShareableIOMemory& rhs) = default;

	ShareableIOMemory(ShareableIOMemory&& other)
		: m_buffer(std::move(other.m_buffer))
		, m_baseFileOffset(other.m_baseFileOffset)
	{
		other.m_baseFileOffset = c_invalidOffset;
	}

	ShareableIOMemory& operator=(ShareableIOMemory&& rhs)
	{
		if (this != &rhs)
		{
			m_buffer = std::move(rhs.m_buffer);
			m_baseFileOffset = rhs.m_baseFileOffset;
			rhs.m_baseFileOffset = c_invalidOffset;
		}

		return *this;
	}

	const void* Data() const
	{
		return m_buffer ? m_buffer->Data() : nullptr;
	}

	void* Data()
	{
		return m_buffer ? m_buffer->Data() : nullptr;
	}

	void* Get()
	{
		return m_buffer ? m_buffer->Data() : nullptr;
	}

	Uint32 GetSize() const
	{
		return m_buffer ? m_buffer->Size() : 0;
	}

	red::UniqueBuffer& GetUnderlyingBuffer()
	{
		RED_FATAL_ASSERT(m_buffer);
		return *m_buffer;
	}

	const red::UniqueBuffer& GetUnderlyingBuffer() const
	{
		RED_FATAL_ASSERT(m_buffer);
		return *m_buffer;
	}

	void Reset()
	{
		m_baseFileOffset = c_invalidOffset;
		m_buffer.Reset();
	}

	red::SharedPtr< red::UniqueBuffer > Release()
	{
		m_baseFileOffset = c_invalidOffset;
		return std::move(m_buffer);
	}

	explicit operator Bool() const
	{
		return m_buffer && *m_buffer;
	}

	red::UniqueBuffer ExtractAsNonShared()
	{
		RED_FATAL_ASSERT(m_buffer);
		//RED_FATAL_ASSERT(m_bufffer->GetPool()->GetHandle() != io::MemoryAllocator::GetMemoryPool(), "You can't guarantee this isn't shared");
		m_baseFileOffset = c_invalidOffset;
		return std::move(*m_buffer);
	}

	static const Uint64 c_invalidOffset = UINT64_MAX;

	Uint64 GetBaseFileOffset() const
	{
		RED_FATAL_ASSERT(m_baseFileOffset != c_invalidOffset, "Offset is valid after going through the async I/O system");
		return m_baseFileOffset;
	}

private:
	red::SharedPtr< red::UniqueBuffer > m_buffer;
	
	// Absolute offset of the data in the file that the buffer was filled from; set to a valid value once succesfully gone through the I/O system
	// This is mainly used for reading "inline" data buffers to see if this m_buffer also contains their data or not
	// NOTE: this doesn't contain the file handle, so only valid to use if you know it's the same file, which can be the case with archives.
	Uint64 m_baseFileOffset;
};

} // io

#endif // RED_IO_COMMON_H