/*
 * Copyright (c) 2017 CD Projekt Red. All Rights Reserved.
 */
#include "build.h"
#include "commandletUtils.h"
#include "../../../common/redFileSystem/include/fileSys.h"
#include "../../../common/redCore/include/commandline.h"
#include "../../../common/resource/include/singleTreeResourceDepot.h"
#include "../../../common/redContainers/include/string/stringUtils.h"

namespace commandlet
{

//------------------------------------------------------------------------------

bool HasResourcePathsCommandLineOption( const red::CommandLine& commandLine )
{
	// other commandlets (like resave cmdlet) perform custom handling of 'glob' parameter, so don't use it here
	return commandLine.HasOption( "all" ) ||
		commandLine.HasOption( "dir" ) ||
		commandLine.HasOption( "file" ) ||
		commandLine.HasOption( "filelist" );
}

red::DynArray< res::ResourcePath > ParseResourcePathsFromCommandLine( const res::IBackendResourceDepot& depot, const red::CommandLine& options, bool mustExist )
{
	return ParseResourcePathsFromCommandLine( depot, options, {}, mustExist );
}

red::DynArray< res::ResourcePath > ParseResourcePathsFromCommandLine( const res::IBackendResourceDepot& depot, const red::CommandLine& options, const red::DynArray< String > &patterns, bool mustExist )
{
	red::DynArray< res::ResourcePath > result;
	if ( options.HasOption( "all" ) )
	{
		result = ScanResourceDepotDirectories( depot, "", patterns );
	}
	else
	{
		if ( options.HasOption( "dir" ) )
		{
			auto directories = options.GetValues( "dir" );
			for ( const auto& dir : directories )
			{
				result.PushBack( ScanResourceDepotDirectories( depot, dir, patterns ) );
			}
		}
		if ( options.HasOption( "file" ) )
		{
			auto files = options.GetValues( "file" );
			for ( const auto& file : files )
			{
				auto resPath = res::ResourcePath::Build( file );
				if ( !mustExist || depot.ResourceExists( resPath ) )
				{
					result.PushBack( resPath );
				}
			}
		}
		if ( options.HasOption( "filelist" ) )
		{
			auto listingFiles = options.GetValues( "filelist" );
			for ( const auto& listingFile : listingFiles )
			{
				auto listingFilePath = red::AbsolutePath::ParseFilePath( listingFile );
				if ( !listingFilePath.Empty() )
				{
					result.PushBack( ReadResourcePathsFromListingFile( listingFilePath ) );
				}
			}
		}
		// other commandlets (like resave cmdlet) perform custom handling of 'glob' parameter, so don't use it here
		red::alg::RemoveDuplicates( result );
	}

	return result;
}

//------------------------------------------------------------------------------

red::DynArray< res::ResourcePath > ScanResourceDepotDirectories( const res::IBackendResourceDepot& depot, const String& dirPath, const char* pattern )
{
	red::DynArray< String > patterns;
	if ( pattern )
	{
		patterns.EmplaceBack( pattern );
	}
	return ScanResourceDepotDirectories( depot, dirPath, patterns );
}

red::DynArray< res::ResourcePath > ScanResourceDepotDirectories( const res::IBackendResourceDepot& depot, const String& dirPath, const red::DynArray< String > &patterns )
{
	red::DynArray< res::ResourcePath > result;

	red::String topLevelSearchPath = dirPath;
	if ( !dirPath.Empty() && !dirPath.EndsWith( "\\" ) )
	{
		topLevelSearchPath += "\\";
	}

	red::DynArray< red::String > searchPaths;
	searchPaths.Reserve( 1024 );
	searchPaths.PushBack( topLevelSearchPath );

	// Create the strings outside of the array so we're not reallocating memory we don't need to every frame
	String filePath{ res::ResourcePath::MAX_LENGTH };
	String searchPath{ res::ResourcePath::MAX_LENGTH };

	red::DynArray< String > files;
	files.Reserve( 1024 );

	red::DynArray< String > subDirs;
	subDirs.Reserve( 128 );

	for ( Uint32 i = 0; i < searchPaths.Size(); ++i )
	{
		files.Clear();
		if ( !patterns.Empty() )
		{
			for ( const String &pattern : patterns )
			{
				depot.FindDepotFiles( searchPaths[ i ], pattern.AsChar(), files );
			}
		}
		else
		{
			depot.FindDepotFiles( searchPaths[ i ], nullptr, files );
		}

		for ( const auto& file : files )
		{
			filePath.Clear();
			red::StrAppend( filePath, searchPaths[ i ], file );

			if ( filePath.Length() >= res::ResourcePath::MAX_LENGTH )
			{
				RED_LOG_ERROR( "Ignoring file '%hs' as it exceeds the maximum permitted length of %u characters (%u)", filePath.AsChar(), res::ResourcePath::MAX_LENGTH, filePath.Length() );
				continue;
			}

			result.PushBack( res::ResourcePath::Build( filePath ) );
		}

		subDirs.Clear();
		depot.FindDepotSubDirectories( searchPaths[ i ], subDirs );
		for ( const auto& subDir : subDirs )
		{
			searchPath.Clear();
			red::StrAppend( searchPath, searchPaths[ i ], subDir, "\\" );
			searchPaths.PushBack( searchPath );
		}
	}

	return result;
}

//------------------------------------------------------------------------------

StandardOutput::StandardOutput()
	: m_output( stdout )
{
}

StandardOutput::StandardOutput( const red::CommandLine& options )
	: m_output( stdout )
{
	String param;
	if ( options.GetFirstOption( "output", param ) )
	{
		red::AbsolutePath path = red::AbsolutePath::ParseFilePath( param );
		Open( path );
	}
}

StandardOutput::StandardOutput( const red::AbsolutePath& path )
	: m_output( stdout )
{
	Open( path );
}

StandardOutput::~StandardOutput()
{
	Close();
}

StandardOutput::StandardOutput( StandardOutput&& other )
	: m_output( other.m_output )
{
	other.m_output = stdout;
}

StandardOutput& StandardOutput::operator=( StandardOutput&& other )
{
	if ( this != &other )
	{
		Close();
		std::swap( m_output, other.m_output );
	}
	return *this;
}

void StandardOutput::Open( const red::AbsolutePath& path )
{
	FILE* output = ::fopen( path.AsChar(), "w" );
	if ( output != nullptr )
	{
		m_output = output;
	}
}

void StandardOutput::Close()
{
	::fflush( m_output );

	if ( m_output != stdout )
	{
		::fclose( m_output );
		m_output = stdout;
	}
}

StandardOutput::operator FILE* ()
{
	return m_output;
}

//------------------------------------------------------------------------------

BackendDepotHolder::BackendDepotHolder()
	: m_depot( commandlet::GetEditorDepot() )
	, m_loader( GResourceLoader )
	, m_cachePath( GFileManager->GetCacheDirectory() )
{
}

BackendDepotHolder::BackendDepotHolder( const red::CommandLine& options )
	: BackendDepotHolder()
{
	String param;
	if ( options.GetFirstOption( "cookedDepot", param ) )
	{
		auto cookedDepotPath = red::AbsolutePath::ParseDirPath(param);
		InitWithPath(cookedDepotPath);
	}
}

BackendDepotHolder::BackendDepotHolder(const red::AbsolutePath& path)
	: BackendDepotHolder()
{
	InitWithPath(path);
}

BackendDepotHolder::BackendDepotHolder( BackendDepotHolder&& other )
	: m_ownedDepot( std::move( other.m_ownedDepot ) )
	, m_depot( other.m_depot )
	, m_ownedLoader( std::move( other.m_ownedLoader ) )
	, m_loader( other.m_loader )
	, m_cachePath( other.m_cachePath )
{
	other.Reset();
}

BackendDepotHolder::~BackendDepotHolder()
{
	m_depot = nullptr;
	m_loader = nullptr;
}

BackendDepotHolder& BackendDepotHolder::operator=( BackendDepotHolder&& other )
{
	if ( this != &other )
	{
		m_ownedDepot = std::move( other.m_ownedDepot );
		m_depot = other.m_depot;
		m_ownedLoader = std::move( other.m_ownedLoader );
		m_loader = other.m_loader;
		other.Reset();
	}
	return *this;
}

res::IBackendResourceDepot* BackendDepotHolder::GetDepot() const
{
	return m_depot;
}

res::ResourceLoader* BackendDepotHolder::GetLoader() const
{
	return m_loader;
}

const red::AbsolutePath& BackendDepotHolder::GetCachePath() const
{
	return m_cachePath;
}

void BackendDepotHolder::Reset()
{
	m_depot = commandlet::GetEditorDepot();
	m_loader = GResourceLoader;
	m_cachePath = GFileManager->GetCacheDirectory();
}

void BackendDepotHolder::InitWithPath(const red::AbsolutePath& path)
{
	m_ownedDepot = red::CreateUniquePtr< res::prv::SingleTreeResourceDepot >(path);
	m_depot = m_ownedDepot.Get();

	m_ownedLoader = red::CreateUniquePtr< res::ResourceLoader >();
	m_ownedLoader->Initialize(m_depot, false);
	m_loader = m_ownedLoader.Get();

	m_cachePath = red::paths::ParentAbsolutePath(path);
}

//------------------------------------------------------------------------------

namespace helper
{

class PooledStringData
{
public:
	PooledStringData() = default;
	PooledStringData( red::UniqueBuffer&& buffer, red::DynArray< red::StringView >&& strings );
	PooledStringData( const PooledStringData& ) = delete;
	PooledStringData( PooledStringData&& other ) = default;
	~PooledStringData() = default;

	PooledStringData& operator=( const PooledStringData& ) = delete;
	PooledStringData& operator=( PooledStringData&& other ) = default;

	bool Empty() const;

	const red::DynArray< red::StringView >& Strings() const;

private:
	red::DynArray< red::StringView > m_strings;
	red::UniqueBuffer m_buffer;
};

PooledStringData::PooledStringData( red::UniqueBuffer&& buffer, red::DynArray< red::StringView >&& strings )
	: m_strings( std::move( strings ) )
	, m_buffer( std::move( buffer ) )
{
}

bool PooledStringData::Empty() const
{
	return !m_buffer;
}

const red::DynArray< red::StringView >& PooledStringData::Strings() const
{
	return m_strings;
}


PooledStringData ReadFileIntoLines( const red::AbsolutePath& path )
{
	auto inputFile = GFileManager->CreateFileReader( path, FOF_AbsolutePath );
	if ( !inputFile )
	{
		RED_LOG_ERROR( "Error opening the file: '%hs'", path.AsChar() );
		return PooledStringData();
	}

	Uint64 fileSize = inputFile->GetSize();
	if ( fileSize == 0 || fileSize >= std::numeric_limits<Uint32>::max() )
	{
		RED_LOG_ERROR( "File size (%ull) exceeds limits", fileSize );
		return PooledStringData();
	}

	Uint32 fileSize32 = static_cast<Uint32>(fileSize + 1);
	red::UniqueBuffer buffer = red::CreateUniqueBuffer<red::PoolBackend>( fileSize32, 1 );
	inputFile->Serialize( buffer.Get(), fileSize );
	inputFile.Reset();

	char* const bufferPtr = static_cast<char*>( buffer.Get() );
	char* const endPtr = bufferPtr + fileSize;
	*endPtr = '\0';

	char* startPtr = bufferPtr;
	char* curPtr = startPtr;

	red::DynArray< red::StringView > strings;

	while ( curPtr < endPtr )
	{
		if ( *curPtr == '\r' || *curPtr == '\n' || *curPtr == '\0' )
		{
			*curPtr = '\0';

			if ( curPtr > startPtr + 1 )
			{
				strings.EmplaceBack( startPtr, curPtr - startPtr );
			}

			startPtr = curPtr + 1;
		}
		++curPtr;
	}
	if ( curPtr > startPtr + 1 )
	{
		strings.EmplaceBack( startPtr, curPtr - startPtr );
	}

	return PooledStringData{ std::move( buffer ), std::move( strings ) };
}

} // namespace helper

red::DynArray< res::ResourcePath > ReadResourcePathsFromListingFile( const red::AbsolutePath& listingFilePath )
{
	red::DynArray< res::ResourcePath > result;

	helper::PooledStringData stringData = helper::ReadFileIntoLines( listingFilePath );
	if ( !stringData.Empty() )
	{
		result.Reserve( stringData.Strings().Size() );
		for ( const auto& str : stringData.Strings() )
		{
			if ( red::paths::IsAbsolutePath( str ) )
			{
				RED_LOG_ERROR( "File specified in listing file is absolute: %hs", str.Data() );
				continue;
			}

			if ( str.EndsWith( '\\' ) || str.EndsWith( '/' ) )
			{
				RED_LOG_ERROR( "Directory path specified in listing file %hs", str.Data() );
				continue;
			}

			result.PushBack( res::ResourcePath::Build( str ) );
		}
	}

	return result;
}

red::DynArray< red::AbsolutePath > ReadAbsolutePathsFromListingFile( const red::AbsolutePath& listingFilePath )
{
	red::DynArray< red::AbsolutePath > result;

	helper::PooledStringData stringData = helper::ReadFileIntoLines( listingFilePath );
	if ( !stringData.Empty() )
	{
		result.Reserve( stringData.Strings().Size() );
		for ( const auto& str : stringData.Strings() )
		{
			if ( !red::paths::IsAbsolutePath( str ) )
			{
				RED_LOG_ERROR( "File specified in listing file is not absolute: %hs", str.Data() );
				continue;
			}

			if ( str.EndsWith( '\\' ) || str.EndsWith( '/' ) )
			{
				RED_LOG_ERROR( "Directory path specified in listing file %hs", str.Data() );
				continue;
			}

			result.PushBack( red::AbsolutePath::CreateFilePath( str ) );
		}
	}

	return result;
}

red::DynArray< red::String > ReadLinesFromListingFile( const red::AbsolutePath& listingFilePath )
{
	red::DynArray< red::String > result;

	helper::PooledStringData stringData = helper::ReadFileIntoLines( listingFilePath );
	if ( !stringData.Empty() )
	{
		result.Reserve( stringData.Strings().Size() );
		for ( const auto& str : stringData.Strings() )
		{
			result.PushBack( str.ToString() );
		}
	}

	return result;
}

//------------------------------------------------------------------------------

res::ResourceEditorDepot* GetEditorDepot()
{
	return res::GetEditorDepot();
}

void SetEditorDepot( res::ResourceEditorDepot* depot )
{
	res::SetEditorDepot( depot );
}

} // namespace commandlet
