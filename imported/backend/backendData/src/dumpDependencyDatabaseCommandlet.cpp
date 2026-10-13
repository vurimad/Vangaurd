/*
* Copyright (c) 2020 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"

#include "../../../common/core/include/commandlet.h"
#include "../../../common/redCore/include/commandline.h"

#include "../../backendEngine/include/commandletUtils.h"

#include "dependencyBinaryDatabase.h"

class DumpDependencyDatabaseCommandlet : public red::ICommandlet
{
	RTTI_DECLARE_TYPE( DumpDependencyDatabaseCommandlet );

public:
	DumpDependencyDatabaseCommandlet();
	~DumpDependencyDatabaseCommandlet();

	virtual bool Execute( const red::CommandLine& options ) override;
	virtual const char* GetOneLiner() const override;
	virtual void PrintHelp() const override;

private:
	struct Settings;
	bool ParseSettings( const red::CommandLine& commandline, Settings& settings );
};

RTTI_BEGIN_TYPE( DumpDependencyDatabaseCommandlet )
RTTI_PARENT_TYPE( red::ICommandlet )
RTTI_END_TYPE();

//------------------------------------------------------------------------------

DumpDependencyDatabaseCommandlet::DumpDependencyDatabaseCommandlet()
	: ICommandlet( RED_NAME_CONSTEXPR( "dump-DependencyDatabase" ) )
{
}

DumpDependencyDatabaseCommandlet::~DumpDependencyDatabaseCommandlet()
{
}

const char* DumpDependencyDatabaseCommandlet::GetOneLiner() const
{
	return "Dump the summary of a binary dependency database into text";
}

void DumpDependencyDatabaseCommandlet::PrintHelp() const
{
}

//------------------------------------------------------------------------------
struct DumpDependencyDatabaseCommandlet::Settings
{
	red::AbsolutePath databasePath;
	red::AbsolutePath outputPath;
};

bool DumpDependencyDatabaseCommandlet::ParseSettings( const red::CommandLine& commandline, Settings& settings )
{
	red::String param;
	if ( commandline.GetFirstOption( "database", param ) )
	{
		settings.databasePath = red::AbsolutePath::ParseFilePath( param );
	}
	else
	{
		RED_LOG_ERROR( "DumpDependencyDatabaseCommandlet: No database parameter specified" );
		return false;
	}

	if ( commandline.GetFirstOption( "output", param ) )
	{
		settings.outputPath = red::AbsolutePath::ParseFilePath( param );
	}

	return true;
}

//------------------------------------------------------------------------------

bool DumpDependencyDatabaseCommandlet::Execute( const red::CommandLine& options )
{
	Settings settings;
	if ( !ParseSettings( options, settings ) )
	{
		return false;
	}

	dependency::BinaryDatabase database( settings.databasePath, 0 );
	if ( !database.Load() )
	{
		RED_LOG_ERROR( "DumpDependencyDatabaseCommandlet: Failed to load the dependency binary database '%hs'", settings.databasePath.AsChar() );
		return false;
	}

	auto entries = database.GetAllResourceInfos();
	std::sort( entries.Begin(), entries.End(), []( const dependency::ResourceInfo* left, const dependency::ResourceInfo* right )
	{
		return left->GetPath() < right->GetPath();
	} );

	commandlet::StandardOutput output( settings.outputPath );

	fprintf( output, "Path Hash\tTimestamp\tHash\tPath String\n" );
	for ( const auto& entry : entries )
	{
		if ( entry != nullptr )
		{
			fprintf( output, "%016llX\t%llu\t%hs\t%hs\n", entry->GetPath().GetHash(), entry->GetTimeStamp(), dependency::Hash::ToEncodedString( entry->GetHash() ).AsChar(), entry->GetPath().ToDebugString() );
		}
	}

	return true;
}