/**
* Copyright (c) 2010-16 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"
#include "commandlet.h"
#include "../../redSystem/include/stopWatch.h"

namespace red
{

namespace prv
{
static red::UniquePtr< ICommandlet > FindCommandlet( const CName& name )
{
	red::DynArray< const rtti::ClassType* > commandletClasses{ red::PoolBackend() };
	GetRttiSystem().RegisterPendingTypes();
	GetRttiSystem().EnumClasses( ClassID< red::ICommandlet >(), commandletClasses );

	for ( const auto* rttiClass : commandletClasses )
	{
		const auto* commandlet = rttiClass->GetDefaultObject< red::ICommandlet >();
		if ( commandlet && (commandlet->GetName() == name) )
		{
			return red::UniquePtr< ICommandlet >( rttiClass->CreateObject< red::ICommandlet >() );
		}
	}

	return nullptr;
}

static bool InitializeCommandlet( ICommandlet* commandlet )
{
	PC_SCOPE( Commandlet_InitializeEnvironment );
	return commandlet->InitializeEnvironment();
}

static void ShutdownCommandlet( ICommandlet* commandlet )
{
	PC_SCOPE( Commandlet_ShutdownEnvironment );
	commandlet->TeardownEnvironment();
}

} // namespace prv

enum ResultCodes : int
{
	SUCCESS = 0,
	GENERAL_FAILURE = -1, 
	COMMAND_NOT_FOUND = -404,
	FAILED_INIT = -500,
};

RED_CORE_API int RunCommandlet( const String& commandletName, const CommandLine& commandLine )
{
	RED_FATAL_ASSERT( ::SIsMainThread(), "Can only run this from the main thread" );

	if ( commandletName.Empty() )
	{
		return SUCCESS;
	}

	auto commandlet = prv::FindCommandlet( RED_NAME( commandletName ) );
	if ( !commandlet )
	{
		RED_LOG_ERROR( "Commandlet: Unable to find commandlet '%hs'", commandletName.AsChar() );
		return COMMAND_NOT_FOUND;
	}

	if ( !prv::InitializeCommandlet( commandlet.Get() ) )
	{
		RED_LOG_ERROR( "Commandlet: Unable to initialize environment for commandlet '%hs'", commandletName.AsChar() );
		commandlet->TeardownEnvironment();
		RED_FATAL_ASSERT( 0, "Failed to initialize environment for commandlet '%hs'. See logs for more info", commandletName.AsChar() );
		return FAILED_INIT;
	}

	// run commandlet
	bool runSuccess = false;
	{
		red::StopWatch timer;
		runSuccess = commandlet->Execute( commandLine );
		RED_LOG( "Commandlet: Execution of '%hs' took %1.2fs", commandletName.AsChar(), timer.GetDelta() );
	}

	prv::ShutdownCommandlet( commandlet.Get() );

	// return error code on failed commandlet execution
	return runSuccess ? SUCCESS : GENERAL_FAILURE;
}

} // red
