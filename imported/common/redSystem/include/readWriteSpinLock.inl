/**
* Copyright (c) 2014 CD Projekt Red. All Rights Reserved.
*/

#pragma once

namespace red
{

RED_INLINE RWSpinLock::RWSpinLock()
#if !defined(USE_NATIVE_RWLOCK)
	: m_lock( UnlockValue )
#endif
{
	profiler::SyncCreateMutex( this, "RWSpinLock" );
#if defined(USE_NATIVE_RWLOCK)
	InitializeSRWLock( &m_SRWLock );
#endif
}

RED_INLINE RWSpinLock::~RWSpinLock()
{
	profiler::SyncDestroyMutex( this );
}

RED_INLINE void RWSpinLock::ReleaseShared()
{
	profiler::SyncReleasing( this );

#if defined(USE_NATIVE_RWLOCK)
	ReleaseSRWLockShared( &m_SRWLock );
#else
	const Int32 prevValue = atomic::ExchangeAdd8( &m_lock, -1 );
	RED_ASSERT( prevValue > 0, "Invalid lock usage" );
	RED_UNUSED( prevValue );
#endif
}

RED_INLINE void RWSpinLock::Release()
{
	profiler::SyncReleasing( this );

#if defined(USE_NATIVE_RWLOCK)
	ReleaseSRWLockExclusive( &m_SRWLock );
#else
	const Int32 prevValue = atomic::Exchange8( &m_lock, UnlockValue );
	RED_ASSERT( prevValue == WriteLockValue, "Invalid lock usage" );
	RED_UNUSED( prevValue );
#endif
}

} // namespace red

