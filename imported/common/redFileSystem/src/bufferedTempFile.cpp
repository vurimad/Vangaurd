/**
* Copyright (c) 2007 CD Projekt Red. All Rights Reserved.
*/
#include "build.h"
#include "bufferedTempFile.h"
#include "fileUtils.h"
#include "fileSys.h"

namespace fs
{

	BufferedTempFile::BufferedTempFile( const red::AbsolutePath& absoluteFilePath )
		: IFile( FF_FileBased | FF_Writer )
		, m_targetFilePath( absoluteFilePath )
	{
		RED_FATAL_ASSERT( !absoluteFilePath.Empty(), "Valid path is required" );

		// Open the destination file for writing
		// We will not write to this until the end but we need to check if we can here
		m_targetWriter = GFileManager->CreateFileWriter( m_targetFilePath );
		if ( !m_targetWriter )
		{
			HandleIOError( "Core: Unable to open output file '%hs'. File will not be saved.", absoluteFilePath.AsChar() );
			return;
		}

		// Create temp file
		m_tempFilePath = GFileManager->GenerateTemporaryFilePath();
		RED_FATAL_ASSERT( !m_tempFilePath.Empty(), "Failed to generate temporary path" );

		// Create actual writer for the temporary file
		m_tempWriter = GFileManager->CreateFileWriter( m_tempFilePath, FOF_Buffered );
		if ( !m_tempWriter )
		{
			HandleIOError( "Failed to create temporary file for saving '%hs' (temp file: '%hs')", m_tempFilePath.AsChar(), m_targetFilePath.AsChar() );
		}
	}

	BufferedTempFile::~BufferedTempFile()
	{
		if ( !m_targetWriter )
		{
			// Failed to open the target file for writing so we just exit here with nothing to do
			return;
		}

		if ( HasErrors() )
		{
			RED_LOG_ERROR( "Core: Not saving content to '%hs' because of IO errors during saving temp file '%hs'", m_targetFilePath.AsChar(), m_tempFilePath.AsChar() );
			return;
		}

		// Close temp writer
		m_tempWriter.Reset();

		// Reopen temp data
		red::UniquePtr<IFile> tempReader = GFileManager->CreateFileReader( m_tempFilePath, FOF_AbsolutePath );
		if ( !tempReader )
		{
			RED_LOG_ERROR( "Core: Unable to open temp file '%hs'. File '%hs' will not be saved.", m_tempFilePath.AsChar(), m_targetFilePath.AsChar() );
			return;
		}

		// copy data
		if ( !fs::CopyFileContent( *tempReader, *m_targetWriter ) )
		{
			RED_LOG_ERROR( "Core: Unable to copy file content from '%hs' to '%hs'", m_tempFilePath.AsChar(), m_targetFilePath.AsChar() );
		}
		else
		{
			const Uint64 size = m_targetWriter->GetSize();
			RED_LOG( "Core: Updated '%hs', %llu bytes of data", m_targetFilePath.AsChar(), size );
		}

		// Done
		tempReader.Reset();
		m_targetWriter.Reset();

		// Delete temporary file
		if ( !CSystemIO::DeleteFile( m_tempFilePath.AsChar() ) )
		{
			RED_LOG_ERROR( "Core: Unable to delete temporary file '%hs'", m_tempFilePath.AsChar() );
		}
	}

	void BufferedTempFile::Serialize( void* buffer, size_t size )
	{
		m_tempWriter->Serialize( buffer, size );
	}

	Uint64 BufferedTempFile::GetOffset() const
	{
		return m_tempWriter->GetOffset();
	}

	Uint64 BufferedTempFile::GetSize() const
	{
		return m_tempWriter->GetSize();
	}

	void BufferedTempFile::Seek( Int64 offset )
	{
		m_tempWriter->Seek( offset );
	}

	void BufferedTempFile::Flush()
	{
		m_tempWriter->Flush();
	}

	const char* BufferedTempFile::GetFileNameForDebug() const
	{
		return m_tempFilePath.AsChar();
	}

} // fs