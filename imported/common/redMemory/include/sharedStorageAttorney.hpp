/**
* Copyright (c) 20016 CD Projekt Red. All Rights Reserved.
*/

#ifndef _RED_MEMORY_SHARED_STORAGE_ATTORNEY_HPP_
#define _RED_MEMORY_SHARED_STORAGE_ATTORNEY_HPP_

namespace red
{
namespace internal
{
	template< typename StorageType >
	RED_MEMORY_INLINE void SharedStorageAttorney::InitializeRefCount( StorageType * storage )
	{
		*storage = StorageType( nullptr );
	}

	template< typename StorageType >
	RED_MEMORY_INLINE bool SharedStorageAttorney::Release( StorageType * storage )
	{
		return storage->InternalRelease(); 
	}

	template< typename StorageType >
	RED_MEMORY_INLINE void SharedStorageAttorney::AddRef( StorageType * storage )
	{
		storage->InternalAddRef();
	}

	template< typename StorageType >
	RED_MEMORY_INLINE void * SharedStorageAttorney::GetPointer( StorageType * storage )
	{
		return storage->Get();
	}

	template< typename StorageType >
	RED_MEMORY_INLINE void SharedStorageAttorney::SetPointer( StorageType * storage, void * ptr )
	{
		storage->InternalSet( ptr );
	}

	template< typename StorageType >
	RED_MEMORY_INLINE void SharedStorageAttorney::GetPointerAddress( StorageType * storage, void *& address )
	{
		storage->InternalGetPointerAddress( address );
	}

	template< typename StorageType >
	RED_MEMORY_INLINE void SharedStorageAttorney::Clear( StorageType * storage )
	{
		storage->InternalClear();
	}
}
}

#endif
