/**
 * Copyright c 2016 CD Projekt Red. All Rights Reserved.
 */

#include "build.h"

#include "../../../common/core/include/commandlet.h"
#include "../../../common/redCore/include/commandline.h"

#include "../../../common/redCore/include/absolutePath.h"
#include "../../../common/redContainers/include/blob.h"
#include "../../../common/redFileSystem/include/fileSys.h"
#include "../../../common/redReflection/include/resourcePathCache.h"

#include "../../../common/archive/include/fileIOImpl.h"
#include "../../../common/archive/include/instance.h"

class ArchiveExtractCommandlet : public red::ICommandlet 
{
	RTTI_DECLARE_TYPE( ArchiveExtractCommandlet );

public:

	ArchiveExtractCommandlet();

	// ICommandlet interface
	virtual const char* GetOneLiner() const override;
	virtual bool Execute( const red::CommandLine& options ) override;
	virtual void PrintHelp() const override;

private:

	bool ExtractFile( const archive::FileMetadata& metadata, archive::FileReader& reader );

	struct Settings
	{
		red::AbsolutePath archivePath;
		red::AbsolutePath outputPath;

		archive::FileIDCode fileIdCode;

		Settings();
		bool Parse( const red::CommandLine& options );
	};

	Settings m_settings;
};

RTTI_BEGIN_TYPE( ArchiveExtractCommandlet )
	RTTI_PARENT_TYPE( red::ICommandlet )
RTTI_END_TYPE()



ArchiveExtractCommandlet::ArchiveExtractCommandlet()
	: ICommandlet( RED_NAME_CONSTEXPR( "archive-extract" ) )
{
}

const char* ArchiveExtractCommandlet::GetOneLiner() const
{
	return "Extracts loose files from an archive";
}

void ArchiveExtractCommandlet::PrintHelp() const
{
	RED_LOG( "Usage: " );
	RED_LOG( "  archive-extract -archive=<file> -output=<path> (-extract=<path>)" );
	RED_LOG( "Parameters: " );
	RED_LOG( "  -archive=<file>         - archive to extract files from" );
	RED_LOG( "  -output=<path>          - output path to extract files to" );
	RED_LOG( "  -file=<file pattern>    - pattern of files to extract" );
	RED_LOG( "Patterns:" );
	RED_LOG( "  path\\to\\file.ext      - extract specific file" );
	RED_LOG( "  0x<HEXSTRING>           - extract file with specific id/hash" );
}


//------------------------------------------------------------------------------


ArchiveExtractCommandlet::Settings::Settings()
	: fileIdCode( 0ull )
{
}

bool ArchiveExtractCommandlet::Settings::Parse( const red::CommandLine& options )
{
	String str;
	if ( options.GetFirstParam( "archive", str ) )
	{
		archivePath = red::AbsolutePath::ParseFilePath( str );
	}
	else
	{
		RED_LOG_ERROR( "Require -archive to be specified" );
		return false;
	}

	if ( options.GetFirstParam( "output", str ) )
	{
		outputPath = red::AbsolutePath::ParseDirPath( str );
	}
	else
	{
		RED_LOG_ERROR( "Require -output to be specified" );
		return false;
	}

	if ( options.GetFirstParam( "file", str ) )
	{
		if ( str.Length() > 2 && str[0] == '0' && ( str[1] == 'x' || str[1] == 'X' ) )
		{
			// if it starts with 0x or 0X then assume it's a 64bit file hash ID
			if ( red::SScanf( &str[2], "%llX", &fileIdCode ) != 1 )
			{
				RED_LOG_ERROR( "Could not parse '%s' as a hexidecimal file ID", str.AsChar() );
				return false;
			}
			if ( fileIdCode == 0ull )
			{
				RED_LOG_ERROR( "Invalid file ID '%s'provided", str.AsChar() );
				return false;
			}
		}
		else
		{
			const auto resPath = res::ResourcePath::Build( str );
			fileIdCode = resPath.GetHash();
		}
	}

	return true;
}


//------------------------------------------------------------------------------

bool ArchiveExtractCommandlet::ExtractFile( const archive::FileMetadata& metadata, archive::FileReader& reader )
{
	// Use resource path cache to see if there's a file name that we can get
	// if not then just extract out the hex path and use that.
	const res::ResourcePath resPath = res::ResourcePath::Build( metadata.GetIDCode() ); 
	red::String relativePath = resPath.ToString();
	if ( relativePath.Empty() )
	{
		relativePath = String::Printf( "%016llX", resPath.GetHash() );
	}
	const red::AbsolutePath outputPath = m_settings.outputPath.AddFilePath( relativePath );
	RED_LOG( "Extracting %s into %hs", metadata.ToDebugString(), outputPath.AsChar() );

	GFileManager->CreatePath( red::utils::ParentAbsolutePath( outputPath ) );

	io::NativeFileHandle writer;
	if ( !writer.Open( outputPath.AsChar(), io::eOpenFlag_Write | io::eOpenFlag_Create ) )
	{
		RED_LOG_ERROR( "Error opening extracted file '%hs' for writing.", outputPath.AsChar() );
		return false;
	}

	Uint32 segmentIndex = 0;
	red::DynArray< archive::FileSegment > segments{ metadata.GetSegments() };
	for ( const auto& segment : segments )
	{
		red::Blob buffer{ segment.sizeOnDisk };
		if ( !reader.Read( buffer.Data(), segment.position, segment.sizeOnDisk ) )
		{
			RED_LOG_ERROR( "Error reading the file contents from the archive for file %s for segment %i at %llu, %u", metadata.ToDebugString(), segmentIndex, segment.position, segment.sizeOnDisk );
			return false;
		}

		// NOTE keep segments compressed unless it's the first segment (object/metadata)
		const auto isCompressed = ( segment.sizeInMemory != segment.sizeOnDisk );
		if ( isCompressed && segmentIndex == 0 )
		{
			const auto result = compression::DecompressData( archive::DefaultCompressionType, buffer.Data(), buffer.Size(), compression::GetDefaultCompressionAllocator() );
			if ( !result )
			{
				RED_LOG_ERROR( "Error decompressing segment %i contents for file %s", segmentIndex, metadata.ToDebugString() );
				return false;
			}

			if ( result->GetDataSize() != segment.sizeInMemory )
			{
				RED_LOG_ERROR( "Error decompressed data size does not match what is specified in segment %i, espected %u, actual %u", segmentIndex, segment.sizeInMemory, result->GetDataSize() );
				return false;
			}

			buffer.Resize( segment.sizeInMemory );
			red::Memcpy( buffer.Data(), result->GetData(), result->GetDataSize() );
		}

		Uint32 bytesWritten = 0;
		if ( !writer.Write( buffer.Data(), buffer.Size(), bytesWritten ) || bytesWritten != buffer.Size() )
		{
			RED_LOG_ERROR( "Error writing segment %i to output file '%hs'", segmentIndex, outputPath.AsChar() );
			return false;
		}

		++segmentIndex;
	}

	return true;
}

bool ArchiveExtractCommandlet::Execute( const red::CommandLine& options )
{
	if ( !m_settings.Parse( options ) )
	{
		RED_LOG_ERROR( "Error parsing command line settings, exiting." );
		return false;
	}

	auto& resourcePathCache = res::prv::GetResourcePathCache();
	const red::AbsolutePath cachePath = GFileManager->GetCacheDirectory().AddFilePath( resourcePathCache.GetDefaultFilename() );
	resourcePathCache.Load( cachePath );

	archive::NullFileWriter nullWriter;
	archive::SyncFileReader reader = archive::SyncFileReader::Open( m_settings.archivePath );
	if ( !reader.Opened() )
	{
		RED_LOG_ERROR( "Failed to open the archive file %hs", m_settings.archivePath.AsChar() );
		return false;
	}

	archive::Instance archive( archive::OpenMode::READING_ONLY, reader, nullWriter );
	if ( archive.Load() != archive::LoadingResult::SUCCESSFUL )
	{
		RED_LOG_ERROR( "Failed to load the archive" );
		return false;
	}

	if ( m_settings.fileIdCode != 0ull )
	{
		archive::FileMetadata fileMetadata;
		if ( archive.FindFileClosestToOffset( archive::FileID( m_settings.fileIdCode ), 0, fileMetadata ) )
		{
			ExtractFile( fileMetadata, reader );
		}
		else
		{
			RED_LOG_ERROR( "File with ID %016llX not found in archive", m_settings.fileIdCode );
			return false;
		}
	}
	else
	{
		for ( const auto& file : archive.AllFiles() )
		{
			ExtractFile( file, reader );
		}
		return false;
	}

	return true;
}
