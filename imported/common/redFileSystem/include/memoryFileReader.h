/**
* Copyright (c) 2007 CD Projekt Red. All Rights Reserved.
*/
#pragma once

#include "file.h"

/////////////////////////////////////////////////////////////////////////////////////////////

/// Array based file reader
class RED_FILESYSTEM_API CMemoryFileReader : public IFile, red::NonCopyable
{
	RED_USE_MEMORY_POOL( red::PoolEngine );

private:
	const Uint8*					m_data;
	size_t							m_dataSize;
	uintptr_t						m_offset;		// Write offset

protected:
	void SetData( const Uint8 * data ) { m_data = data; }
	void SetSize( size_t size ) { m_dataSize = size; }

public:

	CMemoryFileReader( const red::DynArray< Uint8 >& data, uintptr_t offset );
	CMemoryFileReader( const Uint8* data, size_t dataSize, uintptr_t offset );

	virtual ~CMemoryFileReader();

	// IFile interface
	virtual void Serialize( void* buffer, size_t size ) override;
	virtual Uint64 GetOffset() const override;
	virtual Uint64 GetSize() const override;
	virtual void Seek( Int64 offset ) override;
	virtual void Flush() override;
};

/////////////////////////////////////////////////////////////////////////////////////////////

/// Pointer to array based reader
class RED_FILESYSTEM_API CMemoryFileReaderWithBuffer : public CMemoryFileReader
{
private:
	red::DynArray< Uint8 > m_dataPtr;		// Data buffer

public:
	CMemoryFileReaderWithBuffer( Uint32 size );
	virtual ~CMemoryFileReaderWithBuffer();

	//! Get data
	void* GetData(){ return m_dataPtr.Data(); }
};

/////////////////////////////////////////////////////////////////////////////////////////////

/// External buffer based reader
class RED_FILESYSTEM_API CMemoryFileReaderExternalBuffer : public IFile
{
private:
	const void*			m_buffer;		//!< Data buffer
	Uint32				m_size;			//!< Data size
	Uint32				m_offset;		//!< Current offset

	const char* m_debugName;

private:
	CMemoryFileReaderExternalBuffer& operator=( const CMemoryFileReaderExternalBuffer& ) { return *this; }

public:
	CMemoryFileReaderExternalBuffer( const void* buffer, Uint32 size, const char* debugName = nullptr );
	virtual ~CMemoryFileReaderExternalBuffer();

	// IFile interface
	virtual void Serialize( void* buffer, size_t size ) override;
	virtual Uint64 GetOffset() const override;
	virtual Uint64 GetSize() const override;
	virtual void Seek( Int64 offset ) override;
	virtual void Flush() override;

	virtual const char* GetFileNameForDebug() const override;
};

/////////////////////////////////////////////////////////////////////////////////////////////
