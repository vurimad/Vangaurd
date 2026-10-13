/*
* Copyright (c) 2020 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"

#include "../../../common/core/include/commandlet.h"
#include "../../../common/redCore/include/commandline.h"
#include "../../../common/redFileSystem/include/fileSys.h"
#include "../../../common/redCore/include/absolutePath.h"

#include "../../backendEngine/include/commandletUtils.h"

#include "dependencyTypes.h"
#include "dependencyHash.h"

class ComputeDependencyFileInfoCommandlet : public red::ICommandlet
{
	RTTI_DECLARE_TYPE( ComputeDependencyFileInfoCommandlet );

public:
	ComputeDependencyFileInfoCommandlet();
	~ComputeDependencyFileInfoCommandlet();

	virtual bool Execute( const red::CommandLine& options ) override;
	virtual const char* GetOneLiner() const override;
	virtual void PrintHelp() const override;
};

RTTI_BEGIN_TYPE( ComputeDependencyFileInfoCommandlet )
RTTI_PARENT_TYPE( red::ICommandlet )
RTTI_END_TYPE();

//------------------------------------------------------------------------------

ComputeDependencyFileInfoCommandlet::ComputeDependencyFileInfoCommandlet()
	: ICommandlet( RED_NAME_CONSTEXPR( "calcDepsFileInfo" ) )
{
}

ComputeDependencyFileInfoCommandlet::~ComputeDependencyFileInfoCommandlet()
{
}

const char* ComputeDependencyFileInfoCommandlet::GetOneLiner() const
{
	return "Calculates the dependency information of a file on disk";
}

void ComputeDependencyFileInfoCommandlet::PrintHelp() const
{
}

//------------------------------------------------------------------------------


//------------------------------------------------------------------------------

bool ComputeDependencyFileInfoCommandlet::Execute( const red::CommandLine& options )
{
	commandlet::BackendDepotHolder depotHolder( options );
	auto& depot = *depotHolder.GetDepot();

	red::DynArray< res::ResourcePath > resources = 	commandlet::ParseResourcePathsFromCommandLine( depot, options );
	if ( resources.Empty() )
	{
		RED_LOG_ERROR( "ComputeDependencyFileInfoCommandlet: No resources specified" );
		return false;
	}

	red::DynArray< dependency::FileInfo > fileEntries;
	fileEntries.Reserve( resources.Size() );

	for ( const auto& resourcePath : resources )
	{
		red::AbsolutePath absPath = depot.TranslateResourcePath( resourcePath );
		RED_LOG( "ComputeDependencyFileInfoCommandlet: Computing information for '%hs' in '%hs'", resourcePath.ToDebugString(), absPath.AsChar() );

		auto& entry = fileEntries.EmplaceBack();
		entry.path = resourcePath;
		entry.timestamp = 0ull;

		auto file = depot.CreateResourceReader( resourcePath );
		if ( !file )
		{
			RED_LOG_ERROR( "ComputeDependencyFileInfoCommandlet: Failed to open resource '%hs'", resourcePath.ToDebugString() );
			continue;
		}

		red::Blob buffer;
		if ( !red::LoadFileToBuffer( *file, buffer ) )
		{
			RED_LOG_ERROR( "ComputeDependencyFileInfoCommandlet: Failed to load resource '%hs' to buffer", resourcePath.ToDebugString() );
			continue;
		}

		entry.timestamp = depot.GetResourceTimestamp( resourcePath );
		entry.hash = dependency::Hash::Compute( buffer );
	}

	commandlet::StandardOutput output( options );

	fprintf( output, "Path Hash\tTimestamp\tHash\tPath String\n" );
	for ( const auto& entry : fileEntries )
	{
		fprintf( output, "%016llX\t%llu\t%hs\t%hs\n", entry.path.GetHash(), entry.timestamp, dependency::Hash::ToEncodedString( entry.hash ).AsChar(), entry.path.ToDebugString() );
	}

	return true;
}