/**
* Copyright (c) 2007-16 CD Projekt Red. All Rights Reserved.
*/
#include "build.h"
#include "instanceDataLayout.h"
#include "instanceBuffer.h"
#include "../../redReflection/include/rttiType.h"
#include "instanceDataLayoutCompiler.h"
#include "../../../common/redContainers/include/set.h"


//////////////////////////////////////////////////////////////////////////
// usings
using red::OffsetPtr;
using red::DynArray;
using red::String;
using red::Set;


InstanceDataLayout::InstanceDataLayout()
	: m_size( 0 )
{
}

InstanceDataLayout::~InstanceDataLayout()
{
	// There are still some buffers using this layout, WTF ?
#if !defined(NO_EDITOR)
	RED_FATAL_ASSERT( m_buffers.Empty(), "Relasing instance data layout where there are still %i buffer(s) using it. Please DEBUG.", m_buffers.Size() );
#endif
}

void InstanceDataLayout::InitializeBuffer( InstanceBufferBase& buffer ) const
{
	void* data = buffer.GetData();

	// Initialize data
	for ( Uint32 i=0; i<m_vars.Size(); i++ )
	{
		const InstanceVar* var = m_vars[i];
		ASSERT( var );

		// Initialize field
		ConstructVar( *var, data );
	}
}

void InstanceDataLayout::DestroyBufferData( InstanceBufferBase& buffer ) const
{
	void* data = buffer.GetData();
	// Destroy data
#ifndef NO_EDITOR
	for ( Uint32 i=0; i<m_vars.Size(); i++ )
	{
		const InstanceVar* var = m_vars[i];
		ASSERT( var );

		DestructVar( *var, data );
	}
#else
	
	for ( Uint32 i=0; i<m_destructVars.Size(); i++ )
	{
		const InstanceVar* var = m_destructVars[i];
		ASSERT( var );

		DestructVar( *var, data );
	}

#endif
}

InstanceBuffer* InstanceDataLayout::CreateBuffer( ISerializable* owner, const String& info ) const
{
	// Allocate buffer
	InstanceBuffer* buffer = RED_NEW( InstanceBuffer )( this, owner, info );

	InitializeBuffer( *buffer );

#if !defined(NO_EDITOR)
	{
		red::ScopedLock< red::Mutex > lock( m_bufferMutex );
		m_buffers.PushBack( buffer );
	}
#endif

	// Return created buffer
	return buffer;
}

void InstanceDataLayout::ClearBuffer( InstanceBufferBase& buffer ) const
{
	// Destroy data
	DestroyBufferData( buffer );

	// Clear memory
	red::Memset( buffer.GetData(), 0, buffer.GetSize() );

	// Initialize data
	InitializeBuffer( buffer );
}

void InstanceDataLayout::ReleaseBuffer( InstanceBuffer& buffer ) const
{
	// Remove the buffer from the list
#if !defined(NO_EDITOR)
	{
		red::ScopedLock< red::Mutex > lock( m_bufferMutex );
		ASSERT( m_buffers.Exist( &buffer ) );
		m_buffers.Remove( &buffer );
	}
#endif

	DestroyBufferData( buffer );

	// Mark memory
#ifdef _DEBUG
	red::Memset( buffer.GetData(), 0xCC, buffer.GetSize() );
#endif
}

void InstanceDataLayout::CopyBuffer( InstanceBufferBase& dest, const InstanceBufferBase& source ) const
{
	ASSERT( dest.m_layout == this, "Buffers should be copied with their own layout" );
	ASSERT( dest.m_layout == source.m_layout, "Buffers have different layout" );

	// Destroy existing data
	DestroyBufferData( dest );

	const void* srcData = source.GetData();
	void* destData = dest.GetData();

	// Clear memory
	red::Memset( destData, 0, dest.GetSize() );

	// Initialize data
	for ( Uint32 i=0; i<m_vars.Size(); i++ )
	{
		const InstanceVar* var = m_vars[i];
		ASSERT( var );

		ConstructVar( *var, destData );
		CopyVar( *var, srcData, destData );
	}
}

void InstanceDataLayout::SerializeBuffer( IFile& file, InstanceBufferBase& buffer ) const
{
	void* data = buffer.GetData();

	// Destroy data
	for ( Uint32 i=0; i<m_vars.Size(); i++ )
	{
		const InstanceVar* var = m_vars[i];
		ASSERT( var );

		// Initialize field
		SerializeVar( file, *var, data );
	}
}

void InstanceDataLayout::ChangeLayout( const InstanceDataLayoutCompiler& newLayout )
{
#if !defined(NO_EDITOR)

	red::ScopedLock< red::Mutex > lock( m_bufferMutex );

	// We have some buffer that uses current layout, we need to reformat them
	if ( m_buffers.Size() )
	{
		// Get variables
		Set< const InstanceVar* > newVarsSet{ red::PoolEngine() };
		const TInstanceVarList& newVars = newLayout.GetVariables();
		for ( Uint32 i=0; i<newVars.Size(); i++ )
		{
			newVarsSet.Insert( newVars[i].first );
		}

		// Get the list of variables that were removed from layout
		DynArray< InstanceVar* > removedVariables{ red::PoolBackend() };
		for ( Uint32 i=0; i<m_vars.Size(); i++ )
		{
			const InstanceVar* var = m_vars[i];
			if ( newVarsSet.Find( var ) == newVarsSet.End() )
			{
				// Instance variable was removed from the layout
				removedVariables.PushBack( const_cast< InstanceVar* >( var ) );
			}
		}

		// Convert buffers
		for ( Uint32 i=0; i<m_buffers.Size(); i++ )
		{
			// Get the buffer data
			void* oldBufferData = m_buffers[i]->GetData();

			// Destroy variables
			for ( Uint32 j=0; j<removedVariables.Size(); j++ )
			{
				DestructVar( *removedVariables[j], oldBufferData );
			}

			// Allocate new buffer data
			const Uint32 newDataSize = newLayout.GetCurrentSize();
			if ( newDataSize > 0 )
			{
				// Create new buffer data
				void *newBufferData = RED_ALLOCATE( red::PoolEngine, newDataSize );
				red::Memset( newBufferData, 0, newDataSize );

				// Bind new data buffer
				m_buffers[i]->m_data = newBufferData;
				m_buffers[i]->m_size = newDataSize;

				// Copy existing variables from old locations in old buffer and initialize new values
				for ( Uint32 j=0; j<newVars.Size(); j++ )
				{
					InstanceVar* newVar = newVars[j].first;
					const Uint32 newVarOffset = newVars[j].second;

					// If var was in the old layout than copy the data from the old place
					if ( m_vars.Exist( newVar ) )
					{
						// Copy value ( fast, no copy constructor called )
						const Uint32 varDataSize = newVar->GetType()->GetSize();
						const void* oldVarData = OffsetPtr( oldBufferData, newVar->GetOffset() );
						void* newVarData = OffsetPtr( newBufferData, newVarOffset );
						red::Memcpy( newVarData, oldVarData, varDataSize * newVar->GetNumElem() );
					} 
					else
					{
						newVar->SetOffset( newVarOffset );
						INST_BUFFER_DEBUG( newVar->SetIndex( j ); )
						ConstructVar( *newVar, newBufferData );
					}
				}
			}
			else
			{
				// Bind empty data buffer ( can happen )
				m_buffers[i]->m_data = NULL;
				m_buffers[i]->m_size = 0;
			}

			// Destroy old buffer data
			RED_FREE( red::PoolEngine, oldBufferData );
		}

		// Mark removed variables as not addressed
		for ( Uint32 j=0; j<removedVariables.Size(); j++ )
		{
			InstanceVar* removedVar = removedVariables[j];
			RED_ASSERT( removedVar );
			//removedVar->SetOffset( 0xFFFFFFFF );
			INST_BUFFER_DEBUG( removedVar->SetIndex( 0xFFFFFFFF ); )
		}
	}

	// Set layout size
	m_size = newLayout.GetCurrentSize();

	// Set new list of variables
	m_vars.Clear();
	const TInstanceVarList& newVars = newLayout.GetVariables();
	for ( Uint32 i=0; i<newVars.Size(); i++ )
	{
		// Set variable offset
		InstanceVar* var = newVars[i].first;
		RED_ASSERT( var );
		var->SetOffset( newVars[i].second );
		INST_BUFFER_DEBUG( var->SetIndex( i ); )
		// Add to list of variables
		m_vars.PushBack( var );

		// Add to list of variables that require destruction
		if ( var->GetType()->NeedsCleaning() || var->GetType()->GetType() == RT_Class )
		{
			m_destructVars.PushBack( var );
		}
	}

	#else

	// Set layout size
	m_size = newLayout.GetCurrentSize();

	#endif
}

void InstanceDataLayout::ValidateVariable( const InstanceVar* var ) const
{
	ASSERT( var->m_index < m_vars.Size() && var->m_offset == m_vars[ var->m_index ]->m_offset );
}

RED_INLINE void InstanceDataLayout::ConstructVar( const InstanceVar& var, void* data ) const
{
	const rtti::IType* type = var.GetType();
	ASSERT( type );

	void* varData = OffsetPtr( data, var.GetOffset() );
	Uint32 numOfEl = var.GetNumElem();
	for ( Uint32 i = 0; i < numOfEl; ++i )
	{
		type->Construct( OffsetPtr( varData, i * type->GetSize() ) );		
	}
}

RED_INLINE void InstanceDataLayout::DestructVar( const InstanceVar& var, void* data ) const
{
	const rtti::IType* type = var.GetType();
	ASSERT( type );

	void* varData = OffsetPtr( data, var.GetOffset() );
	Uint32 numOfEl = var.GetNumElem();
	for ( Uint32 i = 0; i < numOfEl; ++i )
	{
		type->Destruct( OffsetPtr( varData, i * type->GetSize() ) );		
	}
}

RED_INLINE void InstanceDataLayout::SerializeVar( IFile& file, const InstanceVar& var, void* data ) const
{
	const rtti::IType* type = var.GetType();
	ASSERT( type );

	void* varData = OffsetPtr( data, var.GetOffset() );
	Uint32 numOfEl = var.GetNumElem();
	for ( Uint32 i = 0; i < numOfEl; ++i )
	{
		type->Serialize( file, OffsetPtr( varData, i * type->GetSize() ) );		
	}
}

RED_INLINE void InstanceDataLayout::CopyVar( const InstanceVar& var, const void* src, void* dst ) const
{
	const rtti::IType* type = var.GetType();
	ASSERT( type );

	void* varData = OffsetPtr( dst, var.GetOffset() );
	const void* varSrcData = OffsetPtr( src, var.GetOffset() );
	Uint32 typeSize = type->GetSize();
	Uint32 numOfEl = var.GetNumElem();
	for ( Uint32 i = 0; i < numOfEl; ++i )
	{
		type->Copy( OffsetPtr( varData, i * typeSize ), OffsetPtr( varSrcData, i * typeSize ) );			
	}
}
