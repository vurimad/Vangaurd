/**
* Copyright (c) 2007 CD Projekt Red. All Rights Reserved.
*/
#pragma once

#include "file.h"

#include "../../redMemory/include/uniqueBuffer.h"

namespace fs
{

	/// Trivial buffered file reader
	/// Writes file into a local memory storage in large chunks and then to disk
	class RED_FILESYSTEM_API BufferedWriter final : public IFile
	{
		RED_USE_MEMORY_POOL( red::PoolEngine );

	public:
		explicit BufferedWriter( red::UniquePtr< IFile > writer, Uint32 size, Uint32 alignment );
		virtual ~BufferedWriter();

		// IFile interface
		virtual void Serialize( void* buffer, size_t size ) override;
		virtual Uint64 GetOffset() const override;
		virtual Uint64 GetSize() const override;
		virtual void Seek( Int64 offset ) override;
		virtual void Flush() override;

		/// IFile extended interface
		virtual const char* GetFileNameForDebug() const override;

	private:
		// Check if a given file offset fits the current buffer
		Bool IsOffsetWithinBuffer( Uint64 offset ) const;

		red::UniquePtr< IFile > m_writer;	// Low level writer
		Uint64 m_writerOffset;				// Current offset in the low level writer (cached)

		size_t m_bufferUsed;				// Number of bytes in the write buffer
		Uint64 m_bufferLocation;			// Location of the buffer's first byte in the target writer

		Uint64 m_offset;					// File offset
		Uint64 m_size;						// File size

		red::UniqueBuffer m_buffer;			// Data buffer
	};

} // fs
