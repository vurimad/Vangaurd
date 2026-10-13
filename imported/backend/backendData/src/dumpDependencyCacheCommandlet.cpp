/*
* Copyright (c) 2020 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"

#include "../../../common/core/include/commandlet.h"
#include "../../../common/redCore/include/commandline.h"

#include "../../backendEngine/include/commandletUtils.h"

#include "dependencyCache.h"

class DumpDependencyCacheCommandlet : public red::ICommandlet
{
	RTTI_DECLARE_TYPE( DumpDependencyCacheCommandlet );

public:
	DumpDependencyCacheCommandlet();
	~DumpDependencyCacheCommandlet();

	virtual bool Execute( const red::CommandLine& options ) override;
	virtual const char* GetOneLiner() const override;
	virtual void PrintHelp() const override;

private:
	struct Settings;
	bool ParseSettings( const red::CommandLine& commandline, Settings& settings );
};

RTTI_BEGIN_TYPE( DumpDependencyCacheCommandlet )
RTTI_PARENT_TYPE( red::ICommandlet )
RTTI_END_TYPE();

//------------------------------------------------------------------------------

DumpDependencyCacheCommandlet::DumpDependencyCacheCommandlet()
	: ICommandlet( RED_NAME_CONSTEXPR( "dump-DependencyCache" ) )
{
}

DumpDependencyCacheCommandlet::~DumpDependencyCacheCommandlet()
{
}

const char* DumpDependencyCacheCommandlet::GetOneLiner() const
{
	return "Dump the contents of the dependency hash cache into a more readable text format";
}

void DumpDependencyCacheCommandlet::PrintHelp() const
{
}

//------------------------------------------------------------------------------
struct DumpDependencyCacheCommandlet::Settings
{
	red::AbsolutePath cachePath;
	red::AbsolutePath outputPath;
};

bool DumpDependencyCacheCommandlet::ParseSettings( const red::CommandLine& commandline, Settings& settings )
{
	red::String param;
	if ( commandline.GetFirstOption( "cache", param ) )
	{
		settings.cachePath = red::AbsolutePath::ParseFilePath( param );
	}
	else
	{
		RED_LOG_ERROR( "DumpDependencyCacheCommandlet: No cache parameter specified" );
		return false;
	}

	if ( commandline.GetFirstOption( "output", param ) )
	{
		settings.outputPath = red::AbsolutePath::ParseFilePath( param );
	}

	return true;
}

//------------------------------------------------------------------------------

bool DumpDependencyCacheCommandlet::Execute( const red::CommandLine& options )
{
	Settings settings;
	if ( !ParseSettings( options, settings ) )
	{
		return false;
	}

	dependency::Cache cache( commandlet::GetEditorDepot() ); // NOTE: Not actually using this depot but need to specify something
	if ( !cache.Load( settings.cachePath ) )
	{
		RED_LOG_ERROR( "DumpDependencyCacheCommandlet: Failed to load the dependency file hash cache from '%hs'", settings.cachePath.AsChar() );
		return false;
	}

	auto fileEntries = cache.Interal_GetEntries();
	if ( fileEntries.Empty() )
	{
		RED_LOG_ERROR( "DumpDependencyCacheCommandlet: File '%hs' is empty", settings.cachePath.AsChar() );
		return true;
	}

	std::sort( fileEntries.Begin(), fileEntries.End(), []( const dependency::FileInfo* left, const dependency::FileInfo* right )
	{
		return left->path < right->path;
	} );

	commandlet::StandardOutput output( settings.outputPath );

	fprintf( output, "Path Hash\tTimestamp\tHash\tPath String\n" );
	for ( const auto& entry : fileEntries )
	{
		if ( entry != nullptr )
		{
			fprintf( output, "%016llX\t%llu\t%hs\t%hs\n", entry->path.GetHash(), entry->timestamp, dependency::Hash::ToEncodedString( entry->hash ).AsChar(), entry->path.ToDebugString() );
		}
	}

	return true;
}