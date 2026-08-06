/**
* Copyright (c) 2007 CD Projekt Red. All Rights Reserved.
*/
#pragma once

#include "file.h"
#include "../../../common/redSystem/include/utility.h"
#include "../../../common/redContainers/include/dynArrayAccessor.h"

/// Memory based file writer
class RED_FILESYSTEM_API CMemoryFileWriter : public IFile, private red::NonCopyable
{
public:
	CMemoryFileWriter( red::DynArray< Uint8 >& data );
	virtual ~CMemoryFileWriter();

	// IFile interface
	virtual void Serialize( void* buffer, size_t size ) override;
	virtual Uint64 GetOffset() const override;
	virtual Uint64 GetSize() const override;
	virtual void Seek( Int64 offset ) override;
	virtual void Flush() override;

	RED_INLINE void* GetData() const { return m_data.Data(); }

	void SetFileNameForDebug( const red::String& str );
	virtual const char *GetFileNameForDebug() const override;

private:
	red::DynArrayAccessor& m_data;		// By using DynArrayAccessor, we can write to any dynamic array of Uint8, regardless of pool
	Uint32 m_offset;	

	red::String m_debugFileName;
};

class RED_FILESYSTEM_API CMemoryFileWriterWithDebugName : public CMemoryFileWriter
{
	using TBaseClass = CMemoryFileWriter;

public:
	CMemoryFileWriterWithDebugName(red::DynArray< Uint8 >& data, const String& debugName);
	virtual const char* GetFileNameForDebug() const override;

private:
	String m_debugName;
};

/// Memory based file writer
class RED_FILESYSTEM_API CMemoryFileWriterExternalBuffer : public IFile, red::NonCopyable
{
private:
	void*				m_buffer;		//!< Data buffer
	Uint32				m_offset;		//!< Current offset
	Uint32				m_realSize;		//!< Maximum offset
	Uint32				m_size;			//!< Data size

public:
	CMemoryFileWriterExternalBuffer( void* buffer, Uint32 size );
	virtual ~CMemoryFileWriterExternalBuffer();

	// IFile interface
	virtual void Serialize( void* buffer, size_t size ) override;
	virtual Uint64 GetOffset() const override;
	virtual Uint64 GetSize() const override;
	virtual void Seek( Int64 offset ) override;
	virtual void Flush() override;

	RED_INLINE Uint32 GetRealSize() const { return m_realSize; }
	RED_INLINE Bool CanSerialize( size_t size ) const { return static_cast< Int32 >( m_size - m_offset ) >= size; };
	RED_INLINE void* GetData() const { return m_buffer; }
};

class RED_FILESYSTEM_API CMemoryFileWriterExternalUniqueBuffer : public IFile, red::NonCopyable
{
private:
	red::UniqueBuffer& m_buffer;
	Uint32 m_offset;   //!< Current offset
	Uint32 m_realSize; //!< Maximum offset

public:
	CMemoryFileWriterExternalUniqueBuffer( red::UniqueBuffer& buffer );
	virtual ~CMemoryFileWriterExternalUniqueBuffer();

	// IFile interface
	virtual void Serialize( void* buffer, size_t size ) override;
	virtual Uint64 GetOffset() const override;
	virtual Uint64 GetSize() const override;
	virtual void Seek( Int64 offset ) override;
	virtual void Flush() override;

	RED_INLINE Uint32 GetRealSize() const { return m_realSize; }
	RED_INLINE void* GetData() const { return m_buffer.Data(); }
};

class RED_FILESYSTEM_API CMemoryFileBufferedWriter : public IFile, red::NonCopyable
{
private:
	CMemoryFileWriterExternalBuffer m_staticWriter;
	CMemoryFileWriter m_dynamicWriter;
	Bool m_usesDynamicWriter;

public:
	CMemoryFileBufferedWriter( void* buffer, Uint32 size, red::DynArray< Uint8 >& data );
	virtual ~CMemoryFileBufferedWriter();

	virtual void Serialize( void* buffer, size_t size ) override;
	virtual Uint64 GetOffset() const override;
	virtual Uint64 GetSize() const override;
	virtual void Seek( Int64 offset ) override;
	virtual void Flush() override;

	void* GetData() const;
	Uint32 GetRealSize() const;
};
