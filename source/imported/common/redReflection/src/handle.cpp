/**
* Copyright (c) 2016 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"
#include "handle.h"
#include "../../redFileSystem/include/file.h"
#include "serializationMapping.h"

HandleSharedStorage::HandleSharedStorage( ISerializable * pointer )
	:	m_pointee( pointer ),
		m_refCount( nullptr )
{
	// HACK ctremblay for backward compatibility with old THandle. Remove when raw ISerializable are correct.
	if( pointer )
	{
		auto handle = pointer->HandleFromThisInternal();
		// Uncomment this to catch wrong usage of explicit CTOR in runtime.
		//RED_FATAL_ASSERT( !handle, "Ref counter already created. Use HandleFromThis/Ptr instead." );
		if( handle )
		{
			m_refCount = handle.m_refCount;
			InternalAddRef();
		}
		else
		{
			m_refCount = RED_NEW_WITHOUT_HOOKS( RefCount, red::memory::HookType_Memory_Marking );
			pointer->InternalSetWeakHandle( *static_cast< THandle< ISerializable >* >( this ) );
		}
	}
}

void HandleSharedStorage::ReleaseWeak()
{
	if ( m_refCount )
	{
		Int32 postDecremented = atomic::Decrement32( &m_refCount->weak );
		RED_FATAL_ASSERT( postDecremented >= 0, "Ref counter underflow" );

		if ( postDecremented == 0 )
		{
			RED_DELETE( m_refCount );
			m_refCount = nullptr;
		}
	}
}

void HandleSharedStorage::AddRefWeak()
{
	if( m_refCount )
	{
		atomic::Increment32( &m_refCount->weak );
	}
}

const void* HandleSharedStorage::InternalGetRefCountStorage() const
{
	return m_refCount;
}

bool HandleSharedStorage::InternalDestroyRequest()
{
	return m_pointee->OnDestructionRequest();
}

void HandleSharedStorage::InternalDestroy()
{
	RED_DELETE( m_pointee );
}

Uint32 HandleSharedStorage::CalcHash() const
{
	return red::CalculatePtrHash32( Get() );
}
