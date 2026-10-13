/**
* Copyright (c) 2016-20 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"
#include "nameRegistry.h"
#include "names.h"
#include "../../redMemory/include/linearAllocator.h"
#include "../../redContainers/include/dynArray.h"
#include "../../redContainers/include/string/stringView.h"
#include "../../redSystem/include/readWriteSpinLock.h"
#include "../../redSystem/include/systemTypesFormatters.h"

namespace red
{
	using NameEntryIndex = Uint32;
	using NameEntryLength = Uint32;

	const Uint32 c_nameRegistryChunkSize = RED_KILO_BYTE(512);
	const Uint32 c_nameRegistryHashTableSize = 1 << 19;
	const NameEntryIndex c_nameRegistryHashTableIndexMax = ( 1 << 24 ) - 1;
	const NameEntryIndex c_invalidNameIndex = 0x00ff'ffff;

	static_assert( red::IsPowerOf2( c_nameRegistryHashTableSize ), "Name registry hash table size should be a power of two" );
	static_assert( c_invalidNameIndex >= c_nameRegistryHashTableIndexMax, "Name registry invalid name index must be >= maximum name index" );

	class NameRegistry : red::NonCopyable
	{
	public:
		NameRegistry();
		~NameRegistry();

		void Initialize();

		void RegisterName( CNameHash hash, StringView view );
		StringView GetNameString( CNameHash hash ) const;

	private:

#pragma pack(push, 4)
		struct Storage
		{
			CNameHash hash;
			Storage * next;
			NameEntryLength length; // : 8;
			NameEntryIndex index; // : 24;

			char* Data() { return reinterpret_cast< char* >( this ) + sizeof( Storage ); }
			const char* AsChar() const { return reinterpret_cast< const char* >( this ) + sizeof( Storage ); }
			NameEntryLength Length() const { return length; }
			red::StringView AsStringView() const { return{ AsChar(), length }; }
		};
#pragma pack(pop)

		static_assert( sizeof( Storage ) == 24, "Unexpected size of red::NameRegistry::Storage." );

		using StorageLockPrimitive = RWSpinLock;
		using AllocatorLockPrimitive = RWSpinLock;

		NameEntryIndex FindEntry( CNameHash hash ) const;
		void CreateEntry( CNameHash hash, StringView view );

		const Storage * GetStorage( Uint32 index ) const;
		Uint8 * AllocateStorageMemory( Uint32 size );
		void FreeStorageMemory( Uint8 * ptr );

		mutable StorageLockPrimitive m_lock;
		DynArray< const Storage* > m_storageContainer; // ctremblay maybe something else in final ?
		Storage * m_hashTable[ c_nameRegistryHashTableSize ];

		AllocatorLockPrimitive m_allocatorLock;
		memory::DynamicLinearAllocator m_storageAllocator;
	};

	NameRegistry::NameRegistry()
		: m_storageContainer( red::PoolDebug() )
		, m_hashTable{}
	{}

	NameRegistry::~NameRegistry() = default;

	void NameRegistry::Initialize()
	{
		const red::memory::DynamicLinearAllocatorParameter param =
		{
			&red::memory::AcquireSystemAllocator(),
			c_nameRegistryChunkSize,
			red::memory::Flags_CPU_Read_Write
		};

		m_storageAllocator.Initialize( param );
		m_storageAllocator.Reserve( RED_MEGA_BYTE( 25 ) );

		CreateEntry( CNameHash(), GetNameNone() );
	}

	red::StringView NameRegistry::GetNameString( const CNameHash hash ) const
	{
		const NameEntryIndex index = FindEntry( hash );
		if ( index < m_storageContainer.Size() ) // NOTE c_invalidNameIndex never within valid index range and will not pass this condition
		{
			const Storage * storage = GetStorage( index );
			return storage->AsStringView();
		}

		// Returns with a null terminated empty string rather than nullptr with {}
		return { "", 0 };
	}

	const NameRegistry::Storage * NameRegistry::GetStorage( const Uint32 index ) const
	{
		ScopedSharedLock< StorageLockPrimitive > scopedLock( m_lock );
		return m_storageContainer[index];
	}

	NameEntryIndex NameRegistry::FindEntry( const CNameHash hash ) const
	{
		const Uint32 bucketIndex = hash % c_nameRegistryHashTableSize;

		red::ScopedSharedLock< StorageLockPrimitive > scopedLock( m_lock );

		for ( const Storage * currentStorage = m_hashTable[ bucketIndex ]; currentStorage != nullptr; currentStorage = currentStorage->next )
		{
			if ( currentStorage->hash == hash )
			{
				return currentStorage->index;
			}
		}

		return c_invalidNameIndex;
	}

	void NameRegistry::RegisterName( const CNameHash hash, const StringView view )
	{
		if ( hash != c_invalidNameHashValue )
		{
			const NameEntryIndex index = FindEntry( hash );
			if ( index == c_invalidNameIndex )
			{
				CreateEntry( hash, view );
			}
		}
	}

	void NameRegistry::CreateEntry( const CNameHash hash, const StringView view )
	{
		if( view.Length() > c_redCNameMaxLength )
		{
			RED_LOG_ERROR( "CName string registration { '%hs', %u } exceeds maximum length, hash [%016llx].", view.ToString().AsChar(), view.Length(), hash );
			// TODO skip registration when length > CName maximum (255)
			//return;
		}

		const Uint32 blockSize = sizeof( Storage ) + view.Length() + sizeof( AnsiChar ); // NOTE account for string length and null termination
		const Uint32 bucketIndex = hash % c_nameRegistryHashTableSize;

		Uint8 * block = AllocateStorageMemory( blockSize );
		Storage * storage = new ( block ) Storage;
		Memcpy( storage->Data(), view.Data(), view.Length() );
		storage->Data()[view.Length()] = 0; // NOTE manual null termination

		ScopedLock< StorageLockPrimitive > lock( m_lock );

		const Uint32 storageIndex = m_storageContainer.Size();
		RED_ASSERT( storageIndex < c_nameRegistryHashTableIndexMax, "Storage container index exceeds maximum" );

		storage->hash = hash;
		storage->next = nullptr;
		storage->length = view.Length();
		storage->index = storageIndex;

		Storage * currentStorage = m_hashTable[ bucketIndex ];
		Storage ** listBackPtr = m_hashTable + bucketIndex;
		while ( currentStorage != nullptr )
		{
			if ( currentStorage->hash == hash )
			{
				RED_FATAL_ASSERT( currentStorage->AsStringView() == storage->AsStringView(),
					"CName hash collision between [%hs] and [%hs]\r\nBoth strings produce hash value [%llu]",
					currentStorage->AsChar(), storage->AsChar(), hash );
				// We have already created such entry before, just return it and delete the newly created one.
				// This case should be rare, so it's better to have shorter critical section in regular case, and pay the cost of allocation & deletion here.
				// Since we are using linear allocator this will actually "leak" memory. But so far the cost is negligible.
				storage->~Storage();
				FreeStorageMemory( reinterpret_cast< Uint8* >( storage ) );
				return;
			}
			listBackPtr = &currentStorage->next;
			currentStorage = currentStorage->next;
		}
		*listBackPtr = storage;

		m_storageContainer.PushBack( storage ); // ctremblay if this stays as a dynarray, lock could be heavy ... 
	}

	Uint8 * NameRegistry::AllocateStorageMemory( const Uint32 size )
	{
		ScopedLock< AllocatorLockPrimitive > lock( m_allocatorLock );
		Uint8 * block = static_cast<Uint8 *>( RED_ALLOCATE_ALIGNED( m_storageAllocator, size, __alignof( Storage ) ) );
		return block;
	}

	void NameRegistry::FreeStorageMemory( Uint8* ptr )
	{
		ScopedLock< AllocatorLockPrimitive > lock( m_allocatorLock );
		RED_FREE( m_storageAllocator, ptr );
	}

	NameRegistry * s_nameRegistryDebugger;

	NameRegistry & AcquireNameRegistry()
	{
		struct NameRegistryProxy
		{
			NameRegistryProxy()
			{
				registry.Initialize();
				s_nameRegistryDebugger = &registry;
			}

			NameRegistry registry;
		};

		static NameRegistryProxy s_nameRegistryProxy;
		return s_nameRegistryProxy.registry;
	}

	red::StringView GetNameString( const CNameHash hash )
	{
		if ( hash != c_invalidNameHashValue )
		{
			return AcquireNameRegistry().GetNameString( hash );
		}

		return GetNameNone();
	}

	void Debug_RegisterNameString( const CNameHash hash, const red::StringView view )
	{
		AcquireNameRegistry().RegisterName( hash, view );
	}

	StringView Debug_GetNameString( const CNameHash hash )
	{
		return AcquireNameRegistry().GetNameString( hash );
	}
}
