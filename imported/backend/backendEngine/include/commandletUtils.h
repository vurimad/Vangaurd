/*
 * Copyright (c) 2017 CD Projekt Red. All Rights Reserved.
 */
#pragma once

#include "../../../common/resource/include/resourceEditorDepotImpl.h"

namespace red
{
class AbsolutePath;
class CommandLine;
}

namespace commandlet
{

// Returns if there exists a command line option that would be used in ParseResourcePathsFromCommandLine
BACKEND_ENGINE_API bool HasResourcePathsCommandLineOption( const red::CommandLine& commandLine );

// Parses a set of resource paths from the command line in a specific format:
// Individual files in the form of: -file=<file> (<file> ...)
// All the files in a directory given by: -dir=<dir> (<dir> ...)
// All the files in the depot given by: -all
// A specific set of files provided by a text file -filelist=<textfile>
BACKEND_ENGINE_API red::DynArray< res::ResourcePath > ParseResourcePathsFromCommandLine( const res::IBackendResourceDepot& depot, const red::CommandLine& commandLine, bool mustExist = true );
BACKEND_ENGINE_API red::DynArray< res::ResourcePath > ParseResourcePathsFromCommandLine( const res::IBackendResourceDepot& depot, const red::CommandLine& commandLine, const red::DynArray< String > &patterns, bool mustExist = true );

// Scans directories in the depot and returns a list of resources found within them
// This should only be used for commandlets and will not be supported in the game proper
BACKEND_ENGINE_API red::DynArray< res::ResourcePath > ScanResourceDepotDirectories( const res::IBackendResourceDepot& depot, const String& dirPath, const char* pattern = nullptr );
BACKEND_ENGINE_API red::DynArray< res::ResourcePath > ScanResourceDepotDirectories( const res::IBackendResourceDepot& depot, const String& dirPath, const red::DynArray< String > &patterns );


// Helper wrapper class around printinf text to standard out or to a specified file
// By default this will output to standard out
// If -output=<file path> is specified in command line options or the path constructor
// is used it will output to that file instead.
class BACKEND_ENGINE_API StandardOutput
{
public:
	StandardOutput();
	explicit StandardOutput( const red::CommandLine& options );
	explicit StandardOutput( const red::AbsolutePath& path );
	StandardOutput( const StandardOutput& ) = delete;
	StandardOutput( StandardOutput&& other );
	~StandardOutput();

	StandardOutput& operator=( const StandardOutput& ) = delete;
	StandardOutput& operator=( StandardOutput&& other );

	operator FILE*();

private:
	void Open( const red::AbsolutePath& path );
	void Close();

	FILE* m_output;
};


// Helper wrapper class to provide a backend compatible depot which can be either
// the default source depot or an explicit cooked depot specified on the command line
// Use the -cookedDepot=<depot path> to specify using a cooked depot
// By default it uses the standard depot returned by commandlet::GetEditorDepot()
class BACKEND_ENGINE_API BackendDepotHolder
{
public:
	BackendDepotHolder();
	explicit BackendDepotHolder( const red::CommandLine& options );
	explicit BackendDepotHolder( const red::AbsolutePath& path );
	BackendDepotHolder( const BackendDepotHolder& ) = delete;
	BackendDepotHolder( BackendDepotHolder&& other );
	~BackendDepotHolder();

	BackendDepotHolder& operator=( const BackendDepotHolder& ) = delete;
	BackendDepotHolder& operator=( BackendDepotHolder&& other );

	res::IBackendResourceDepot* GetDepot() const;
	res::ResourceLoader* GetLoader() const;

	const red::AbsolutePath& GetCachePath() const;

private:
	void Reset();
	void InitWithPath(const red::AbsolutePath& path);

	red::UniquePtr< res::IBackendResourceDepot > m_ownedDepot;
	res::IBackendResourceDepot* m_depot;

	red::UniquePtr< res::ResourceLoader > m_ownedLoader;
	res::ResourceLoader* m_loader;

	red::AbsolutePath m_cachePath;
};


BACKEND_ENGINE_API red::DynArray< res::ResourcePath > ReadResourcePathsFromListingFile( const red::AbsolutePath& listingFilePath );
BACKEND_ENGINE_API red::DynArray< red::AbsolutePath > ReadAbsolutePathsFromListingFile( const red::AbsolutePath& listingFilePath );
BACKEND_ENGINE_API red::DynArray< red::String > ReadLinesFromListingFile( const red::AbsolutePath& listingFilePath );

BACKEND_ENGINE_API res::ResourceEditorDepot* GetEditorDepot();
BACKEND_ENGINE_API void SetEditorDepot( res::ResourceEditorDepot* depot );

} // namespace commandlet
