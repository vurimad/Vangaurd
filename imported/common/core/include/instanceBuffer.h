/**
* Copyright (c) 2007 CD Projekt Red. All Rights Reserved.
*/
#pragma once

#include "instanceVar.h"
#include "instanceDataLayout.h"
#include "../../../common/redContainers/include/arraySpan.h"
#include "../../../common/redContainers/include/arrayWrapper.h"

// Base class for InstanceBuffers
class RED_CORE_API InstanceBufferBase
{
	RED_USE_MEMORY_POOL( red::PoolEngine );

	friend class InstanceDataLayout;

protected:
	void*							m_data;			//!< Data
	Uint32							m_size;			//!< Size of the buffer
#if defined(_DEBUG)
	Bool							m_isRawBuffer;	//!< Is it raw buffer
#endif
	const InstanceDataLayout*		m_layout;		//!< Buffer layout

	
	//! Create-and-initialize buffer, InstanceBufferBase is abstract
	InstanceBufferBase( const InstanceDataLayout* layout );

	//! Move-constructor
	InstanceBufferBase( InstanceBufferBase&& buf );

	//! Create uninitilized buffer, InstanceBufferBase is abstract
	InstanceBufferBase();

	//! Common destructor, abstract InstanceBufferBase is not destroyable (so its protected)
	~InstanceBufferBase();

	//! Common code for memory allocation
	void AllocateBuffer( const InstanceDataLayout* layout );

	//! Common code for memory free
	void FreeBuffer();

	//! Move data from other instance buffer (just switching buffer ownership - only viable for intrusive implementation)
	void operator =( InstanceBufferBase&& obj );

public:

	//! Get buffer size
	RED_INLINE Uint32 GetSize() const { return m_size; }

	//! Get data buffer 
	RED_INLINE void* GetData() { return m_data; }

	//! Get data buffer 
	RED_INLINE const void* GetData() const { return m_data; }

	//! Get buffer layout
	RED_INLINE const InstanceDataLayout* GetLayout() const { return m_layout; }

public:
	//! Clear buffer to default values
	void Clear();

	//! Serialize data
	void Serialize( IFile& file );

	//! Copy data from other instance buffer (using layout)
	void operator =( const InstanceBufferBase& obj );

public:
	void* GetRawPointer( const InstanceVar& var ) const
	{
#ifdef RED_ASSERTS_ENABLED
		m_layout->ValidateVariable( &var );
#endif
		return red::OffsetPtr( m_data, var.GetOffset() );
	}

	//! Get data for instance variable ( slow, debug )
	template< class T >
	T& operator[]( const TInstanceVar<T>& var )
	{
#ifdef RED_ASSERTS_ENABLED
		m_layout->ValidateVariable( &var );
#endif
		RED_FATAL_ASSERT( ( var.GetOffset() + sizeof( T ) ) <= m_size, "Offset is out of bound!" );
		return *( T*) red::OffsetPtr( m_data, var.GetOffset() );
	}

	//! Get data for instance variable ( slow, debug )
	template< class T >
	const T& operator[]( const TInstanceVar<T>& var ) const
	{
#ifdef RED_ASSERTS_ENABLED
		m_layout->ValidateVariable( &var );
#endif
		RED_FATAL_ASSERT( ( var.GetOffset() + sizeof( T ) ) <= m_size, "Offset is out of bound!" );
		return *( const T*) red::OffsetPtr( m_data, var.GetOffset() );
	}

	template< class T >
	red::ArraySpan<T> operator[]( const TInstanceVarArray<T>& var )
	{
#ifdef RED_ASSERTS_ENABLED
		m_layout->ValidateVariable( &var );
#endif
		RED_FATAL_ASSERT( var.GetNumElem() == 0 || ( var.GetOffset() + sizeof( T ) ) <= m_size, "Offset is out of bound!" );
		return red::ArraySpan<T>( static_cast< T* >( red::OffsetPtr( m_data, var.GetOffset() ) ), var.GetNumElem() );
	}

	template< class T >
	const red::ArraySpan<T> operator[]( const TInstanceVarArray<T>& var ) const
	{
#ifdef RED_ASSERTS_ENABLED
		m_layout->ValidateVariable( &var );
#endif
		RED_FATAL_ASSERT( var.GetNumElem() == 0 || ( var.GetOffset() + sizeof( T ) ) <= m_size, "Offset is out of bound!" );
		return red::ArraySpan<T>( static_cast< T* >( red::OffsetPtr( m_data, var.GetOffset() ) ), var.GetNumElem() );
	}

	template< class T >
	red::ArrayWrapperExt<T> operator[]( const TInstanceVarArrayExt<T>& var )
	{
#ifdef RED_ASSERTS_ENABLED
		m_layout->ValidateVariable( &var );
#endif
		RED_FATAL_ASSERT( var.GetNumElem() == 0 || ( var.GetOffset() + sizeof( T ) ) <= m_size, "Offset is out of bound!" );
		return red::ArrayWrapperExt<T>( red::OffsetPtr( m_data, var.GetOffset() ), var.GetNumElem(), *( (Uint32*)red::OffsetPtr( m_data, var.GetSizeVarOffset() ) ) );
	}

	template< class T >
	const red::ArrayWrapperExt<T> operator[]( const TInstanceVarArrayExt<T>& var ) const
	{
#ifdef RED_ASSERTS_ENABLED
		m_layout->ValidateVariable( &var );
#endif
		RED_FATAL_ASSERT( var.GetNumElem() == 0 || ( var.GetOffset() + sizeof( T ) ) <= m_size, "Offset is out of bound!" );
		RED_FATAL_ASSERT( var.m_arraySize + sizeof( Uint32 ) <= m_size, "Offset is out of bound!" );
		return red::ArrayWrapperExt<T>( red::OffsetPtr( m_data, var.GetOffset() ), var.GetNumElem(), *( (Uint32*)red::OffsetPtr( m_data, var.GetSizeVarOffset() ) ) );
	}
};

/// Buffer for instanced data. Meant to be allocated via InstanceDataLayout
class RED_CORE_API InstanceBuffer : public InstanceBufferBase
{
	friend class InstanceDataLayout;

protected:
	ISerializable*				m_owner;		//!< Owner of this instance buffer
#ifndef RED_CONFIGURATION_FINAL
	red::String					m_description;	//!< Debug description
#endif

public:
	~InstanceBuffer() {}

	//! Get owner of this instance buffer
	RED_INLINE ISerializable* GetOwner() const { return m_owner; }

public:
	//! Clear buffer to default values
	void Clear();

	//! Release buffer ( frees all the memory )
	void Release();

	//! Release raw buffer ( frees all the memory, but doesn't call destructors )
	void ReleaseRaw();

	//! Create copy of buffer
	InstanceBuffer* CreateCopy( ISerializable* owner, const red::String& info ) const;

	//! Create copy of raw buffer (without calling constructors)
	InstanceBuffer* CreateRawCopy( ISerializable* owner, const red::String& info ) const;

private:
	// Allocation and deallocation of instance buffer is done by third-party
	InstanceBuffer( const InstanceDataLayout* layout, ISerializable* owner, const red::String& info );

	// Copying of instance data buffer is not allowed ( we do not know what kind of data is there )
	RED_INLINE InstanceBuffer( const InstanceBuffer& ) {}
};


// Intrusive Instance buffer, meant to be kept by composition. It has less debugging functionalities (as Layout couldn't store pointer to its contents),
// but its interface allows for less restricting allocation and management policy.
class RED_CORE_API InstanceBufferIntrusive : public InstanceBufferBase
{
public:
	InstanceBufferIntrusive()
		: InstanceBufferBase() {}
	InstanceBufferIntrusive( InstanceBufferIntrusive&& buf )
		: InstanceBufferBase( std::move( buf ) ) {}
	~InstanceBufferIntrusive();

	//! Binds buffer with layout
	void Bind( const InstanceDataLayout* layout );

	//! Leaves buffer at uninitialized state
	void Unbind();

	//! Public move assignment
	void operator =( InstanceBufferIntrusive&& obj ) { InstanceBufferBase::operator=( std::move( obj ) ); }
	void operator =( const InstanceBufferIntrusive& obj ) { InstanceBufferBase::operator=( obj ); }
};

