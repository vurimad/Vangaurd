/**
* Copyright (c) 2007-20 CD Projekt Red. All Rights Reserved.
*/
#pragma once

#include "file.h"

#include "../../redMemory/include/uniqueBuffer.h"

namespace fs
{

	/// Trivial buffered file reader
	/// Reads file into a local memory storage in large chunks (32KB fixed size)
	class RED_FILESYSTEM_API BufferedReader final : public IFile
	{
		RED_USE_MEMORY_POOL( red::PoolEngine );

	public:
		explicit BufferedReader( red::UniquePtr< IFile > reader, Uint32 size, Uint32 alignment );
		virtual ~BufferedReader();

		/// IFile interface
		virtual void Serialize( void* buffer, size_t size ) override;
		virtual Uint64 GetOffset() const override;
		virtual Uint64 GetSize() const override;
		virtual void Seek( Int64 offset ) override;
		virtual void Flush() override;

		/// IFile extended interface
		virtual const char* GetFileNameForDebug() const override;

	private:
		red::UniquePtr< IFile > m_reader;	// Low level reader

		Uint64 m_bufferBase;				// From where in the source file we have the data cached
		Uint64 m_bufferCount;				// Number of bytes in the read buffer

		Uint64 m_offset;					// File offset
		Uint64 m_size;						// File size

		red::UniqueBuffer m_buffer;			// Data buffer
	};

} // fs
