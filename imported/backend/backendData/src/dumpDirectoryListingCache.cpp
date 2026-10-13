/*
 * Copyright (c) 2020 CD Projekt Red. All Rights Reserved.
 */

#include "build.h"

#include "../../../common/core/include/commandlet.h"
#include "../../../common/redCore/include/commandline.h"

#include "../../backendEngine/include/commandletUtils.h"

#include "../../../common/resource/include/directoryListingCache.h"

class DumpDirectoryListingCacheCommandlet : public red::ICommandlet
{
	RTTI_DECLARE_TYPE( DumpDirectoryListingCacheCommandlet );

public:
	DumpDirectoryListingCacheCommandlet();
	~DumpDirectoryListingCacheCommandlet();

	virtual bool Execute( const red::CommandLine& options ) override;
	virtual const char* GetOneLiner() const override;
	virtual void PrintHelp() const override;

private:
	struct Settings;
	bool ParseSettings( const red::CommandLine& commandline, Settings& settings );
};

RTTI_BEGIN_TYPE( DumpDirectoryListingCacheCommandlet )
RTTI_PARENT_TYPE( red::ICommandlet )
RTTI_END_TYPE();

//------------------------------------------------------------------------------

DumpDirectoryListingCacheCommandlet::DumpDirectoryListingCacheCommandlet()
	: ICommandlet( RED_NAME_CONSTEXPR( "dump-DirectoryListingCache" ) )
{
}

DumpDirectoryListingCacheCommandlet::~DumpDirectoryListingCacheCommandlet()
{
}

const char* DumpDirectoryListingCacheCommandlet::GetOneLiner() const
{
	return "Dump the contents of the directory listing cache into a more readable text format";
}

void DumpDirectoryListingCacheCommandlet::PrintHelp() const
{
}

//------------------------------------------------------------------------------
struct DumpDirectoryListingCacheCommandlet::Settings
{
	red::AbsolutePath cachePath;
	red::AbsolutePath outputPath;
};

bool DumpDirectoryListingCacheCommandlet::ParseSettings( const red::CommandLine& commandline, Settings& settings )
{
	red::String param;
	if ( commandline.GetFirstOption( "cache", param ) )
	{
		settings.cachePath = red::AbsolutePath::ParseFilePath( param );
	}
	else
	{
		RED_LOG_ERROR( "DumpDirectoryListingCacheCommandlet: No cache parameter specified" );
		return false;
	}

	if ( commandline.GetFirstOption( "output", param ) )
	{
		settings.outputPath = red::AbsolutePath::ParseFilePath( param );
	}

	return true;
}

//------------------------------------------------------------------------------

bool DumpDirectoryListingCacheCommandlet::Execute( const red::CommandLine& options )
{
	Settings settings;
	if ( !ParseSettings( options, settings ) )
	{
		return false;
	}

	res::prv::DirectoryListingCache cache;
	if ( !cache.Load( settings.cachePath ) )
	{
		RED_LOG_ERROR( "DumpDirectoryListingCacheCommandlet: Failed to load the dependency file hash cache from '%hs'", settings.cachePath.AsChar() );
		return false;
	}

	auto fileStrings = cache.Debug_GetEntireTree();
	if ( fileStrings.Empty() )
	{
		RED_LOG_ERROR( "DumpDirectoryListingCacheCommandlet: File '%hs' is empty", settings.cachePath.AsChar() );
		return true;
	}

	commandlet::StandardOutput output( settings.outputPath );

	for ( const auto& str : fileStrings )
	{
		fputs( str.AsChar(), output );
		fputs( "\n", output );
	}

	return true;
}