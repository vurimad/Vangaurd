/**
* Copyright c 2013-16 CD Projekt Red. All Rights Reserved.
*/

//////////////////////////////////////////////////////////////////////////
// headers
#include "build.h"

#include "../../../common/core/include/commandlet.h"
#include "../../../common/redCore/include/commandline.h"
#include "../../../common/redReflection/include/serializationLoader.h"
#include "../../../common/redReflection/include/dateTime.h"
#include "../../../common/redCore/include/profilerTypes.h"
#include "../../../common/redFileSystem/include/fileVersionList.h"
#include "../../../common/redFileSystem/include/fileSys.h"
#include "../../../common/redSystem/include/stopWatch.h"


using red::DynArray;


/// File version checker
class CFileVersionCommandlet : public red::ICommandlet
{
	RTTI_DECLARE_TYPE( CFileVersionCommandlet );

public:
	CFileVersionCommandlet();

	// ICommandlet interface
	virtual const char* GetOneLiner() const override;
	virtual bool Execute( const red::CommandLine& options ) override;
	virtual void PrintHelp() const override;

private:
	Bool ProcessFile( const red::AbsolutePath& file, const Bool verbose, Uint32& outVersion );

	struct HistogramEntry
	{
		Uint32		m_count;
		CDateTime	m_oldest;
		CDateTime	m_newest;

		HistogramEntry()
			: m_count(0)
		{}

		void Add( const CDateTime& time )
		{
			if ( time != CDateTime::INVALID )
			{
				if ( m_count == 0 )
				{
					m_oldest = time;
					m_newest = time;
				}
				else
				{
					if ( time < m_oldest )
						m_oldest = time;
					if ( !(time < m_newest) )
						m_newest = time;
				}

				m_count += 1;
			}
		}
	};

	typedef DynArray< HistogramEntry >		THistogram;

	Uint32					m_minVersion;
	THistogram				m_versionHistogram;	
	Uint32					m_numResources;
	Uint32					m_numEmpty;
	Uint32					m_numUnableToOpen;
	Uint32					m_numNotResources;
	Uint32					m_numInvalidVersion;
};

RTTI_BEGIN_TYPE( CFileVersionCommandlet )
	RTTI_PARENT_TYPE( ICommandlet )
RTTI_END_TYPE()


CFileVersionCommandlet::CFileVersionCommandlet()
	: ICommandlet( RED_NAME_CONSTEXPR( "filever" ) )
{
}

bool CFileVersionCommandlet::Execute( const red::CommandLine& options )
{
	// min version
	m_minVersion = VER_MINIMAL;
	{
		String minVersionStr;
		if ( options.GetFirstParam( "min", minVersionStr ) )
		{
			if ( !FromString( minVersionStr.AsChar(), m_minVersion ) )
			{
				RED_LOG_ERROR( "WCC: Unable to parse version number");
				return false;
			}
		}
	}

	// verbose mode
	const Bool verbose = options.HasOption( "verbose" );

	// histogram mode
	const Bool histogram = options.HasOption( "histogram" );

	// tested extensions
	DynArray< String > allowedExtensions;
	if ( options.HasOption( "ext" ) )
	{
		const auto list = options.GetValues( "ext" );
		for ( auto it = list.Begin(); it != list.End(); ++it )
		{
			allowedExtensions.PushBack( String( *it ).ToLower() );
		}
	}

	// tested files
	DynArray< red::AbsolutePath > customFiles;
	if ( options.HasOption( "file" ) )
	{
		const auto list = options.GetValues( "file" );
		for ( auto it = list.Begin(); it != list.End(); ++it )
		{
			String lowerPath = String( *it ).ToLower();
			customFiles.PushBack( red::AbsolutePath::CreateFilePath( lowerPath ) );
		}
	}

	// tested dirs
	DynArray< red::AbsolutePath > customDirs;
	if ( options.HasOption( "dir" ) )
	{
		const auto list = options.GetValues( "dir" );
		for ( auto it = list.Begin(); it != list.End(); ++it )
		{
			String lowerPath = String( *it ).ToLower();
			customDirs.PushBack( red::AbsolutePath::CreateDirPath( lowerPath ) );
		}
	}

	// initialize version histogram
	m_versionHistogram.Resize( VER_CURRENT+1 );

	// reset
	m_numNotResources = 0;
	m_numInvalidVersion = 0;
	m_numResources = 0;
	m_numEmpty = 0;
	m_numUnableToOpen = 0;

	// enumerate depot
	if ( !customFiles.Empty() )
	{
		for ( const auto& path : customFiles )
		{
			// Process files
			Uint32 version = 0;
			if ( ProcessFile( path, true, version ) )
			{
				RED_LOG( "WCC: Version of file '%hs' = %d", path.AsChar(), version );
			}
		}

		return true;
	}

	// non selective mode - process all
	if ( customFiles.Empty() )
	{
		red::StopWatch timer;

		for ( const auto& path : customDirs )
		{
			RED_LOG( "WCC: Scanning directory '%hs'...", path.AsChar() );

			DynArray< red::AbsolutePath > allFiles;
			GFileManager->FindFiles( path, "*.*", allFiles, true );

			RED_LOG( "WCC: Directory enumerated in %1.2fs", timer.GetDelta() );
			RED_LOG( "WCC: Found %d files in directory", allFiles.Size() );

			for ( const auto& filePath : allFiles )
			{
				Uint32 version = 0;
				ProcessFile( filePath, verbose, version );
			}
		}
	}

	// display summary
	RED_LOG( "WCC: Summary stats:" );
	RED_LOG( "WCC:   %d: files not opened", m_numUnableToOpen );
	RED_LOG( "WCC:   %d: empty files", m_numEmpty );
	RED_LOG( "WCC:   %d: non resource files", m_numNotResources );
	RED_LOG( "WCC:   %d: deprecated resources", m_numInvalidVersion );
	RED_LOG( "WCC:   %d: valid resources", m_numResources );

	// display histogram
	if ( histogram )
	{
		CDateTime totalOldest( CDateTime::INVALID );
		CDateTime totalNewest( CDateTime::INVALID );

		RED_LOG( "WCC: Version histogram:" );
		for ( Uint32 i=0; i<m_versionHistogram.Size(); ++i )
		{
			const HistogramEntry& ver = m_versionHistogram[i];

			if ( ver.m_count > 0 )
			{
				RED_LOG( "WCC:   Version %d: %d files", i, ver.m_count );
				RED_LOG( "WCC:      Oldest: %hs", ::ToStringDirect( ver.m_oldest ).AsChar() );
				RED_LOG( "WCC:      Newest: %hs", ::ToStringDirect( ver.m_newest ).AsChar() );

				if ( totalOldest == CDateTime::INVALID )
				{
					totalOldest = ver.m_oldest;
					totalNewest = ver.m_newest;
				}
				else
				{
					if ( ver.m_oldest < totalOldest )
						totalOldest = ver.m_oldest;

					if ( !(ver.m_newest < totalNewest) )
						totalNewest = ver.m_newest;
				}
			}
		}

		RED_LOG( "WCC: Total time span:" );
		RED_LOG( "WCC:      Oldest file: %hs", ::ToStringDirect( totalOldest ).AsChar() );
		RED_LOG( "WCC:      Newest file: %hs", ::ToStringDirect( totalNewest ).AsChar() );
	}

	return true;
}

Bool CFileVersionCommandlet::ProcessFile( const red::AbsolutePath& path, const Bool verbose, Uint32& outVersion )
{
	// create reader
	red::SharedPtr<IFile> reader( GFileManager->CreateFileReader( path, FOF_AbsolutePath | FOF_Buffered ) );
	if ( !reader )
	{
		if ( verbose )
		{
			RED_LOG_ERROR( "WCC: Unable to open '%hs'", path.AsChar() );
		}

		m_numUnableToOpen += 1;
		return false;
	}

	// empty file
	if ( reader->GetSize() == 0 )
	{
		if ( verbose )
		{
			RED_LOG_ERROR( "WCC: Empty file '%hs'", path.AsChar() );
		}

		m_numEmpty += 1;
		return false;
	}

	// read file header
	struct RawFileHeader
	{
		Uint32		m_magic;
		Uint32		m_version;
	};
	RawFileHeader header;
	red::Memzero( &header, sizeof(header) );
	reader->Serialize( &header, sizeof(header) );

	// resource ?
	if ( header.m_magic != 'W2RC' )
	{
		m_numNotResources += 1;
		return false;
	}

	// check version
	if ( header.m_version > VER_CURRENT )
	{
		// that's highly unusual - print it regardless of the settings
		RED_LOG_ERROR( "WCC: File '%hs' has version %d that is outside current range", 
			path.AsChar(), header.m_version );
		return false;
	}

	// get file time of the file
	const CDateTime fileTime( GFileManager->GetFileTime( path ) );

	// to old
	if ( header.m_version < m_minVersion )
	{
		RED_LOG_ERROR( "WCC: File '%hs' is in deprecated version %d", 
			path.AsChar(), header.m_version );

		m_numInvalidVersion += 1;
		m_versionHistogram[ header.m_version ].Add( fileTime );
		return false;
	}

	// update version histogram
	m_versionHistogram[ header.m_version ].Add( fileTime );
	m_numResources += 1;
	return true;
}

const char* CFileVersionCommandlet::GetOneLiner() const
{
	return "Check file versions";
}

void CFileVersionCommandlet::PrintHelp() const
{
	RED_LOG( "WCC: Usage: "  );
	RED_LOG( "WCC:   filever [-ext=extensionlist] [-file=file] [-min=version] [-verbose] [-histogram]"  );
	RED_LOG( "WCC: "  );
	RED_LOG( "WCC: Optional parameters:"  );
	RED_LOG( "WCC:   -ext - Check only files with give extensions" );
	RED_LOG( "WCC:   -file - Check only given files"  );
	RED_LOG( "WCC:   -min - Treat any file with version lower than given number as error"  );
	RED_LOG( "WCC:   -verbose - Print all invalid files"  );
	RED_LOG( "WCC:   -histogram - Print version histogram"  );
}