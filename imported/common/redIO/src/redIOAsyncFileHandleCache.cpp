/**
* Copyright (c) 2014 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"
#include "redIOAsyncFileHandleCache.h"
#include "redIOProfilerInterface.h"

#include "../../redSystem/include/redThreadsThread.h"
#include "../../redSystem/include/utility.h"
#include "../../redContainers/include/circularBuffer.h"
#include "../../redMemory/include/linearAllocator.h"
#include "serializationMemoryAllocator.h"

namespace io
{

AsyncFileHandleCache::~AsyncFileHandleCache() = default;

class MemoryAllocator;

//-----------------------------------------------------------------------------

class REDIO_API GameFileHandleCache : public AsyncFileHandleCache
{
	RED_USE_MEMORY_POOL( red::PoolEngine );
public:
	GameFileHandleCache();
	virtual ~GameFileHandleCache();

	virtual TFileHandle Open( const char* absoluteFilePath, Uint8 asyncFlags ) override;

	virtual void AddRef( TFileHandle handle ) override;
	virtual void Release( TFileHandle handle ) override;

	virtual AsyncFile* GetAsyncFile( TFileHandle handle ) override;
	virtual const char* GetFileName( TFileHandle handle ) const override;
	virtual Uint8 GetAsyncFlags( TFileHandle handle ) const override;
	virtual void SetMemoryAllocator(MemoryAllocator*) override;

private:
	static constexpr Uint32 c_maxPathLength = REDIO_MAX_PATH_LENGTH;
	static constexpr Uint32 c_maxEntries = REDIO_MAX_FILE_HANDLES;

	using FilePath = char[ c_maxPathLength ];

	struct FileEntry
	{
		AsyncFile* m_file = nullptr; // actual file
		red::Atomic<Int32> m_refCount;
		Uint8 m_asyncFlags{ 0 };
	};
	static_assert( sizeof( FileEntry ) == 16, "FileEntry size increased" );

	red::CircularBuffer<TFileHandle> m_idPool{ red::PoolEngine() };

	red::HashMap< red::String, TFileHandle > m_entriesMap;

	FileEntry m_entries[c_maxEntries];
	FilePath m_absolutePaths[c_maxEntries];

	mutable red::SpinLock m_lock;

	TFileHandle AllocateEntry_NoLock();
	void FreeEntry_NoLock( TFileHandle id );

	void OnFatalDoubleRelease_NoLock(TFileHandle id);
	void OnFatalDanglingHandle_NoLock(TFileHandle id);

	MemoryAllocator* m_allocator{ nullptr }; // not owned
};


GameFileHandleCache::GameFileHandleCache()
	: m_entriesMap( red::PoolEngine() )
{
	m_idPool.Reserve( c_maxEntries );
	for (Uint32 i = 0; i < c_maxEntries; ++i)
	{
		m_idPool.PushBack(i);
	}
}

GameFileHandleCache::~GameFileHandleCache()
{
	for ( Uint32 i = 0; i < c_maxEntries; ++i )
	{
		if (m_entries[i].m_refCount.GetValue() != 0)
		{
			OnFatalDanglingHandle_NoLock(i);
		}
		RED_DELETE( m_entries[i].m_file );
		m_entries[i].m_file = nullptr;
	}
}

TFileHandle GameFileHandleCache::Open( const char* absoluteFilePath, Uint8 asyncFlags )
{
	if ( !absoluteFilePath || !*absoluteFilePath )
	{
		return INVALID_FILE_HANDLE;
	}

	RED_SCOPE_LOCK( m_lock );

	TFileHandle id = INVALID_FILE_HANDLE;
	const auto iter = m_entriesMap.Find( absoluteFilePath );

	if ( iter != m_entriesMap.End() )
	{
		// entry already exists
		id = iter.Value();
		m_entries[ id ].m_refCount.Increment();
		return id;
	}

	// allocate new entry
	id = AllocateEntry_NoLock();
	ALWAYSENABLED_RED_FATAL_ASSERT( id != INVALID_FILE_HANDLE, "Exhausted the supply of file handles, too many open files!" );
	if ( id == INVALID_FILE_HANDLE )
	{
		return INVALID_FILE_HANDLE;
	}

	RED_FATAL_ASSERT(id < c_maxEntries, "Invalid ID %u", id);

	// bind to file
	String key = String::CreateExternal( m_absolutePaths[ id ], c_maxPathLength );
	key = absoluteFilePath;

	RED_VERIFY( m_entriesMap.Insert( std::move( key ), id ).IsSuccessful(), "Entry already exists" );

	// open the physical file
#ifdef RED_PROFILE_FILE_SYSTEM
	IIOProfiler::Get()->ProfileAsyncIOOpenFileStart( absoluteFilePath );
#endif
	AsyncFile* file = RED_NEW( AsyncFile );
	Uint8 openFlags = eOpenFlag_Read | eOpenFlag_Async;
	if ( asyncFlags & eAsyncFlag_Unbuffered )
	{
		openFlags |= eOpenFlag_Unbuffered;
	}

	if ( !file->Open( absoluteFilePath, openFlags ) )
	{
#ifdef RED_PROFILE_FILE_SYSTEM
		IIOProfiler::Get()->ProfileAsyncIOOpenFileEnd( 0 );
#endif
		RED_LOG_WARNING( "RedIO: AsyncFileHandleCache::Open: Failed to open file handle for '%hs'. Consider lowering the hardlimit to avoid hits to the filesystem", absoluteFilePath );
		RED_DELETE( file );

		FreeEntry_NoLock( id );

		return INVALID_FILE_HANDLE;
	}

	auto& entry = m_entries[ id ];
	RED_FATAL_ASSERT( entry.m_file == nullptr, "Entry is already in use" );
	RED_FATAL_ASSERT( entry.m_refCount.GetValue() == 0, "Entry is already in use" );

	// create entry
	entry.m_refCount.SetValue(1);
	entry.m_file = file;
	entry.m_asyncFlags = asyncFlags;

#ifdef RED_PROFILE_FILE_SYSTEM
	IIOProfiler::Get()->ProfileAsyncIOOpenFileEnd( file->GetFileID() );
#endif

	return id;
}

void GameFileHandleCache::AddRef( TFileHandle handle )
{
	if ( handle != INVALID_FILE_HANDLE )
	{
		RED_FATAL_ASSERT( handle < c_maxEntries, "Invalid file handle %u", handle );
		auto& entry = m_entries[ handle ];
		entry.m_refCount.Increment();
	}
}

void GameFileHandleCache::Release( TFileHandle handle )
{
	if ( handle != INVALID_FILE_HANDLE )
	{
		RED_FATAL_ASSERT( handle < c_maxEntries, "Invalid file handle %u", handle );
		auto& entry = m_entries[ handle ];

		RED_SCOPE_LOCK( m_lock );
		const Int32 newCount = entry.m_refCount.Decrement();

		if (newCount < 0)
		{
			OnFatalDoubleRelease_NoLock(handle);
		}

		if ( newCount == 0 )
		{
			RED_DELETE( entry.m_file );
			entry.m_file = nullptr;

			FreeEntry_NoLock( handle );
		}
	}
}

AsyncFile* GameFileHandleCache::GetAsyncFile( TFileHandle handle )
{
	if ( handle == INVALID_FILE_HANDLE )
		return nullptr;

	RED_FATAL_ASSERT( handle < c_maxEntries, "Invalid file handle %u", handle );
	auto& entry = m_entries[ handle ];

	return entry.m_file;
}

const char* GameFileHandleCache::GetFileName( TFileHandle handle ) const
{
	if ( handle == INVALID_FILE_HANDLE )
		return nullptr;

	RED_FATAL_ASSERT( handle < c_maxEntries, "Invalid file handle %u", handle );
	auto& entry = m_entries[ handle ];

	return m_absolutePaths[ handle ];
}

Uint8 GameFileHandleCache::GetAsyncFlags( TFileHandle handle ) const
{
	if ( handle == INVALID_FILE_HANDLE )
		return 0;

	RED_FATAL_ASSERT( handle < c_maxEntries, "Invalid file handle %u", handle );
	auto& entry = m_entries[ handle ];

	return entry.m_asyncFlags;
}

void GameFileHandleCache::SetMemoryAllocator(MemoryAllocator* allocator)
{
	m_allocator = allocator;
}

TFileHandle GameFileHandleCache::AllocateEntry_NoLock()
{
	if (m_idPool.Empty())
	{
		return INVALID_FILE_HANDLE;
	}

	const TFileHandle handle = m_idPool.Front();
	m_idPool.PopFront();

	return handle;
}

void GameFileHandleCache::FreeEntry_NoLock( TFileHandle handle )
{
	auto& entry = m_entries[ handle ];

	const auto key = m_absolutePaths[ handle ];
	RED_VERIFY( m_entriesMap.Remove( key ).IsSuccessful(), "Entry is expected to be used" );

	m_idPool.PushBack(handle);

	if (m_allocator)
	{
		m_allocator->InvalidateCacheForFileHandle(handle);
	}
}

RED_NOINLINE void GameFileHandleCache::OnFatalDoubleRelease_NoLock( TFileHandle id )
{
#ifdef RED_ASSERTS_ENABLED
	// Copy out just in case
	char buf[REDIO_MAX_PATH_LENGTH] = "";
	red::Strcpy(buf, m_absolutePaths[id], RED_ARRAY_COUNT_U32(buf));
	RED_FATAL("Double release on handle %u. Last known path name '%hs'!", id, buf);
#endif
}

void GameFileHandleCache::OnFatalDanglingHandle_NoLock( TFileHandle id )
{
#ifdef RED_ASSERTS_ENABLED
	// Copy out just in case
	char buf[REDIO_MAX_PATH_LENGTH] = "";
	red::Strcpy(buf, m_absolutePaths[id], RED_ARRAY_COUNT_U32(buf));
	RED_FATAL("Dangling handle %u on shutdown. Last known path name '%hs'!", id, buf);
#endif
}


//-----------------------------------------------------------------------------

class REDIO_API EditorFileHandleCache : public AsyncFileHandleCache
{
	RED_USE_MEMORY_POOL( red::PoolEngine );
public:
	EditorFileHandleCache();
	virtual ~EditorFileHandleCache();

	virtual TFileHandle Open( const char* absoluteFilePath, Uint8 asyncFlags ) override;

	virtual void AddRef( TFileHandle handle ) override;
	virtual void Release( TFileHandle handle ) override;

	virtual AsyncFile* GetAsyncFile( TFileHandle handle ) override;
	virtual const char* GetFileName( TFileHandle handle ) const override;
	virtual Uint8 GetAsyncFlags( TFileHandle handle ) const override;

	virtual void SetMemoryAllocator(MemoryAllocator* allocator) override;

private:
	struct FileEntry
	{
		red::Atomic< Int32 > m_refCount;
		AsyncFile m_file;
		red::String m_path;
		Uint8 m_asyncFlags{ 0 };
	};

	red::HashMap< red::String, TFileHandle > m_entriesMap;

	// Circular buffer for recycling ids
	red::CircularBuffer< TFileHandle > m_idPool{ red::PoolEngine() };

	red::DynArray< FileEntry* > m_entries;

	mutable red::SpinLock m_lock;

	// Note: just a single lock for all deferred opens. Could have a lock per file, but storage overkill
	// currently since only used in the editor and Win32 async IO is on a single thread.
	// More defending against somebody trying to get the file size while async I/O is in progress.
	mutable red::RWSpinLock m_deferredOpenRWLock;

	bool ProcessDeferredOpen( FileEntry& entry );

	TFileHandle AllocateEntry_NoLock();
	void FreeEntry_NoLock( TFileHandle id );

	void OnFatalDoubleRelease_NoLock(TFileHandle id);
	void OnFatalDanglingHandle_NoLock(TFileHandle id);
};


EditorFileHandleCache::EditorFileHandleCache()
	: m_entriesMap( red::PoolEngine() )
	, m_entries( red::PoolEngine() )
{
	m_entries.Reserve( REDIO_MAX_FILE_HANDLES );
}

EditorFileHandleCache::~EditorFileHandleCache()
{
}

TFileHandle EditorFileHandleCache::Open( const char* absoluteFilePath, Uint8 asyncFlags )
{
	if ( !absoluteFilePath || !*absoluteFilePath )
	{
		return INVALID_FILE_HANDLE;
	}

	RED_SCOPE_LOCK( m_lock );

	TFileHandle id = INVALID_FILE_HANDLE;
	const auto iter = m_entriesMap.Find( absoluteFilePath );
	if ( iter != m_entriesMap.End() )
	{
		// entry already exists
		id = iter.Value();
		auto* entry = m_entries[ id ];
		RED_FATAL_ASSERT( entry != nullptr );
		entry->m_refCount.Increment();
		return id;
	}

	// allocate new entry
	id = AllocateEntry_NoLock();
	RED_FATAL_ASSERT( id != INVALID_FILE_HANDLE, "Exhausted the supply of file handles, too many open files!" );
	if ( id == INVALID_FILE_HANDLE )
	{
		RED_LOG_ERROR( "RedIO: Failed to allocate file handle for '%hs'. To many in-flight file handles", absoluteFilePath );
		return id;
	}

	RED_FATAL_ASSERT( id < m_entries.Size(), "Invalid ID %u", id );

	auto& entry = m_entries[ id ];
	RED_FATAL_ASSERT( !entry->m_file.IsValid(), "Entry is already in use" );
	RED_FATAL_ASSERT( entry->m_refCount.GetValue() == 0, "Entry is already in use" );

	entry->m_path = absoluteFilePath;

	RED_VERIFY( m_entriesMap.Insert( entry->m_path, id ).IsSuccessful(), "Entry already exists" );

	const Bool isDeferredOpen = ( asyncFlags & eAsyncFlag_DeferredOpen ) != 0;

	// open the physical file
#ifdef RED_PROFILE_FILE_SYSTEM
	IIOProfiler::Get()->ProfileAsyncIOOpenFileStart( absoluteFilePath );
#endif
	if ( !isDeferredOpen )
	{
		Uint8 openFlags = eOpenFlag_Read | eOpenFlag_Async;
		if ( asyncFlags & eAsyncFlag_Unbuffered )
		{
			openFlags |= eOpenFlag_Unbuffered;
		}

		if ( !entry->m_file.Open( absoluteFilePath, openFlags ) )
		{
#ifdef RED_PROFILE_FILE_SYSTEM
			IIOProfiler::Get()->ProfileAsyncIOOpenFileEnd( 0 );
#endif
			RED_LOG_WARNING( "RedIO: AsyncFileHandleCache::Open: Failed to open file handle for '%hs'.", absoluteFilePath );

			FreeEntry_NoLock( id );

			return INVALID_FILE_HANDLE;
		}
	}


	// create entry
	entry->m_refCount.SetValue(1);
	entry->m_asyncFlags = asyncFlags;

#ifdef RED_PROFILE_FILE_SYSTEM
	IIOProfiler::Get()->ProfileAsyncIOOpenFileEnd( id );
#endif

	return id;
}

void EditorFileHandleCache::AddRef( TFileHandle handle )
{
	if ( handle != INVALID_FILE_HANDLE )
	{
		RED_FATAL_ASSERT( handle < m_entries.Size(), "Invalid file handle %u", handle );
		auto* entry = m_entries[ handle ];
		RED_FATAL_ASSERT( entry != nullptr );
		entry->m_refCount.Increment();
	}
}

void EditorFileHandleCache::Release( TFileHandle handle )
{
	if ( handle != INVALID_FILE_HANDLE )
	{
		RED_FATAL_ASSERT( handle < m_entries.Size(), "Invalid file handle %u", handle );
		auto* entry = m_entries[ handle ];
		RED_FATAL_ASSERT( entry != nullptr );

		RED_SCOPE_LOCK( m_lock );
		const Int32 newCount = entry->m_refCount.Decrement();
		if ( newCount < 0 )
		{
			OnFatalDoubleRelease_NoLock(handle);
		}

		if ( newCount == 0 )
		{
			entry->m_file.Close();
			FreeEntry_NoLock( handle );
			entry->m_path.Clear();
		}
	}
}

AsyncFile* EditorFileHandleCache::GetAsyncFile( TFileHandle handle )
{
	if ( handle != INVALID_FILE_HANDLE )
	{
		RED_FATAL_ASSERT( handle < m_entries.Size(), "Invalid file handle %u", handle );
		auto* entry = m_entries[ handle ];
		RED_FATAL_ASSERT( entry != nullptr );

		if ( ( entry->m_asyncFlags & eAsyncFlag_DeferredOpen ) != 0 )
		{
			if ( !ProcessDeferredOpen( *entry ) )
			{
				return nullptr;
			}
		}

		return &entry->m_file;
	}
	return nullptr;
}

const char* EditorFileHandleCache::GetFileName( TFileHandle handle ) const
{
	if ( handle != INVALID_FILE_HANDLE )
	{
		RED_FATAL_ASSERT( handle < m_entries.Size(), "Invalid file handle %u", handle );
		auto* entry = m_entries[ handle ];
		RED_FATAL_ASSERT( entry != nullptr );
		return entry->m_path.AsChar();
	}
	return nullptr;
}

Uint8 EditorFileHandleCache::GetAsyncFlags( TFileHandle handle ) const
{
	if ( handle != INVALID_FILE_HANDLE )
	{
		RED_FATAL_ASSERT( handle < m_entries.Size(), "Invalid file handle %u", handle );
		auto* entry = m_entries[ handle ];
		RED_FATAL_ASSERT( entry != nullptr );
		return entry->m_asyncFlags;
	}
	return 0;
}

void EditorFileHandleCache::SetMemoryAllocator(MemoryAllocator*)
{
	// No-op
}

bool EditorFileHandleCache::ProcessDeferredOpen( FileEntry& entry )
{
	RED_FATAL_ASSERT( ( entry.m_asyncFlags & eAsyncFlag_DeferredOpen ) != 0 );

	Bool tryOpen = false;
	{
		RED_SCOPE_SHARED_LOCK( m_deferredOpenRWLock );
		if ( !entry.m_file.IsValid() )
		{
			tryOpen = true;
		}
	}

	if ( tryOpen )
	{
		Uint8 openFlags = eOpenFlag_Read | eOpenFlag_Async;
		if ( entry.m_asyncFlags & eAsyncFlag_Unbuffered )
		{
			openFlags |= eOpenFlag_Unbuffered;
		}
		RED_SCOPE_LOCK( m_deferredOpenRWLock );
		if ( !entry.m_file.IsValid() && !entry.m_file.Open( entry.m_path.AsChar(), openFlags ) )
		{
#ifdef RED_PROFILE_FILE_SYSTEM
			IIOProfiler::Get()->ProfileAsyncIOOpenFileEnd( 0 );
#endif
			RED_LOG_WARNING( "RedIO: AsyncFileHandleCache::Open: Failed to open deferred file handle for '%hs'", entry.m_path.AsChar() );
		}
	}

	return entry.m_file.IsValid();
}

TFileHandle EditorFileHandleCache::AllocateEntry_NoLock()
{
	if ( m_idPool.Empty() )
	{
		const TFileHandle handle = m_entries.Size();
		if ( handle >= m_entries.Capacity() )
		{
			m_entries.Reserve( m_entries.Capacity() * 2 );
		}
		m_entries.PushBack( RED_NEW( FileEntry, red::PoolEngine ) );
		return handle;
	}
	else
	{
		const TFileHandle handle = m_idPool.Front();
		m_idPool.PopFront();
		return handle;
	}
}

void EditorFileHandleCache::FreeEntry_NoLock( TFileHandle handle )
{
	RED_FATAL_ASSERT( handle < m_entries.Size(), "Invalid file handle %u", handle );
	auto* entry = m_entries[ handle ];
	RED_FATAL_ASSERT( entry != nullptr );

	RED_VERIFY( m_entriesMap.Remove( entry->m_path ).IsSuccessful(), "Entry is expected to be used" );
	m_idPool.PushBack( handle );
}

RED_NOINLINE void EditorFileHandleCache::OnFatalDoubleRelease_NoLock( TFileHandle id )
{
#ifdef RED_ASSERTS_ENABLED
	auto* entry = m_entries[ id ];
	RED_FATAL_ASSERT( entry != nullptr );

	// Copy out just in case
	char buf[REDIO_MAX_PATH_LENGTH] = "";
	red::Strcpy( buf, entry->m_path.AsChar(), RED_ARRAY_COUNT_U32( buf ) );
	RED_FATAL( "Double release on handle %u. Last known path name '%hs'!", id, buf );
#endif
}

void EditorFileHandleCache::OnFatalDanglingHandle_NoLock(TFileHandle id)
{
#ifdef RED_ASSERTS_ENABLED
	auto* entry = m_entries[ id ];
	RED_FATAL_ASSERT( entry != nullptr );

	// Copy out just in case
	char buf[REDIO_MAX_PATH_LENGTH] = "";
	red::Strcpy( buf, entry->m_path.AsChar(), RED_ARRAY_COUNT_U32( buf ) );
	RED_FATAL( "Dangling handle %u on shutdown. Last known path name '%hs'!", id, buf );
#endif
}



//------------------------------------------------------------------------------

red::UniquePtr< AsyncFileHandleCache > AsyncFileHandleCache::Create()
{
#if defined( RED_PLATFORM_WINPC ) && !defined( NO_EDITOR )
	return red::CreateUniquePtr< EditorFileHandleCache >();
#else
	return red::CreateUniquePtr< GameFileHandleCache >();
#endif
}

} // io
