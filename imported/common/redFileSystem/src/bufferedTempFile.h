/**
* Copyright (c) 2007 CD Projekt Red. All Rights Reserved.
*/
#pragma once

#include "file.h"
#include "../../redCore/include/absolutePath.h"

namespace fs
{

	/// Temporary file writer that overwrites the specified file only after successful save
	class BufferedTempFile final : public IFile
	{
		RED_USE_MEMORY_POOL( red::PoolEngine );

	public:
		BufferedTempFile( const red::AbsolutePath& absoluteFilePath );
		virtual ~BufferedTempFile();

		// IFile interface
		virtual void Serialize( void* buffer, size_t size ) override;
		virtual Uint64 GetOffset() const override;
		virtual Uint64 GetSize() const override;
		virtual void Seek( Int64 offset ) override;
		virtual void Flush() override;

		/// IFile extended interface
		virtual const char* GetFileNameForDebug() const override;

	protected:
		red::UniquePtr< IFile > m_targetWriter;
		red::UniquePtr< IFile > m_tempWriter;
		red::AbsolutePath m_targetFilePath;
		red::AbsolutePath m_tempFilePath;
	};

} // fs