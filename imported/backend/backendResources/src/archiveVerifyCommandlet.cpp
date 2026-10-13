/**
 * Copyright (c) 2018 CD Projekt Red. All Rights Reserved.
 */

#include "build.h"

#include "../../../common/core/include/commandlet.h"
#include "../../../common/redCore/include/commandline.h"

#include "../../../common/redContainers/include/string/stringUtils.h"
#include "../../../common/redCore/include/absolutePath.h"
#include "../../../common/redContainers/include/blob.h"

#include "../../../common/redFileSystem/include/fileSys.h"

#include "../../../common/archive/include/fileIOImpl.h"
#include "../../../common/archive/include/instance.h"
#include "../../../common/archive/include/inspector.h"

#include "../../backendData/include/dependencyHash.h"


class ArchiveVerifyCommandlet : public red::ICommandlet 
{
	RTTI_DECLARE_TYPE( ArchiveVerifyCommandlet );

public:

	ArchiveVerifyCommandlet();

	// ICommandlet interface
	virtual const char* GetOneLiner() const override;
	virtual bool Execute( const red::CommandLine& options ) override;
	virtual void PrintHelp() const override;

private:

	bool ParseSettings( const red::CommandLine& options );

	bool VerifyFile( const archive::Instance& archive, archive::SyncFileReader& reader, const archive::FileMetadata& file );
	bool VerifyArchive( const red::AbsolutePath& archivePath );

	void Output( const red::String& str );
	void ReportError( const red::String& str );
	void ReportError( const archive::FileID& fileId, const red::String& str );

	red::DynArray< red::AbsolutePath > m_archivePaths;

	red::AbsolutePath m_currentArchivePath;

	bool m_checkFileExtents;
	bool m_checkFileHashes;
};

RTTI_BEGIN_TYPE( ArchiveVerifyCommandlet )
	RTTI_PARENT_TYPE( red::ICommandlet )
RTTI_END_TYPE()



ArchiveVerifyCommandlet::ArchiveVerifyCommandlet()
	: ICommandlet( RED_NAME_CONSTEXPR( "verify-archive" ) )
	, m_checkFileExtents{ true }
	, m_checkFileHashes{ true }
{
}

const char* ArchiveVerifyCommandlet::GetOneLiner() const
{
	return "Verifies the archive is valid";
}

void ArchiveVerifyCommandlet::PrintHelp() const
{
}


//------------------------------------------------------------------------------


bool ArchiveVerifyCommandlet::ParseSettings( const red::CommandLine& options )
{
	if ( !options.HasOption( "archive" ) )
	{
		RED_LOG_ERROR( "Require -archive to be specified" );
		return false;
	}

	auto archives = options.GetValues( "archive" );
	for ( const auto& str : archives )
	{
		// Allow specifying *.archive in a directory 
		if ( red::IsGlobString( str ) )
		{
			const red::AbsolutePath fullPath = red::AbsolutePath::ParseFilePath( str );
			const red::AbsolutePath basePath = red::paths::ParentAbsolutePath( fullPath );
			const red::String searchStr = red::paths::ExtractFileName( fullPath );

			red::DynArray< red::AbsolutePath > paths;
			GFileManager->FindFiles( basePath, searchStr, paths, false );

			m_archivePaths.PushBack( paths );
		}
		else
		{
			red::AbsolutePath archivePath = red::AbsolutePath::ParseFilePath( str );
			m_archivePaths.PushBack( archivePath );
		}
	}

	m_checkFileExtents = !options.HasOption( "skip-files" );
	m_checkFileHashes = !options.HasOption( "skip-file-hashes" );

	if ( !m_checkFileExtents )
	{
		m_checkFileHashes = false;
	}

	return true;
}

void ArchiveVerifyCommandlet::Output( const red::String& str )
{
	if ( m_currentArchivePath.Empty() )
	{
		auto message = String::Printf( "%hs\n", str.AsChar() );
		fprintf( stdout, message.AsChar() );
		RED_LOG( message.AsChar() );
	}
	else
	{
		auto message = String::Printf( "%hs : %hs\n", m_currentArchivePath.AsChar(), str.AsChar() );
		fprintf( stdout, message.AsChar() );
		RED_LOG( message.AsChar() );
	}
}

void ArchiveVerifyCommandlet::ReportError( const red::String& str )
{
	auto message = String::Printf( "%hs : %hs\n", m_currentArchivePath.AsChar(), str.AsChar() );
	fprintf( stdout, message.AsChar() );
	RED_LOG_ERROR( message.AsChar() );
}

void ArchiveVerifyCommandlet::ReportError( const archive::FileID& fileId, const red::String& str )
{
	auto message = String::Printf( "%hs : %hs : %hs\n", m_currentArchivePath.AsChar(), fileId.ToDebugString().AsChar(), str.AsChar() );
	fprintf( stdout, message.AsChar() );
	RED_LOG_ERROR( message.AsChar() );
}

bool ArchiveVerifyCommandlet::VerifyFile( const archive::Instance& archive, archive::SyncFileReader& reader, const archive::FileMetadata& file )
{
	const auto& inspector = archive.GetInspector();
	const Uint64 startPos = inspector.GetValidContentStart();
	const Uint64 endPos = inspector.GetValidContentEnd();

	const auto segments = file.GetSegments();

	for ( const auto& segment : segments )
	{
		if ( segment.position < startPos || ( segment.position + segment.sizeOnDisk ) > endPos )
		{
			ReportError( file.GetID(), "Segment for file lies outside the valid content area" );
			return false;
		}

		if ( segment.sizeOnDisk > segment.sizeInMemory )
		{
			ReportError( file.GetID(), "Segment has larger size on disk than in memory, compresion broken or corruption detected!" );
			return false;
		}
	}

	// extract out the file and compare hashes
	if ( m_checkFileHashes )
	{
		red::Blob fileBlob{ file.GetTotalSizeInMemory() };
		Uint32 offset = 0;
		for ( const auto& segment : segments )
		{
			if ( segment.sizeInMemory != segment.sizeOnDisk )
			{
				red::Blob diskBlob{ segment.sizeOnDisk };
				reader.Read( diskBlob.Pointer<Uint8>(), segment.position, segment.sizeOnDisk );
				auto resultBufferPtr = compression::DecompressData( archive::DefaultCompressionType, diskBlob.Data(), diskBlob.Size(), compression::GetDefaultCompressionAllocator() );
				if ( !resultBufferPtr )
				{
					auto message = String::Printf( "Could not decompress segment at position %llu size %u", segment.position, segment.sizeOnDisk );
					ReportError( file.GetID(), message );
					return false;
				}

				RED_WARNING( segment.sizeInMemory == resultBufferPtr->GetDataSize(), "Expected size in memory to be size of uncompressed buffer" );
				red::Memcpy( fileBlob.Pointer<Uint8>( offset ), resultBufferPtr->GetData(), segment.sizeInMemory );
			}
			else // sizeInMemory == sizeOnDisk
			{
				reader.Read( fileBlob.Pointer<Uint8>( offset ), segment.position, segment.sizeOnDisk );
			}
			offset += segment.sizeInMemory;
		}

		const dependency::Hash& fileHash = dependency::Hash::FromFileHash( file.GetHash() );
		dependency::Hash newHash = dependency::Hash::Compute( fileBlob );
		if ( fileHash != newHash )
		{
			auto newHashString = dependency::Hash::ToEncodedString( newHash );
			auto fileHashString = dependency::Hash::ToEncodedString( fileHash );
			auto message = String::Printf( "File hash stored in archive does not match hash computed from file segments. '%hs' != '%hs'", 
				fileHashString.AsChar(), newHashString.AsChar() );
			ReportError( file.GetID(), message );
			return false;
		}
	}

	return true;
}

bool ArchiveVerifyCommandlet::VerifyArchive( const red::AbsolutePath& archivePath )
{
	archive::NullFileWriter nullWriter;
	archive::SyncFileReader reader = archive::SyncFileReader::Open( archivePath );
	if ( !reader.Opened() )
	{
		RED_LOG_ERROR( "Failed to open the archive file %hs", archivePath.AsChar() );
		return false;
	}

	m_currentArchivePath = archivePath;

	archive::Instance archive( archive::OpenMode::INSPECTION, reader, nullWriter );
	auto archiveLoadResult = archive.Load();
	if ( archiveLoadResult != archive::LoadingResult::SUCCESSFUL )
	{
		if ( archiveLoadResult == archive::LoadingResult::ERROR_IO )
		{
			ReportError( "Could not read data from the archive file, archive is truncated or corrupt!" );
		}
		else if ( archiveLoadResult == archive::LoadingResult::INVALID_ARCHIVE )
		{
			ReportError( "Could not parse the archive correctly, the metadata is corrupt!" );
		}
		else if ( archiveLoadResult == archive::LoadingResult::INVALID_VERSION )
		{
			ReportError( "Invalid archive version, archive is out of date!" );
		}
		else if ( archiveLoadResult == archive::LoadingResult::INVALID_CRC )
		{
			ReportError( "Invalid CRCs found in the metadata, archive is corrupt!" );
		}
		return false;
	}

	bool allFilesValid = true;
	if ( m_checkFileExtents )
	{
		const auto files = archive.AllFiles();
		for ( const auto& file : files )
		{
			if ( !VerifyFile( archive, reader, file ) )
			{
				allFilesValid = false;
			}
		}
	}

	m_currentArchivePath.Clear();

	return allFilesValid;
}

bool ArchiveVerifyCommandlet::Execute( const red::CommandLine& options )
{
	if ( !ParseSettings( options ) )
	{
		RED_LOG_ERROR( "Error parsing command line settings, exiting." );
		return false;
	}

	for ( const auto& archivePath : m_archivePaths )
	{
		if ( VerifyArchive( archivePath ) )
		{
			fprintf( stdout, "Archive %hs verified valid!", archivePath.AsChar() );
		}
		else
		{
			fprintf( stdout, "Archive %hs failed validation! Consider deleting and rebuilding!", archivePath.AsChar() );
		}
	}

	return true;
}
