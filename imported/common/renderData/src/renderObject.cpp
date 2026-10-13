/**
* Copyright (c) 2007 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"
#include "renderObject.h"

IRenderObject::~IRenderObject()
{
	RED_ASSERT( m_refCount.GetValue() == 0 );
}

IRenderObject::IRenderObject()
	: m_refCount( 1 )
{
}

Bool IRenderObject::IsRefCountZero() const
{
	return 0 == m_refCount.GetValue();
}

void IRenderObject::DestroySelfIfRefCountZero()
{
#ifdef RED_ASSERTS_ENABLED
	Int32 refCount = m_refCount.GetValue();
	RED_ASSERT( refCount == 0, "Destroying IRenderObject with non-zero refcount: %d", refCount );
#endif
	RED_DELETE( this );
}

void IRenderObject::AddRef()
{
	Int32 newRefCount = m_refCount.Increment();
	RED_ASSERT( newRefCount > 1, "AddRefing IRenderObject that already reached 0 refs. It's about to be deleted." );
}

void IRenderObject::Release()
{
	Int32 newRefCount = m_refCount.Decrement();
	RED_FATAL_ASSERT( newRefCount >= 0, "Releasing IRenderObject too many times" );
	if ( newRefCount == 0 )
	{
		DestroySelfIfRefCountZero();
	}
}

