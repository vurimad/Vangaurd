/**
* Copyright (c) 2007-20 CD Projekt Red. All Rights Reserved.
*/
#pragma once

#include "../../redIO/include/redIOFile.h"
#include "file.h"
#include "../../redCore/include/absolutePath.h"

namespace fs
{

	// IFile implementation that handles unbuffered file reads
	class RED_FILESYSTEM_API RawFileReader final : public IFile, public red::NonCopyable
	{
		RED_USE_MEMORY_POOL( red::PoolEngine );

	public:
		// Create file reader
		static red::UniquePtr< RawFileReader > Create( const red::AbsolutePath& absoluteFilePath );

		RawFileReader();
		virtual ~RawFileReader();
		
		// IFile interface
		virtual void Serialize( void* buffer, size_t size ) override;
		virtual Uint64 GetOffset() const override;
		virtual Uint64 GetSize() const override;
		virtual void Seek( Int64 offset ) override;
		virtual void Flush() override;

		virtual const char* GetFileNameForDebug() const override;

	private:
		io::NativeFileHandle	m_fileHandle;
		red::AbsolutePath		m_filePath;
	};

} // fs
