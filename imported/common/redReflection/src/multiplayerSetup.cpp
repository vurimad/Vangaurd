#include "build.h"
#include "multiplayerSetup.h"
#include "../../../common/redCore/include/commandline.h"

namespace red
{
	namespace MultiplayerSetup
	{
		static Bool s_isInitialized = false;
		static Bool s_isServer = false;
		static Bool s_isClient = false;
		static Bool s_isBoothMode = false;
		static Bool s_isForceUsePCData = false;
		static String s_playtestName;

		void Initialize()
		{
			RED_ASSERT( !s_isInitialized );

			const red::CommandLine& commandLine = red::CommandLine::Get();

			String engineClass;
			if( commandLine.GetFirstOption( "engine", engineClass ) )
			{
				if ( engineClass == "serverServerGameEngine" ||
					 engineClass == "serverDebugServerGameEngine" ||
					 engineClass == "FunctionalTestsServerEngine" ||
					 engineClass == "HeadfullTestServerEngine" ||
					 engineClass == "HeadllessTestServerEngine" )
				{
					s_isServer = true;
				}
			}

			if ( commandLine.HasOption( "multiplayerClient" ) )
			{
				s_isClient = true;
			}

#ifndef RED_CONFIGURATION_FINAL
			if( commandLine.HasOption( "boothMode" ) )
			{
				s_isBoothMode = true;
			}

			if( commandLine.HasOption( "forceUsePCData" ) )
			{
				s_isForceUsePCData = true;
			}

			commandLine.GetFirstParam( "playtest", s_playtestName );
#endif

			s_isInitialized = true;

			RED_ASSERT( !IsBoothMode() || IsPlaytest(), "Booth mode requires valid playtest!" );
		}

		Bool IsInitialized()
		{
			return s_isInitialized;
		}

		Bool IsServer()
		{
			return s_isServer;
		}

		Bool IsClient()
		{
			RED_ASSERT( s_isInitialized );
			return s_isClient;
		}

		Bool IsMultiplayer()
		{
			RED_ASSERT( s_isInitialized );
			return s_isClient || s_isServer;
		}

		Bool IsBoothMode()
		{
			return s_isBoothMode;
		}

		Bool IsPlaytest()
		{
			return s_playtestName.Length() > 0;
		}

		const String& GetPlaytestName()
		{
			return s_playtestName;
		}

#ifdef RED_PLATFORM_WINPC
		Bool ShouldUseWindowsServerData()
		{
			return
				IsServer() &&
				::IsHeadlessMode() &&
				!s_isForceUsePCData;
		}
#endif
	}
}
