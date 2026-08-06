/**
* Copyright (c) 2016 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"
#include "nonAtomicSharedStorage.h"

namespace red
{
namespace internal
{
	void NonAtomicSharedStorage::InternalSet( void * pointer )
	{
		m_pointee = pointer;
	}

	void NonAtomicSharedStorage::InternalClear()
	{
		m_pointee = nullptr;
		m_refCount = nullptr;
	}
	
	void NonAtomicSharedStorage::InternalGetPointerAddress( void *& pointer )
	{
		pointer = m_pointee;
	}
}
}
