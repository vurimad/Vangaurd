/**
 * Copyright c 2016 CD Projekt Red. All Rights Reserved.
 */

#include "build.h"

#include "../../../common/core/include/commandlet.h"
#include "../../../common/redCore/include/commandline.h"

#include "../../../common/redContainers/include/string/stringUtils.h"
#include "../../../common/redCore/include/absolutePath.h"
#include "../../../common/resource/include/basicTextBuilder.h"

#include "../../../common/redFileSystem/include/fileSys.h"

#include "../../../common/archive/include/fileIOImpl.h"
#include "../../../common/archive/include/instance.h"
#include "../../../common/archive/include/inspector.h"

#include "../../backendData/include/dependencyHash.h"

//------------------------------------------------------------------------------

class ArchiveDumpHelper
{
public:
	enum class OutputType
	{
		CSV,
		PRETTY,
	};

	struct Settings
	{
		OutputType outputType;
		bool everything;
		bool resourcesOnly;
	};

	explicit ArchiveDumpHelper( const Settings& settings );

	bool Process( const red::AbsolutePath& archivePath, const red::AbsolutePath& outputPath );

private:
	void DumpContents( CBasicTextBuilder& textBuilder, const archive::Inspector& inspector );
	void DumpContentsPretty( CBasicTextBuilder& textBuilder, const archive::Inspector& inspector );
	void DumpContentsCsv( CBasicTextBuilder& textBuilder, const archive::Inspector& inspector );
	void DumpResourcesOnlyCsv( CBasicTextBuilder& textBuilder, const archive::Inspector& inspector );

	const Settings& m_settings;

	Uint64 m_fileDataBytes;
	Uint64 m_freeSpaceBytes;
};

//------------------------------------------------------------------------------

class ArchiveDumpCommandlet : public red::ICommandlet 
{
	RTTI_DECLARE_TYPE( ArchiveDumpCommandlet );

public:

	ArchiveDumpCommandlet();

	// ICommandlet interface
	virtual const char* GetOneLiner() const override;
	virtual bool Execute( const red::CommandLine& options ) override;
	virtual void PrintHelp() const override;

private:

	struct ArchiveEntry
	{
		red::AbsolutePath archivePath;
		red::AbsolutePath outputPath;
	};

	bool ParseSettings( const red::CommandLine& options );

	ArchiveDumpHelper::Settings m_settings;
	red::DynArray< ArchiveEntry > m_archives;
};

RTTI_BEGIN_TYPE( ArchiveDumpCommandlet )
	RTTI_PARENT_TYPE( red::ICommandlet )
RTTI_END_TYPE()

//------------------------------------------------------------------------------

ArchiveDumpCommandlet::ArchiveDumpCommandlet()
	: ICommandlet( RED_NAME_CONSTEXPR( "dump-archive" ) )
{
}

const char* ArchiveDumpCommandlet::GetOneLiner() const
{
	return "Dump information about an archive to a text file";
}

void ArchiveDumpCommandlet::PrintHelp() const
{
	RED_LOG( "Usage: " );
	RED_LOG( "  dump-archive -archive=<file> (<file>)" );
	RED_LOG( "Parameters: " );
	RED_LOG( "  -archive=<file>     - archive file to dump information about" );
	RED_LOG( "  -everything         - optional output details for every table and component" );
	RED_LOG( "  -resources-only     - list resources in raw csv format (this is designed for further processing)" );
	RED_LOG( "  -pretty             - list contents in more verbose/pretty format" );
	RED_LOG( "  -csv                - list contents in csv format (this is the default)" );
}

//------------------------------------------------------------------------------

bool ArchiveDumpCommandlet::ParseSettings( const red::CommandLine& options )
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

			for ( const auto& archivePath : paths )
			{
				red::AbsolutePath outputPath =  red::paths::ReplaceExtension( archivePath, "dump.txt" );
				m_archives.PushBack( { archivePath, outputPath } );
			}
		}
		else
		{
			red::AbsolutePath archivePath = red::AbsolutePath::ParseFilePath( str );
			red::AbsolutePath outputPath = red::paths::ReplaceExtension( archivePath, "dump.txt" );
			m_archives.PushBack( { archivePath, outputPath } );
		}
	}

	m_settings.everything = options.HasOption( "everything" );
	m_settings.resourcesOnly = options.HasOption( "resources-only" );

	m_settings.outputType = ArchiveDumpHelper::OutputType::CSV;
	if ( options.HasOption( "pretty" ) )
	{
		m_settings.outputType = ArchiveDumpHelper::OutputType::PRETTY;
	}

	return true;
}

bool ArchiveDumpCommandlet::Execute( const red::CommandLine& options )
{
	if ( !ParseSettings( options ) )
	{
		RED_LOG_ERROR( "Error parsing command line settings, exiting." );
		return false;
	}

	for ( const auto& entry : m_archives )
	{
		RED_LOG( "Dumping %hs to %hs", entry.archivePath.AsChar(), entry.outputPath.AsChar() );
		ArchiveDumpHelper helper( m_settings );
		helper.Process( entry.archivePath, entry.outputPath );
	}

	return true;
}

//------------------------------------------------------------------------------

namespace
{
	double Percent( Uint64 num, Uint64 den )
	{
		if ( den == 0 )
		{
			return 0.0;
		}
		return ( num / static_cast< double >( den ) ) * 100;
	}
}

ArchiveDumpHelper::ArchiveDumpHelper( const Settings& settings )
	: m_settings( settings )
	, m_fileDataBytes( 0ull )
	, m_freeSpaceBytes( 0ull )
{
}

bool ArchiveDumpHelper::Process( const red::AbsolutePath& archivePath, const red::AbsolutePath& outputPath )
{
	archive::NullFileWriter nullWriter;
	archive::SyncFileReader reader = archive::SyncFileReader::Open( archivePath );
	if ( !reader.Opened() )
	{
		RED_LOG_ERROR( "Failed to open the archive file %hs", archivePath.AsChar() );
		return false;
	}

	archive::Instance archive( archive::OpenMode::INSPECTION, reader, nullWriter );
	if ( archive.Load() != archive::LoadingResult::SUCCESSFUL )
	{
		RED_LOG_ERROR( "Failed to load the archive" );
		return false;
	}

	CBasicTextBuilder textBuilder;

	const archive::Inspector& inspector = archive.GetInspector();

	if ( m_settings.resourcesOnly )
	{
		DumpResourcesOnlyCsv( textBuilder, inspector );
	}
	else
	{
		// File Details
		textBuilder.Write( "Archive Summary\n" );
		textBuilder.Writef( "Name: %s\n", archivePath.AsChar() );
		textBuilder.Writef( "Size: %llu\n", inspector.GetFileSize() );
		textBuilder.Write( "\n" );

		DumpContents( textBuilder, inspector );
	}

	// Write out file
	{
		auto filePtr = GFileManager->CreateFileWriter( outputPath, FOF_AbsolutePath );
		if ( !filePtr )
		{
			RED_LOG_ERROR( "Failed to open dump report output file: %hs", outputPath.AsChar() );
			return false;
		}
		textBuilder.SaveToFile( filePtr.Get() );
	}

	return true;
}

void ArchiveDumpHelper::DumpContents( CBasicTextBuilder& textBuilder, const archive::Inspector& inspector )
{
	// Header Summary
	textBuilder.Write( "Header\n" );
	textBuilder.Writef( "Version: %u\n", inspector.GetArchiveVersion() );
	textBuilder.Write( "\n" );

	// Index Summary
	textBuilder.Write( "Index\n" );
	auto indexSpan = inspector.GetIndexSpan();
	textBuilder.Writef( "Position: %llu\n", indexSpan.position );
	textBuilder.Writef( "Size: %u\n", indexSpan.size );
	textBuilder.Write( "\n" );

	textBuilder.Write( "Table Name          CRC             \tOffset   \tSize     \tPosition\n" );
	auto fileTableSpan = inspector.GetFileTableSpan();
	textBuilder.Writef( "File Table          %016llX\t%9u\t%9u\t%llu\n", 
		inspector.GetFileTableCRC(),
		fileTableSpan.position - indexSpan.position,
		fileTableSpan.size,
		fileTableSpan.position );
	textBuilder.Write( "\n" );

	if ( m_settings.outputType == OutputType::CSV )
	{
		DumpContentsCsv( textBuilder, inspector );
	}
	else
	{
		DumpContentsPretty( textBuilder, inspector );
	}

	// Statistics
	textBuilder.Write( "Statistics\n" );
	const Uint64 fileSize = inspector.GetFileSize();
	textBuilder.Writef( "Archive Size: %llu\n", fileSize );
	const Uint64 metadataSize = inspector.GetMetadataSize();
	textBuilder.Writef( "Metadata Size: %llu (%.3lf%%)\n", metadataSize, Percent( metadataSize, fileSize ) );
	textBuilder.Writef( "File Data Size: %llu (%.3lf%%)\n", m_fileDataBytes, Percent( m_fileDataBytes, fileSize ) );
	const Uint64 otherSize = inspector.GetFileSize() - inspector.GetMetadataSize() - m_fileDataBytes - m_freeSpaceBytes;
	textBuilder.Writef( "Padding Size: %llu (%.3lf%%)\n", otherSize, Percent( otherSize, fileSize ) );
}

void ArchiveDumpHelper::DumpContentsPretty( CBasicTextBuilder& textBuilder, const archive::Inspector& inspector )
{
	// File Listing
	textBuilder.Write( "Files\n" );
	textBuilder.Write( "\n" );

	red::ArraySpan< const archive::FileMetadata > files = inspector.GetFileList();
	for ( const auto& file : files )
	{
		textBuilder.Writef( "%016llX\n", file.GetIDCode() );
		textBuilder.Writef( "Timestamp: %llu\n", file.GetTimestamp() );
		String hashStr = dependency::Hash::ToEncodedString( dependency::Hash::FromFileHash( file.GetHash() ) );
		textBuilder.Writef( "Hash: %hs\n", hashStr.AsChar() );
		textBuilder.Writef( "Position: %llu (%016llx)\n", file.GetPositionOnDisk(), file.GetPositionOnDisk() );
		textBuilder.Writef( "Size on Disk: %u bytes\n", file.GetTotalSizeOnDisk() );
		textBuilder.Writef( "Size in Memory: %u bytes\n", file.GetTotalSizeInMemory() );
		const auto& segments = file.GetSegments();
		textBuilder.Writef( "Segments: %u\n", segments.Size() );
		for ( const auto& segment : segments )
		{
			textBuilder.Writef( "Position: %llu\tSize on Disk: %u\tSize in Memory: %u\n", segment.position, segment.sizeOnDisk, segment.sizeInMemory );
		}
		textBuilder.Writef( "Padding: %u bytes\n ", archive::Inspector::CalculatePaddingByteSize( archive::FileSpan( file.GetPositionOnDisk(), file.GetTotalSizeOnDisk() ) ) );
		textBuilder.Write( "\n" );

		m_fileDataBytes += file.GetTotalSizeOnDisk();
	}
}

void ArchiveDumpHelper::DumpContentsCsv( CBasicTextBuilder& textBuilder, const archive::Inspector& inspector )
{
	// File Listing
	textBuilder.Write( "Files\n" );
	textBuilder.Write( "File ID\tTimestamp\tPosition\tSize on Disk\tSize in Memory\n" );

	red::ArraySpan< const archive::FileMetadata > files = inspector.GetFileList();
	for ( const auto& file : files )
	{
		textBuilder.Writef( "%016llX\t%llu\t%llu\t%u\t%u\t%s\n", 
			file.GetIDCode(), file.GetTimestamp(), file.GetPositionOnDisk(), file.GetTotalSizeOnDisk(), file.GetTotalSizeInMemory(),
			dependency::Hash::ToEncodedString( dependency::Hash::FromFileHash( file.GetHash() ) ).AsChar()
		);
		m_fileDataBytes += file.GetTotalSizeOnDisk();
	}

	textBuilder.Write( "\n" );
}

void ArchiveDumpHelper::DumpResourcesOnlyCsv( CBasicTextBuilder& textBuilder, const archive::Inspector& inspector )
{
	textBuilder.Write( "File ID\tTimestamp\tPosition\tSize on Disk\tSize in Memory\n" );

	red::ArraySpan< const archive::FileMetadata > files = inspector.GetFileList();
	for ( const auto& file : files )
	{
		textBuilder.Writef( "%016llX\t%llu\t%llu\t%u\t%u\n", 
			file.GetIDCode(), file.GetTimestamp(), file.GetPositionOnDisk(), file.GetTotalSizeOnDisk(), file.GetTotalSizeInMemory() );
	}
}

