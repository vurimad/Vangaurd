/**
 * Copyright c 2016 CD Projekt Red. All Rights Reserved.
 */

#include "build.h"

#include "../../../common/core/include/commandlet.h"
#include "../../../common/redCore/include/commandline.h"
#include "../../../common/redContainers/include/string/stringUtils.h"
#include "../../../common/redFileSystem/include/fileSys.h"

#include "../../../common/resource/include/resourceEditorDepotImpl.h"
#include "../../../common/resource/include/singleTreeResourceDepot.h"

#include "../../backendEngine/include/commandletUtils.h"
#include "../../backendEngine/include/commandletConsole.h"

#include "resourcesCommands.h"

//------------------------------------------------------------------------------

namespace
{
using resources_commands_helper::Hash;
using resources_commands_helper::HashParam;
}

//------------------------------------------------------------------------------

class ResourcesInteractiveCommandlet : public red::ICommandlet 
{
	RTTI_DECLARE_TYPE( ResourcesInteractiveCommandlet );

public:
	ResourcesInteractiveCommandlet();

	// ICommandlet interface
	virtual const char* GetOneLiner() const override;
	virtual bool Execute( const red::CommandLine& options ) override;
	virtual void PrintHelp() const override;

	virtual bool InitializeEnvironment() override;
	virtual void TeardownEnvironment() override;

private:
	using CommandParameters = ResourcesCommands::CommandParameters;

	void ParseCommandLine( CommandParameters& arguments, red::StringView& command, red::StringView& redirect );
	bool ExecuteCommand( red::StringView command, CommandParameters arguments );

	ResourcesCommands m_commands;

	commandlet::InteractiveConsole m_console;
};

RTTI_BEGIN_TYPE( ResourcesInteractiveCommandlet )
	RTTI_PARENT_TYPE( red::ICommandlet )
RTTI_END_TYPE()

//------------------------------------------------------------------------------

ResourcesInteractiveCommandlet::ResourcesInteractiveCommandlet()
	: ICommandlet( RED_NAME_CONSTEXPR( "resource-service" ) )
{
}

const char* ResourcesInteractiveCommandlet::GetOneLiner() const
{
	return "Interactive commandlet to inspect the resource system";
}

void ResourcesInteractiveCommandlet::PrintHelp() const
{
}

bool ResourcesInteractiveCommandlet::InitializeEnvironment()
{
	return m_console.Initialise();
}

void ResourcesInteractiveCommandlet::TeardownEnvironment()
{
	m_console.Shutdown();
}

void ResourcesInteractiveCommandlet::ParseCommandLine( CommandParameters& arguments, red::StringView& command, red::StringView& redirect )
{
	command = arguments.PopFront();

	if ( !arguments.Empty() )
	{
		red::StringView last = arguments.Back();
		if ( last.StartsWith( '>' ) )
		{
			arguments.PopBack();
			last.TrimFront( 1 );
			redirect = last;
		}
		else if ( arguments.Size() >= 2 && arguments[ arguments.Size() - 2 ] == ">" )
		{
			redirect = arguments.Back();
			arguments = arguments.Left( arguments.Size() - 2 );
		}
	}
}

bool ResourcesInteractiveCommandlet::Execute( const red::CommandLine& options )
{
	commandlet::BackendDepotHolder depotHolder{ options };

	auto printFunction = [&console = m_console]( red::StringView str ) { console.PrintOutput( str ); };
	m_commands.Initialise( depotHolder, printFunction );

	bool running = true;
	while ( running )
	{
		m_console.PrintOutput( "\n>" );
		red::StringView commandline = m_console.GetInput();
		red::DynArray< red::StringView > parts = red::StrSplit( commandline, ' ' );
		if ( parts.Empty() )
		{
			continue;
		}

		CommandParameters arguments( parts );
		red::StringView command;
		red::StringView redirect;
		ParseCommandLine( arguments, command, redirect );

		if ( !redirect.Empty() )
		{
			m_console.StartRedirect( redirect );
		}

		running = ExecuteCommand( command, arguments );

		if ( !redirect.Empty() )
		{
			m_console.EndRedirect();
		}
	}

	return true;
}

bool ResourcesInteractiveCommandlet::ExecuteCommand( red::StringView command, CommandParameters arguments )
{
	Uint32 commandHash = HashParam( command );
	switch ( commandHash )
	{
	case Hash( "exit" ):
	case Hash( "quit" ):
		return false;

	case Hash( "help" ):
	case Hash( "?" ):
		// TODO: Make some sort of extra command like layer to this so we can have commands within
		// commandlets without having to reload the database or re-scan the resource system.
		// Might be able to integrate this with the commandlet system somehow
		m_console.PrintOutput( m_commands.CommandsHelpText() );
		break;

	default:
		if ( !m_commands.ExecuteCommand( commandHash, arguments ) )
		{
			m_console.PrintOutput( "Unknown Command\n" );
		}
		break;
	};

	return true;
}

//------------------------------------------------------------------------------

class ResourcesQueryCommandlet : public red::ICommandlet 
{
	RTTI_DECLARE_TYPE( ResourcesQueryCommandlet );

public:
	ResourcesQueryCommandlet();

	// ICommandlet interface
	virtual const char* GetOneLiner() const override;
	virtual bool Execute( const red::CommandLine& options ) override;
	virtual void PrintHelp() const override;

private:
	ResourcesCommands m_commands;
};

RTTI_BEGIN_TYPE( ResourcesQueryCommandlet )
RTTI_PARENT_TYPE( red::ICommandlet )
RTTI_END_TYPE()

//------------------------------------------------------------------------------

ResourcesQueryCommandlet::ResourcesQueryCommandlet()
	: ICommandlet( RED_NAME_CONSTEXPR( "resource-query" ) )
{
}

const char* ResourcesQueryCommandlet::GetOneLiner() const
{
	return "Non Interactive commandlet to inspect the resource system";
}

void ResourcesQueryCommandlet::PrintHelp() const
{
}

bool ResourcesQueryCommandlet::Execute( const red::CommandLine& options )
{
	red::ArraySpan< const red::String > commandlineParts = options.GetValues( "" );
	if ( commandlineParts.Size() == 1 )
	{
		return false;
	}
	RED_FATAL_ASSERT( commandlineParts.Front() == "resource-query" );
	commandlineParts.PopFront(); // Strip off commandlet name

	commandlet::BackendDepotHolder depotHolder{ options };

	auto printFunction = []( red::StringView str ) { printf( "%.*s", str.Length(), str.Data() ); };
	m_commands.Initialise( depotHolder, printFunction );

	red::DynArray< red::String > listingArguments;

	String param;
	if ( options.GetFirstOption( "inputFile", param ) )
	{
		listingArguments = commandlet::ReadLinesFromListingFile( red::AbsolutePath::ParseFilePath( param ) );
	}

	red::DynArray< red::StringView > argumentsArray{ commandlineParts.Size() + listingArguments.Size() };
	auto iter = argumentsArray.Begin();
	iter = std::copy( commandlineParts.begin(), commandlineParts.end(), iter );
	iter = std::copy( listingArguments.Begin(), listingArguments.End(), iter );

	ResourcesCommands::CommandParameters arguments = argumentsArray;
	
	red::StringView command = arguments.PopFront();
	Uint32 commandHash = HashParam( command );

	if ( m_commands.ExecuteCommand( commandHash, arguments ) )
	{
		fflush( stdout );
		return true;
	}
	return false;
}

//------------------------------------------------------------------------------
