/**
* Copyright (c) 2016 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"
#include "atomicSharedStorage.h"

template RED_MEMORY_API void* red::memory::internal::NewHelper< red::internal::AtomicRefCount >( red::memory::u32 );

namespace red
{
namespace internal
{
	void AtomicSharedStorage::InternalSet( void * pointer )
	{
		m_pointee = pointer;
	}

	void AtomicSharedStorage::InternalClear()
	{
		m_pointee = nullptr;
		m_refCount = nullptr;
	}

	void AtomicSharedStorage::InternalGetPointerAddress( void *& pointer )
	{
		pointer = m_pointee;
	}
}
}
