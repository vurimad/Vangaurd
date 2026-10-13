/**
* Copyright (c) 20016 CD Projekt Red. All Rights Reserved.
*/

#ifndef _RED_MEMORY_SHARED_STORAGE_ATTORNEY_H_
#define _RED_MEMORY_SHARED_STORAGE_ATTORNEY_H_

namespace red
{
namespace internal
{
	// DO NOT USE THIS CLASS. Usage is reserved for RTTI.
	class SharedStorageAttorney
	{
	public:
	
		template< typename StorageType >
		static void InitializeRefCount( StorageType * storage );

		template< typename StorageType >
		static bool Release( StorageType * storage );
	
		template< typename StorageType >
		static void AddRef( StorageType * storage );

		template< typename StorageType >
		static void * GetPointer( StorageType * storage );

		template< typename StorageType >
		static void SetPointer( StorageType * storage, void * ptr );

		template< typename StorageType >
		static void GetPointerAddress( StorageType * storage, void *& address );
		
		template< typename StorageType >
		static void Clear( StorageType * storage );
	};
}
}

#include "sharedStorageAttorney.hpp"

#endif
