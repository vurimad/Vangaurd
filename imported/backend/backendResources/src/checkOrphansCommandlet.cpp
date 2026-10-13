/**
 * Copyright (c) 2019 CD Projekt Red. All Rights Reserved.
 */

#include "build.h"

#include "../../../common/core/include/commandlet.h"

#include "../../../common/resource/include/resourceEditorDepotImpl.h"
#include "../../../common/redCore/include/commandline.h"
#include "../../../common/redFileSystem/include/fileSys.h"
#include "../../backendEngine/include/commandletEngineInstance.h"
#include "../../backendData/include/resourceDatabase.h"
#include "../../backendData/include/dataTypes.h"
#include "../../backendEngine/include/commandletUtils.h"
#include "../../backendEngine/include/resourceUtils.h"

class CheckOrphansCommandlet : public red::ICommandlet
{
	RTTI_DECLARE_TYPE( CheckOrphansCommandlet );

public:
	CheckOrphansCommandlet();
	virtual ~CheckOrphansCommandlet() = default;

	// ICommandlet interface
	virtual Bool InitializeEnvironment() override;
	virtual void TeardownEnvironment() override;
	virtual void PrintHelp() const override;
	virtual const char* GetOneLiner() const override;
	virtual Bool Execute( const red::CommandLine& options ) override;

private:
	void FilterResourcePaths( red::DynArray< res::ResourcePath >& paths );
	Bool ParseSettings( const red::CommandLine& options );
	Bool IsTargetResource( const res::ResourcePath &path ) const;

	struct Settings
	{
		red::DynArray< res::ResourcePath > resources;
		String extension;
		red::AbsolutePath outputPath;
		red::AbsolutePath generateFileListPath;
		Bool verbose{};
		Bool generateFileListOnly{};
	};

	Settings m_settings{};
	commandlet::EngineInstance m_engineInstance;
	red::UniquePtr< resdb::Database > m_ownedSourceDepotDatabase;
	res::ResourceEditorDepot* m_sourceDepot{};
	resdb::Database* m_sourceDepotDatabase{};
};

RTTI_BEGIN_TYPE( CheckOrphansCommandlet )
	RTTI_PARENT_TYPE( red::ICommandlet )
RTTI_END_TYPE()

CheckOrphansCommandlet::CheckOrphansCommandlet()
	: ICommandlet( RED_NAME_CONSTEXPR( "check-orphans" ) )
{
}

Bool CheckOrphansCommandlet::InitializeEnvironment()
{
	if ( !m_engineInstance.Initialise() )
	{
		return false;
	}

	m_ownedSourceDepotDatabase = red::CreateUniquePtr< resdb::Database >();

	if ( !m_ownedSourceDepotDatabase )
	{
		return false;
	}

	m_sourceDepotDatabase = m_ownedSourceDepotDatabase.Get();
	m_sourceDepot = commandlet::GetEditorDepot();

	if ( !m_sourceDepot )
	{
		return false;
	}

	const red::AbsolutePath& sourceCachePath = GFileManager->GetCacheDirectory();
	const red::AbsolutePath sourceDatabasePath = sourceCachePath.AddFilePath( resdb::Database::GetDefaultFilename() );

	m_ownedSourceDepotDatabase->Initialise( *m_sourceDepot, sourceDatabasePath );

	return true;
}

void CheckOrphansCommandlet::TeardownEnvironment()
{
	m_engineInstance.Shutdown();
}

void CheckOrphansCommandlet::PrintHelp() const
{
	RED_LOG( "Usage:" );
	RED_LOG( "  check-orphans [-all|-file|-dir|-filelist] [-output=<file>] [-extension=<extension>]" );
	RED_LOG( "Parameters:" );
	RED_LOG( "  -all                              - validate all files" );
	RED_LOG( "  -file=<file1> <file2>             - list of files to be validated" );
	RED_LOG( "  -dir=<dir1> <dir2>                - directories to be validated" );
	RED_LOG( "  -filelist=<filelist>              - file that contains list of files to be validated" );
	RED_LOG( "  -generate-filelist=<filename>     - just generate file list from -all/-file/-dir arguments" );
	RED_LOG( "  -extension=<extension>            - filter files list for this extension only" );
	RED_LOG( "  -verbose                          - be verbose" );
	RED_LOG( "  -output=<file>                    - where to save report" );
}

static String FormatBytes( Uint64 numBytes )
{
	constexpr Uint64 KB = 1024;
	constexpr Uint64 MB = KB * 1024;
	constexpr Uint64 GB = MB * 1024;

	if ( numBytes >= GB )
	{
		return String::Printf( "%.2f GB", numBytes / Float( GB ) );
	}
	else if ( numBytes >= MB )
	{
		return String::Printf( "%.2f MB", numBytes / Float( MB ) );
	}
	else if ( numBytes >= KB )
	{
		return String::Printf( "%.2f KB", numBytes / Float( KB ) );
	}
	return String::Printf( "%llu B", numBytes );
}

Bool CheckOrphansCommandlet::Execute( const red::CommandLine& options )
{
	if ( !ParseSettings( options ) )
	{
		RED_LOG( "Failed to parse settings from commandline.." );
		RED_LOG_FLUSH_AND_WAIT();

		return false;
	}

	auto &resources = m_settings.resources;

	if ( m_settings.generateFileListOnly )
	{
		if ( m_settings.verbose )
		{
			RED_LOG_INFO( "Generating filelist (%hc) for %u resources", m_settings.generateFileListPath.AsChar(), resources.Size() );
		}

		GFileManager->CreatePath( m_settings.generateFileListPath );
		FILE *listFile = fopen( m_settings.generateFileListPath.AsChar(), "w" );

		for ( Uint32 i = 0; i < resources.Size(); ++i )
		{
			const res::ResourcePath& resourcePath = resources[ i ];
			fwrite( resourcePath.ToStringView().Data(), resourcePath.ToStringView().Length(), 1, listFile );
			if ( i < resources.Size() - 1 )
			{
				fwrite( "\n", 1, 1, listFile );
			}
		}

		fclose( listFile );

		return true;
	}

	red::DynArray< res::ResourcePath > toDelete{};

	RED_LOG_INFO( "Checking for orphans of %u resources (extension \'%hs\')", m_settings.resources.Size(), m_settings.extension );

	struct StatEntry
	{
		Uint64 total{};
		Uint64 toDelete{};
		Uint64 totalSize{};
		Uint64 toDeleteSize{};
	};
	red::Map< String, StatEntry > stats;

	Uint64 totalSize{};

	for ( Uint32 i = 0; i < resources.Size(); ++i )
	{
		const res::ResourcePath& resourcePath = resources[ i ];

		const auto absolutePath = m_sourceDepot->TranslateResourcePath( resourcePath );
		const String extension = red::paths::GetExtension( absolutePath ).ToString();
		auto &extStat = stats[ extension ];
		const Uint64 fileSize = GFileManager->GetFileSize( absolutePath );
		extStat.total++;
		extStat.totalSize += fileSize;
		totalSize += fileSize;

		if ( m_settings.verbose )
		{
			RED_LOG_INFO( "[%u/%u] Validating %hs", i + 1, resources.Size(), resourcePath.ToDebugString() );
		}

		const auto links = m_ownedSourceDepotDatabase->GetBackwardLinks( resourcePath );

		if ( links.Empty() )
		{
			if ( m_settings.verbose )
			{
				RED_LOG_INFO( " Marking for deletion.." );
			}
			toDelete.PushBack( resourcePath );
		}
	}

	Uint64 deleteSize{};

	if ( !m_settings.outputPath.Empty() )
	{
		GFileManager->CreatePath( m_settings.outputPath );
		FILE *output = fopen( m_settings.outputPath.AsChar(), "w" );

		for ( auto const &path : toDelete )
		{
			const auto absolutePath = m_sourceDepot->TranslateResourcePath( path );
			const String extension = red::paths::GetExtension( absolutePath ).ToString();
			auto &extStat = stats[ extension ];
			const Uint64 fileSize = GFileManager->GetFileSize( absolutePath );
			extStat.toDelete++;
			extStat.toDeleteSize += fileSize;
			deleteSize += fileSize;

			const String str = path.ToDebugString();
			fwrite( str.AsChar(), str.Length(), 1, output );
			fputs( "\n", output );
		}

		fclose( output );
	}

	RED_LOG_INFO( "Validated %u resources (%hs), to delete: %u (%hs)", m_settings.resources.Size(),
		FormatBytes( totalSize ), toDelete.Size(), FormatBytes( deleteSize ) );

	if ( m_settings.extension.Empty() )
	{
		for ( auto const &extStat : stats )
		{
			const String& extension = extStat.Key();
			const StatEntry &stat = extStat.Value();

			if ( stat.toDelete == 0 )
			{
				continue;
			}

			RED_LOG_INFO( " \'%hs\' total: %u (%hs) to delete %u (%hs)",
				extension.AsChar(), stat.total, FormatBytes( stat.totalSize ).AsChar(), stat.toDelete, FormatBytes( stat.toDeleteSize ).AsChar()
			);
		}
	}

	return true;
}

void CheckOrphansCommandlet::FilterResourcePaths( red::DynArray< res::ResourcePath >& paths )
{
	if ( !m_settings.extension.Empty() )
	{
		paths.Remove( std::remove_if( paths.Begin(), paths.End(), [ this ]( const res::ResourcePath& path )
		{
			return !IsTargetResource( path );
		} ), paths.End() );
	}
}

Bool CheckOrphansCommandlet::ParseSettings( const red::CommandLine& options )
{
	m_settings.verbose = options.HasOption( "verbose" );

	String param;
	if ( options.GetFirstOption( "output", param ) )
	{
		m_settings.outputPath = red::AbsolutePath::ParseFilePath( param );
	}

	if ( options.GetFirstOption( "extension", param ) )
	{
		m_settings.extension = param;
	}

	const auto &editorDepot = m_sourceDepot;

	auto &resources = m_settings.resources;
	resources = commandlet::ParseResourcePathsFromCommandLine( *editorDepot, options );

	FilterResourcePaths( resources );
	std::sort( resources.Begin(), resources.End(), res::SortByPathString );

	if ( options.GetFirstOption( "generate-filelist", param ) )
	{
		if ( resources.Empty() && m_settings.verbose )
		{
			RED_LOG_ERROR( "Resources list empty, skipping generate-filelist mode" );
		}

		m_settings.generateFileListOnly = true;
		m_settings.generateFileListPath = red::AbsolutePath::ParseFilePath( param );
	}

	return !resources.Empty();
}

Bool CheckOrphansCommandlet::IsTargetResource( const res::ResourcePath &path ) const
{
	return red::paths::GetExtension( path.ToStringView() ) == m_settings.extension;
}

const char* CheckOrphansCommandlet::GetOneLiner() const
{
	return "Check orphaned files for given extension";
}
