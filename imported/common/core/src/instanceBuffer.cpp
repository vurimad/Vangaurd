/**
* Copyright (c) 2007 CD Projekt Red. All Rights Reserved.
*/
#include "build.h"
#include "instanceBuffer.h"
#include "instanceDataLayout.h"

using red::String;

InstanceBufferBase::InstanceBufferBase( const InstanceDataLayout* layout )
	: m_data( NULL )
	, m_layout( layout )
#if defined(_DEBUG)
	, m_isRawBuffer( false )
#endif
{
	AllocateBuffer( layout );
}

InstanceBufferBase::InstanceBufferBase( InstanceBufferBase&& buf )
	: m_data( buf.m_data )
	, m_size( buf.m_size )
#if defined(_DEBUG)
	, m_isRawBuffer( buf.m_isRawBuffer )
#endif
	, m_layout( buf.m_layout )
{
	buf.m_data = nullptr;
	buf.m_layout = nullptr;
}

InstanceBufferBase::InstanceBufferBase()
	: m_data( nullptr )
	, m_size( 0 )
	, m_layout( nullptr )
#if defined(_DEBUG)
	, m_isRawBuffer( false )
#endif
{

}

InstanceBufferBase::~InstanceBufferBase()
{
	// Internal data should be freed by now
	RED_ASSERT( !m_layout );

	// Free allocated memory
	FreeBuffer();
}

void InstanceBufferBase::AllocateBuffer( const InstanceDataLayout* layout )
{
	RED_ASSERT( layout );

	// Allocate data
	m_size = layout->GetSize();
	if ( m_size )
	{
		// Memory is allocated from specialized pool
		m_data = RED_ALLOCATE_ALIGNED( red::PoolEngine, m_size, 16 );
		ASSERT( m_data );

		// TODO: get rid of this if possible
		// Initialize memory
		red::Memset( m_data, 0, m_size );
	}
}

void InstanceBufferBase::FreeBuffer()
{
	if ( m_data )
	{
		RED_FREE( red::PoolEngine, m_data );
		m_data = nullptr;
	}
}

void InstanceBufferBase::Clear()
{
	ASSERT( m_layout );
	m_layout->ClearBuffer( *this );
}

void InstanceBufferBase::Serialize( IFile& file )
{
	ASSERT( m_layout );
	m_layout->SerializeBuffer( file, *this );
}

void InstanceBufferBase::operator =( const InstanceBufferBase& obj )
{
	ASSERT( m_layout );
	m_layout->CopyBuffer( *this, obj );
}

void InstanceBufferBase::operator =( InstanceBufferBase&& obj )
{
	if ( m_data )
	{
		if ( m_layout )
		{
			m_layout->DestroyBufferData( *this );
			m_layout = nullptr;
		}
		RED_FREE( red::PoolEngine, m_data );
		m_data = nullptr;
	}

	m_layout = obj.m_layout;
	m_data = obj.m_data;
	m_size = obj.m_size;
#if defined(_DEBUG)
	m_isRawBuffer = obj.m_isRawBuffer;
#endif

	obj.m_layout = nullptr;
	obj.m_data = nullptr;
}


InstanceBuffer::InstanceBuffer( const InstanceDataLayout* layout, ISerializable* owner, const String& info )
	: InstanceBufferBase( layout )
	, m_owner( owner )
#ifndef RED_CONFIGURATION_FINAL
	, m_description( info )
#endif
{
}

void InstanceBuffer::Release()
{
#if defined(_DEBUG)
	ASSERT( ! m_isRawBuffer, "This is raw buffer. Release it with ReleaseRaw()" );

#endif
	ASSERT( m_layout );

	// Release the internal layout data
	if ( m_layout )
	{
		m_layout->ReleaseBuffer( *this );
		m_layout = nullptr;
	}

	// Delete data buffer
	RED_DELETE( this );
}

void InstanceBuffer::ReleaseRaw()
{
#if defined(_DEBUG)
	ASSERT( m_isRawBuffer, "This isn't raw buffer. Release it with Release()" );

#endif
	// clear layout, we're raw buffer, so we don't need to release it via layout and destructor expects to have layout cleared
	m_layout = NULL;
	// Delete data buffer
	RED_DELETE( this );
}

InstanceBuffer* InstanceBuffer::CreateCopy( ISerializable* owner, const String& info ) const
{
	InstanceBuffer* copy = m_layout->CreateBuffer( owner, info );
	copy->operator =( *this );
	return copy;
}

InstanceBuffer* InstanceBuffer::CreateRawCopy( ISerializable* owner, const String& info ) const
{
	InstanceBuffer* copy = RED_NEW( InstanceBuffer )( m_layout, owner, info );
	red::Memcpy( copy->m_data, m_data, m_size );
#if defined(_DEBUG)
	copy->m_isRawBuffer = true;
#endif
	return copy;
}


InstanceBufferIntrusive::~InstanceBufferIntrusive()
{
	Unbind();
}

void InstanceBufferIntrusive::Bind( const InstanceDataLayout* layout )
{
	RED_ASSERT( !m_layout, "Initialization of allready initialized buffer" );

	// In uninitialized state, we can still have memory allocated
	FreeBuffer();

	// Allocate data
	AllocateBuffer( layout );
	layout->InitializeBuffer( *this );
	m_layout = layout;
}
void InstanceBufferIntrusive::Unbind()
{
	if ( m_layout )
	{
		m_layout->DestroyBufferData( *this );
		m_layout = nullptr;
	}
}