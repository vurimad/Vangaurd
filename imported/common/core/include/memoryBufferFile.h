/*
 * Copyright (c) 2017 CD Projekt Red. All Rights Reserved.
 */

#pragma once

#include "../../redMemory/include/linearAllocator.h"
#include "../../redFileSystem/include/file.h"

//------------------------------------------------------------------------------

class RED_CORE_API GrowableMemoryBuffer
{
	RED_USE_MEMORY_POOL( red::PoolBackend );

public:
	GrowableMemoryBuffer( Uint32 blockSize = RED_KILO_BYTE( 64u ), Uint32 blockInitCount = 1 );
	~GrowableMemoryBuffer();

	GrowableMemoryBuffer( const GrowableMemoryBuffer& ) = delete;
	GrowableMemoryBuffer operator=( const GrowableMemoryBuffer& ) = delete;

	void* Data();
	const void* Data() const;
	Uint32 BlockSize() const;
	Uint32 Size() const;
	Uint32 Capacity() const;

	bool Read( Uint32 offset, void* buffer, Uint32 size ) const;
	bool Write( Uint32 offset, const void* buffer, Uint32 size );

private:
	bool EnsureSize( Uint32 size );

	red::memory::DynamicLinearAllocator m_allocator;
	red::memory::Block m_allocation;
	Uint8* m_start;	// Start of the buffer
	Uint8* m_end;	// End of the buffer
	Uint8* m_ptr;	// Pointer to the end of the valid portion of the buffer
};

//------------------------------------------------------------------------------

class RED_CORE_API MemoryBufferFileWriter final : public IFile
{
public:
	explicit MemoryBufferFileWriter( GrowableMemoryBuffer& buffer );
	~MemoryBufferFileWriter();

	MemoryBufferFileWriter( const MemoryBufferFileWriter& ) = delete;
	MemoryBufferFileWriter operator=( const MemoryBufferFileWriter& ) = delete;

	virtual void Serialize( void* buffer, size_t size ) override;
	virtual Uint64 GetOffset() const override;
	virtual Uint64 GetSize() const override;
	virtual void Seek( Int64 offset ) override;
	virtual void Flush() override;

private:
	GrowableMemoryBuffer& m_buffer;
	Uint64 m_offset;
};

//------------------------------------------------------------------------------

class RED_CORE_API MemoryBufferFileReader final : public IFile
{
public:
	explicit MemoryBufferFileReader( const GrowableMemoryBuffer& buffer );
	~MemoryBufferFileReader();

	MemoryBufferFileReader( const MemoryBufferFileReader& ) = delete;
	MemoryBufferFileReader operator=( const MemoryBufferFileReader& ) = delete;

	virtual void Serialize( void* buffer, size_t size ) override;
	virtual Uint64 GetOffset() const override;
	virtual Uint64 GetSize() const override;
	virtual void Seek( Int64 offset ) override;
	virtual void Flush() override;

private:
	const GrowableMemoryBuffer& m_buffer;
	Uint64 m_offset;
};