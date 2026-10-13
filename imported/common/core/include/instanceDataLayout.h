/**
* Copyright (c) 2007 CD Projekt Red. All Rights Reserved.
*/
#pragma once

// Instanced variable
class InstanceVar;
class InstanceBufferBase;
class InstanceBuffer;
class InstanceDataLayoutCompiler;

/// Data layout compiler
class RED_CORE_API InstanceDataLayout
{
	friend class InstanceBufferBase;
	friend class InstanceBuffer;
	friend class InstanceBufferIntrusive;
	friend class InstanceDataLayoutCompiler;

	typedef red::DynArray< const InstanceVar* >		TInstanceConstVarArray;
	typedef red::DynArray< InstanceBuffer* >		TInstanceBufferArray;

protected:
	Uint32									m_size;				//!< Current size
	TInstanceConstVarArray					m_vars{ red::PoolEngine() };				//!< Instance variables
	TInstanceConstVarArray					m_destructVars{ red::PoolEngine() };		//!< Instance variables that need destructors to be called

#if !defined(NO_EDITOR)
	mutable TInstanceBufferArray			m_buffers{ red::PoolEngine() };			//!< Buffers created from this data layout
	mutable red::Mutex						m_bufferMutex;		//!< Mutex for operation on buffer array
#endif

public:
	//! Get size of compiled data
	RED_INLINE Uint32 GetSize() const { return m_size; }

public:
	InstanceDataLayout();
	~InstanceDataLayout();

	//! Create initialized instance buffer
	InstanceBuffer* CreateBuffer( ISerializable* owner, const red::String& info ) const;

	//! Change layout of data
	void ChangeLayout( const InstanceDataLayoutCompiler& newLayout );

protected:
	//! Initialize buffer properties
	void InitializeBuffer( InstanceBufferBase& buffer ) const;

	//! Destroy buffer data
	void DestroyBufferData( InstanceBufferBase& buffer ) const;

	//! Clear data in given buffer
	void ClearBuffer( InstanceBufferBase& buffer ) const;

	//! Copy data from one buffer to another
	void CopyBuffer( InstanceBufferBase& dest, const InstanceBufferBase& source ) const;

	//! Release data from given buffer
	void ReleaseBuffer( InstanceBuffer& buffer ) const;

	//! Serialize data from given buffer
	void SerializeBuffer( IFile& file, InstanceBufferBase& buffer ) const;

	//! Validate variable; assert on failure
	void ValidateVariable( const InstanceVar* var ) const;

	//! These functions will iterate over multiple elements if the var is an array.
	RED_INLINE void ConstructVar( const InstanceVar& var, void* data ) const;
	RED_INLINE void DestructVar( const InstanceVar& var, void* data ) const;
	RED_INLINE void SerializeVar( IFile& file, const InstanceVar& var, void* data ) const;
	RED_INLINE void CopyVar( const InstanceVar& var, const void* src, void* dst ) const;
};