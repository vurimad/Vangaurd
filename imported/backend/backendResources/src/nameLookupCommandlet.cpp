/*
 * Copyright (c) 2018 CD Projekt Red. All Rights Reserved.
 */
#include "build.h"

#include "../../backendEngine/include/commandletEngineInstance.h"
#include "../../backendEngine/include/commandletUtils.h"
#include "../../backendEngine/include/resourceUtils.h"

#include "../../../common/resource/include/resourceEditorDepotImpl.h"
#include "../../../common/core/include/commandlet.h"
#include "../../../common/redCore/include/commandline.h"
#include "../../../common/redContainers/include/fundamentalStringParser.h"
#include "../../../common/redSystem/include/stopWatch.h"

class NameLookupCommandlet : public red::ICommandlet
{
	RTTI_DECLARE_TYPE( NameLookupCommandlet );

public:
	NameLookupCommandlet();

	virtual const char* GetOneLiner() const override;
	virtual void PrintHelp() const override;
	virtual bool Execute( const red::CommandLine& options ) override;

	virtual Bool InitializeEnvironment() override;
	virtual void TeardownEnvironment() override;

private:
	void LoadResources();

	commandlet::EngineInstance m_engineInstance;
	Bool m_loadResources = false;
};

RTTI_BEGIN_TYPE( NameLookupCommandlet )
	RTTI_PARENT_TYPE( red::ICommandlet )
RTTI_END_TYPE()

//------------------------------------------------------------------------------

NameLookupCommandlet::NameLookupCommandlet()
	: ICommandlet( RED_NAME_CONSTEXPR( "name-lookup" ) )
{
}

const char* NameLookupCommandlet::GetOneLiner() const
{
	return "Lookup CName by its hash";
}

void NameLookupCommandlet::PrintHelp() const
{
}

Bool NameLookupCommandlet::InitializeEnvironment()
{
	return m_engineInstance.Initialise();
}

void NameLookupCommandlet::TeardownEnvironment()
{
	m_engineInstance.Shutdown();
}

void NameLookupCommandlet::LoadResources()
{
	RED_LOG_ERROR( "NameLookupCommandlet: Loading all the resources..." );

	auto* depot = commandlet::GetEditorDepot();
	red::DynArray< res::ResourcePath > resources = commandlet::ScanResourceDepotDirectories( *depot, "" );

	// filter
	auto iter = std::remove_if( resources.Begin(), resources.End(), IsLinkResource );
	iter = std::remove_if( resources.Begin(), iter, [] ( const res::ResourcePath& path ) -> bool
	{
		auto pathView = path.ToStringView();
		if ( pathView.StartsWith( "user\\" ) || pathView.StartsWith( "test\\" ) || pathView.StartsWith( "temp\\" ) ) return true;
		if ( IsJsonResource( path ) ) return false;
		if ( IsTextResource( path ) ) return false;
		if ( IsBinaryResource( path ) ) return false;
		if ( path.ToStringView().EndsWith( ".bk2" ) ) return true;
		if ( path.ToStringView().EndsWith( ".streamingsector" ) || path.ToStringView().EndsWith( ".streamingarea" ) ) return true;
		return true;
	} );
	resources.Remove( iter, resources.End() );

	RED_LOG_ERROR( "NameLookupCommandlet: Loading %u resources...", resources.Size() );

	red::StopWatch sw;

	job::Counter loadingCounter{ job::Priority::Latent };

	for ( const res::ResourcePath& path : resources )
	{
		res::IssueLoadingRequestParameter param{ path };
		param.postLoadFlags.skipUploadRenderData = true;
		param.skipBuffers = true;
		param.skipPostLoad = true;
		param.skipImports = true;
		auto token = GResourceLoader->IssueLoadingRequest( param );
		loadingCounter += token->GetWaitCounter();
	}

	job::FlushCounter(loadingCounter);

	RED_LOG_INFO( "NameLookupCommandlet: Loading %u resources took %.2f seconds", resources.Size(), sw.GetDelta() );
}

bool NameLookupCommandlet::Execute( const red::CommandLine& commandLine )
{
	String param;

	if ( !commandLine.GetFirstOption( "hash", param ) )
	{
		RED_LOG_ERROR( "NameLookupCommandlet: Missing 'hash' commandline argument" );
		return false;
	}

	const Bool loadAllResources = commandLine.HasOption( "loadResources" );
	if ( loadAllResources )
	{
		LoadResources();
	}

	const AnsiChar* paramStr = param.AsChar();

	Bool hashParsed = false;
	Uint64 hash = 0;
	if ( param.Length() > 2 && ( param.BeginsWith( "0x" ) || param.BeginsWith( "0X" ) ) )
	{
		const AnsiChar* hexStr = paramStr + 2;
		if ( GParseHex( hexStr, hash ) )
		{
			hashParsed = true;
		}
	}

	if ( !hashParsed )
	{
		if ( GParseUint64( paramStr, hash ) )
		{
			hashParsed = true;
		}
	}

	if ( !hashParsed )
	{
		RED_LOG_ERROR( "NameLookupCommandlet: Failed to parse name hash" );
		return false;
	}

	String result;

	const CName foundName( hash );
	if ( foundName == RED_NAME( foundName ) )
	{
		result = String::Printf("Found 0x%016llX -> '%hs'", hash, foundName.AsChar() );
	}
	else
	{
		result = String::Printf( "Could not find name that matches hash 0x%016llX.", hash );

		if ( !loadAllResources )
		{
			result += " Try running again with -loadResources switch.";
		}
	}

	printf( "NameLookupCommandlet: %hs\n", result.AsChar() );
	RED_LOG_INFO( "NameLookupCommandlet: %hs", result.AsChar() );

	return true;
}
