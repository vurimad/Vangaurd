/**
 * Copyright (c) 2016 CD Projekt Red. All Rights Reserved.
 */

#include "build.h"
#include "instance.h"

#include "fileIOInterfaces.h"
#include "fileFormat.h"
#include "utils.h"
#include "inspector.h"

#include "../../redContainers/include/blob.h"
#include "../../redReflection/include/serializationFileTables.h"
#include "../../redReflection/include/version.h"

#include <numeric>

namespace archive
{
#ifndef RED_READONLY_ARCHIVES
namespace utils
{
	Uint64 InitialFileDataPosition()
	{
		return sizeof(fileformat::MainHeader) + sizeof(fileformat::DebugContent);
		//return utils::GetAlignedPosition( sizeof(fileformat::MainHeader) + sizeof(fileformat::DebugContent), fileformat::GlobalAlignment );
	}

	static const Uint64 GetBuildVersionHash()
	{
		// This for now. Later once we patch the game, it'll need to change. But we'll know because we'll fail to load all archives.
		const char key[] = APP_LAST_P4_CHANGE APP_P4_STREAM APP_P4_SHELF;
		return red::CalculateHash64( key );
	}

	static String GetArchiveSaverComputerName()
	{
#ifdef RED_PLATFORM_WINPC
		char name[ 1024 ] = "";
		DWORD size = RED_ARRAY_COUNT_U32( name );
		if ( ::GetComputerNameExA( ComputerNameDnsHostname, name, &size ) )
		{
			return String( name );
		}
#endif
		return "<Unknown host>";
	}

	static const Bool IsArchivesByBuildMachine()
	{
		const String name = GetArchiveSaverComputerName().ToLower();
		return name.BeginsWith( "cdprs-build" );
	}

	void WritePaddingBytes( FileWriter& writer, Uint32 padding )
	{
		const Uint32 paddingBytes = utils::GetAlignedPaddingBytes( writer.Tell(), padding );
		if ( paddingBytes != 0 )
		{
			red::Blob paddingBuffer{ paddingBytes };
			red::Memset( paddingBuffer.Data(), fileformat::PaddingByte, paddingBytes );
			RED_VERIFY( writer.Write( paddingBuffer.Data(), paddingBytes ) );
		}
	}
}
#endif // RED_READONLY_ARCHIVES


Instance::Instance( OpenMode openMode, FileReader& reader, FileWriter& writer )
	: m_reader( reader )
	, m_writer( writer )
	, m_fileTable()
#ifndef RED_READONLY_ARCHIVES
	, m_fileDataStartPosition( utils::InitialFileDataPosition() )
	, m_fileDataEndPosition( utils::InitialFileDataPosition() )
#endif
	, m_version( fileformat::Version )
	, m_openMode( openMode )
{
}

Instance::Instance( Instance&& other )
	: m_reader( other.m_reader )
	, m_writer( other.m_writer )
	, m_fileTable( std::move( other.m_fileTable ) )
#ifndef RED_READONLY_ARCHIVES
	, m_fileDataStartPosition( other.m_fileDataStartPosition )
	, m_fileDataEndPosition( other.m_fileDataEndPosition )
#endif
	, m_version( other.m_version )
	, m_openMode( other.m_openMode )
{
#ifndef RED_READONLY_ARCHIVES
	other.m_fileDataStartPosition = other.m_fileDataEndPosition = utils::InitialFileDataPosition();
#endif
}

Instance::~Instance()
{
}

LoadingResult Instance::Load()
{
	red::DynArray< res::ResourcePath > existingResources{ red::PoolEngine() };
	return Load( existingResources );
}

LoadingResult Instance::Load( red::DynArray< res::ResourcePath >& existingResources )
{
	RED_FATAL_ASSERT( OpenModePermitsReading( m_openMode ), "Cannot load an archive when not opened for reading" );
	RED_FATAL_ASSERT( m_reader.Tell() == 0ull, "Must read archive from the start of the file" );

	constexpr Uint32 minimumReadSize = RED_KILO_BYTE( 4 );
	if ( m_reader.Size() < minimumReadSize )
	{
		RED_LOG_ERROR( "Invalid sized archive found. Size: %u", m_reader.Size() );
		return LoadingResult::INVALID_ARCHIVE;
	}

	red::Blob buffer{ red::CreateUniqueBuffer< archive::PoolArchive >( minimumReadSize, 16 ) };
	if ( !m_reader.Read( buffer.Data(), buffer.Size() ) )
	{
		RED_LOG_ERROR( "Error reading main header from archive" );
		return LoadingResult::ERROR_IO;
	}

	fileformat::MainHeader& header = buffer.As<fileformat::MainHeader>();
	if ( header.magic != fileformat::MainHeader::MAGIC_VALUE )
	{
		RED_LOG_ERROR( "Invalid magic value found in archive main header. Expected: %08X Found: %08X", fileformat::MainHeader::MAGIC_VALUE, header.magic );
		return LoadingResult::INVALID_ARCHIVE;
	}

	// Currently there are incompatible changes in the new version, and it's messy to support backwards compatability
	if ( header.version != archive::fileformat::Version && header.version != archive::fileformat::old::Version )
	{
		RED_LOG_ERROR("Invalid and/or unsupported archive version found in archive main header. Expected: %u or %u Found: %u", 
			archive::fileformat::Version, archive::fileformat::old::Version, header.version );
		return LoadingResult::INVALID_VERSION;
	}
	m_version = header.version;

	if ( header.totalFileSize != m_reader.Size() )
	{
		RED_LOG_ERROR( "File size differs to what is reported in the archive main header. Actual File Size: %llu Header File Size: %llu", m_reader.Size(), header.totalFileSize );
		return LoadingResult::INVALID_ARCHIVE;
	}

	if ( header.totalFileSize < header.indexPosition + header.indexSize )
	{
		RED_LOG_ERROR( "Invalid main header found in the archive" );
		return LoadingResult::INVALID_ARCHIVE;
	}

	if ( ( header.indexPosition % fileformat::GlobalAlignment ) != 0 || 
		( header.totalFileSize % fileformat::GlobalAlignment ) != 0 )
	{
		RED_LOG_ERROR( "Main index is found at an unaligned offset, archive is invalid!" );
		return LoadingResult::INVALID_ARCHIVE;
	}

	m_fileDataEndPositionValidAfterLoading = header.indexPosition;

#ifndef RED_READONLY_ARCHIVES
	m_fileDataEndPosition = header.indexPosition;

	if ( m_openMode == OpenMode::INSPECTION )
	{
		m_inspector = red::CreateUniquePtr< Inspector >( *this, m_fileTable );
		m_inspector->Push( header );
	}
#endif

	red::Blob indexBlob{ header.indexSize };
	if ( !m_reader.Read( indexBlob.Data(), header.indexPosition, header.indexSize ) )
	{
		RED_LOG_ERROR( "Error loading the index from the archive" );
		return LoadingResult::ERROR_IO;
	}

	LoadingResult result = LoadIndex( indexBlob, m_version, existingResources );
	if ( result != LoadingResult::SUCCESSFUL )
	{
		return result;
	}

	return LoadingResult::SUCCESSFUL;
}

Bool Instance::LoadEntireArchiveIntoMemory(red::Blob& outBlob)
{
	outBlob = red::Blob();

	const Uint64 oldPos = m_reader.Tell();
	const Uint64 size = m_fileDataEndPositionValidAfterLoading;

	ALWAYSENABLED_RED_FATAL_ASSERT(size <= std::numeric_limits<Uint32>::max());

	red::Blob buffer{ red::CreateUniqueBuffer< archive::PoolArchive >((Uint32)size, 1) };

	if (!m_reader.Seek(0))
	{
		RED_LOG_ERROR("Error reading archive into blob: seek error");
		return false;
	}

	if (!m_reader.Read(buffer.Data(), buffer.Size()))
	{
		m_reader.Seek(oldPos);
		RED_LOG_ERROR("Error reading archive into blob: read error");
		return false;
	}

	outBlob = std::move(buffer);

	m_reader.Seek(oldPos);

	return true;
}

LoadingResult Instance::LoadIndex( const red::BlobView& indexData, Uint32 archivesVersion, red::DynArray< res::ResourcePath >& existingResources )
{
	RED_FATAL_ASSERT( OpenModePermitsReading( m_openMode ), "Cannot load an index when not opened for reading" );

	if ( indexData.Size() < sizeof( fileformat::IndexHeader ) )
	{
		RED_LOG_ERROR( "Archive Index is invalid" );
		return LoadingResult::INVALID_ARCHIVE;
	}

	const auto& header = indexData.As< const fileformat::IndexHeader >();

	if ( header.fileTableSize != 0 )
	{
		if ( header.fileTableOffset + header.fileTableSize > indexData.Size() )
		{
			RED_LOG_ERROR( "File Table is not located in a valid position in the index" );
			return LoadingResult::INVALID_ARCHIVE;
		}

		const red::BlobView fileTableBlob = indexData.Range( header.fileTableOffset, header.fileTableSize );
		auto result = m_fileTable.Load( fileTableBlob, archivesVersion, existingResources );
		if ( result != LoadingResult::SUCCESSFUL )
		{
			return result;
		}
	}
	else
	{
		RED_LOG_ERROR( "Instance is possibly truncated, no file table found!" );
		return LoadingResult::INVALID_ARCHIVE;
	}

#ifndef RED_READONLY_ARCHIVES
	if ( m_openMode == OpenMode::INSPECTION && m_inspector )
	{
		m_inspector->Push( header );
	}
#endif

	return LoadingResult::SUCCESSFUL;
}

bool Instance::FindFileClosestToOffset( const FileID& id, Uint64 offset, FileMetadata& result ) const
{
	return m_fileTable.FindClosest( id.GetCode(), offset, result );
}

#ifndef RED_READONLY_ARCHIVES

SavingResult Instance::Save()
{
	RED_FATAL_ASSERT( OpenModePermitsWriting( m_openMode ), "Cannot save an archive when not opened for writing" );

	// All file data should be in the file, just need to write the index and the main header

	if ( m_version != fileformat::Version )
	{
		RED_LOG_ERROR( "Archive needs upgrading to the latest version!" );
		return SavingResult::INVALID_VERSION;
	}

	// Align index to multiple of global alignment so that we can read it easily
	m_writer.Seek( m_fileDataEndPosition );
	utils::WritePaddingBytes( m_writer, fileformat::GlobalAlignment );

	fileformat::MainHeader header;
	header.magic = fileformat::MainHeader::MAGIC_VALUE;
	header.version = fileformat::Version;
	header.indexPosition = m_writer.Tell();
	header.indexSize = 0;
	header.debugPosition = 0ull;
	header.debugSize = 0;
	header.totalFileSize = 0ull;

	RED_FATAL_ASSERT( ( header.indexPosition % fileformat::GlobalAlignment ) == 0,
		"Index should be saved directly after file data, and therefore aligned as another file would be" );

	auto result = SaveIndex( m_writer, header.indexPosition );
	if ( result != SavingResult::SUCCESSFUL )
	{
		return result;
	}

	header.indexSize = static_cast< Uint32 >( m_writer.Tell() - header.indexPosition );

	utils::WritePaddingBytes( m_writer, fileformat::GlobalAlignment ); // Write padding after index

	RED_FATAL_ASSERT( ( m_writer.Tell() % fileformat::GlobalAlignment ) == 0,
		"File should be a aligned when completely written out" );

	// Truncate the archive at this point, the index should be the last thing in the file
	m_writer.SetEndOfFile();

	const Uint64 filePosition = m_writer.Tell();
	const Uint64 fileSize = m_writer.Size();
	RED_FATAL_ASSERT( filePosition == fileSize, "File position and size must match up at this point" );

	header.totalFileSize = m_writer.Size();
	if ( !m_writer.Write( &header, 0ull, sizeof(fileformat::MainHeader) ) )
	{
		RED_LOG_ERROR( "Error writing the main header to file" );
		return SavingResult::ERROR_IO;
	}

	// TODO: Figure these details out somehow
	// TODO: Pass in which machine was building this 
	fileformat::DebugContent debug;
	debug.buildMachine = fileformat::BuildMachine::Unknown;
	red::Memset( debug.computerName, 0, fileformat::DebugContent::COMPUTER_NAME_LENGTH );
	red::Memset( debug.buildName, 0, fileformat::DebugContent::BUILD_NAME_LENGTH );

	if ( !m_writer.Write( &debug, sizeof( fileformat::DebugContent ) ) )
	{
		RED_LOG_ERROR( "Error writing debug content to file" );
		return SavingResult::ERROR_IO;
	}

	return SavingResult::SUCCESSFUL;
}

SavingResult Instance::SaveIndex( FileWriter& writer, const Uint64 headerPosition )
{
	RED_FATAL_ASSERT( OpenModePermitsWriting( m_openMode ), "Cannot save an index when not opened for writing" );

	RED_FATAL_ASSERT( ( headerPosition % fileformat::GlobalAlignment ) == 0,
		"Writer needs to be aligned to a multiple of %u to write the Index!",
		fileformat::GlobalAlignment );

	// Skip past the index header to write the tables first
	writer.Seek( headerPosition + sizeof(fileformat::IndexHeader) );

	fileformat::IndexHeader header;

	header.fileTableOffset = static_cast<Uint32>( writer.Tell() - headerPosition );
	header.fileTableSize = 0;

	if ( !m_fileTable.Empty() )
	{
		auto result = m_fileTable.Save( writer );
		if ( result != SavingResult::SUCCESSFUL )
		{
			return result;
		}

		header.fileTableSize = static_cast<Uint32>( writer.Tell() - headerPosition ) - header.fileTableOffset;
	}
	else
	{
		RED_LOG_ERROR( "Nothing to save as the archive is empty!" );
		return SavingResult::EMPTY_ARCHIVE;
	}

	// Save final position so we can set the file position to it at the end
	const Uint64 finalPosition = writer.Tell();

	if ( !writer.Write( &header, headerPosition, sizeof(fileformat::IndexHeader) ) )
	{
		RED_LOG_ERROR( "Error writing the archive index header to file" );
		return SavingResult::ERROR_IO;
	}

	writer.Seek( finalPosition );

	return SavingResult::SUCCESSFUL;
}

void Instance::ClearHeader()
{
	RED_FATAL_ASSERT( OpenModePermitsWriting( m_openMode ), "Cannot clear the header when not opened for writing" );

	const Uint64 pos = m_writer.Tell();

	fileformat::MainHeader header;
	red::Memzero( &header, sizeof( fileformat::MainHeader ) );
	m_writer.Write( &header, 0ull, sizeof( fileformat::MainHeader ) );
	m_writer.Flush();

	m_writer.Seek( pos );
}

void Instance::Clear()
{
	m_fileTable.Clear();

	m_fileDataStartPosition = m_fileDataEndPosition = utils::InitialFileDataPosition();
}

void Instance::Reserve( Uint32 filesCount )
{
	m_fileTable.Reserve( filesCount );
}

bool Instance::WriteFileSegment( red::BlobView data, FileSegment& segmentInfo, Uint32 alignStart, Uint32 alignSize )
{
	RED_FATAL_ASSERT( OpenModePermitsWriting( m_openMode ), "Invalid open mode for adding new files" );

	FileSpan span;
	span.position = red::AlignUp( m_fileDataEndPosition, alignStart );
	span.size = red::AlignUp( data.Size(), alignSize );

	if ( m_fileDataEndPosition < span.position )
	{
		utils::WritePaddingBytes( m_writer, static_cast<Uint32>( span.position - m_fileDataEndPosition ) );
	}

	if ( m_writer.Write( data.Data(), span.position, data.Size() ) )
	{
		if ( data.Size() != span.size )
		{
			utils::WritePaddingBytes( m_writer, span.size - data.Size() );
		}
		m_fileDataEndPosition = span.position + span.size;

		segmentInfo.position = span.position;

		return true;
	}

	return false;
}

void Instance::AddFileToIndex( FileMetadata& metadata )
{
	RED_FATAL_ASSERT( OpenModePermitsWriting( m_openMode ), "Invalid open mode for adding new files" );

	m_fileTable.Add( metadata );
}


const Inspector& Instance::GetInspector() const
{
	RED_FATAL_ASSERT( m_openMode == OpenMode::INSPECTION, "Can only access the inspector object when archive is opened for inspection." );
	return *m_inspector;
}

#endif // RED_READONLY_ARCHIVES

red::ArraySpan< const FileMetadata > Instance::AllFiles() const
{
	return m_fileTable.GetFileList();
}

} // namespace archive
