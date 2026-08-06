/**
 * Copyright (c) 2016 CD Projekt Red. All Rights Reserved.
 */

#ifndef _RED_MEMORY_FILE_STREAM_WIN_H_
#define _RED_MEMORY_FILE_STREAM_WIN_H_

#include "../include/stream.h"

namespace red
{
namespace memory
{
	class SystemAllocator;

	enum FileResult : u8
	{
		FileResult_OK,
		FileResult_OpenFailed,
		FileResult_CloseFailed,
		FileResult_FileNotOpen,
		FileResult_WriteFailed
	};

	class FileStreamWin final : public Stream
	{
	public:
		FileStreamWin();
		virtual ~FileStreamWin();

		FileResult Open( const char * filePath );
		FileResult Close();
		FileResult Flush();

	private:

		virtual u32 OnDataAvailableToWrite() const override;
		virtual u32 OnDataAvailableToRead() const override;
		virtual Bool OnWriteBuffer( const void * buffer, u32 size ) override;
		virtual Bool OnReadBuffer( void * buffer, u32 size, u32 & readSize ) override;
		virtual void* OnGetReadData() const override;
		virtual void OnSeekReadIndex( u32 size ) override;

		void * m_fileHandle;
		u8 * m_buffer;
		u8 * m_bufferMarker;
	};
}
}

#endif
