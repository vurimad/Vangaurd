/**
* Copyright (c) 2010-16 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"
#include "commandlet.h"
#include "../../redCore/include/commandline.h"
#include "../../redReflection/include/version.h"
#include "../../game/include/gameVersion.h"

namespace red
{
	ICommandlet::ICommandlet( const CName name )
		: m_commandletName( name )
	{
	}

	ICommandlet::~ICommandlet()
	{
	}

	Bool ICommandlet::InitializeEnvironment()
	{
		return true;
	}

	void ICommandlet::TeardownEnvironment()
	{
	}

	//---

	/// prints engine version
	class CVersionCommandlet : public ICommandlet
	{
		RTTI_DECLARE_TYPE( CVersionCommandlet );

	public:
		CVersionCommandlet()
			: ICommandlet( RED_NAME_CONSTEXPR("version") )
		{
		}

		virtual Bool Execute( const CommandLine& options ) override final
		{
			RED_LOG( "App version: %hs.%hs", APP_VERSION_MAJOR, APP_VERSION_MINOR );
			RED_LOG( "App build: %hs", APP_VERSION_BUILD );
			RED_LOG( "App timestamp: %hs %hs", __DATE__, __TIME__ );
			RED_LOG( "Peforce changelist: %hs", APP_LAST_P4_CHANGE );
			RED_LOG( "Peforce stream: %hs", APP_P4_STREAM );
			RED_LOG( "Peforce shelve: %hs", APP_P4_SHELF );
			RED_LOG( "Game version: %hs", GAME_VERSION );
			return true;
		}

		virtual const char* GetOneLiner() const override final
		{
			return "Print engine's version data";
		}

		virtual void PrintHelp() const override final
		{
			RED_LOG( "Prints engine's version data" );
		}
	};

	//---

	/// prints help for commandlets
	class CHelpCommandlet : public ICommandlet
	{
		RTTI_DECLARE_TYPE( CHelpCommandlet );

	public:
		CHelpCommandlet()
			: ICommandlet( RED_NAME_CONSTEXPR("help" ) )
		{
		}

		virtual Bool Execute( const CommandLine& options ) override final
		{
			String commandletName;
			options.GetFirstOption( "name", commandletName );

			if ( commandletName.Empty() )
				return PrintList();
			else
				return PrintHelp( commandletName );
		}

		virtual const char* GetOneLiner() const override final
		{
			return "Print help for commandlet system";
		}

		virtual void PrintHelp() const override final
		{
			RED_LOG( "Usage:" );
			RED_LOG( "   help [-name=<commandletname>]" );
			RED_LOG( "" );
			RED_LOG( "name - name of the commandlet for which more information should be printed" );
		}

	private:
		Bool PrintHelp( const String& commandletName )
		{
			red::DynArray< const rtti::ClassType* > commandletClasses{ red::PoolBackend() };
			GetRttiSystem().EnumClasses( ClassID< ICommandlet >(), commandletClasses );

			const CName commandletCName = RED_NAME_NOREG( commandletName );
			const ICommandlet* commandlet = nullptr;
			for ( const auto* commandletClass : commandletClasses )
			{
				const auto* testCommandlet = commandletClass->GetDefaultObject< ICommandlet >();
				if ( testCommandlet->GetName() == commandletCName )
				{
					commandlet = testCommandlet;
					break;
				}
			}

			if ( commandlet )
			{
				commandlet->PrintHelp();
				return true;
			}
			else
			{
				RED_LOG_ERROR( "Commandlet '%hs' not found", commandletName.AsChar() );
				return false;
			}
		}

		Bool PrintList()
		{
			red::DynArray< const rtti::ClassType* > commandletClasses{ red::PoolBackend() };
			GetRttiSystem().EnumClasses( ClassID< ICommandlet >(), commandletClasses );

			RED_LOG("Found %d commandlets", commandletClasses.Size() );

			struct Entry
			{
				String m_name;
				String m_oneLiner;
			};

			red::DynArray< Entry > infoEntries{ red::PoolBackend() };

			for ( const auto* commandletClass : commandletClasses )
			{
				const auto* commandlet = commandletClass->GetDefaultObject< ICommandlet >();

				Entry entry;
				entry.m_name = commandlet->GetName().AsChar();
				entry.m_oneLiner = commandlet->GetOneLiner();
				infoEntries.PushBack( entry );
			}

			std::sort( infoEntries.Begin(), infoEntries.End(), [](const Entry& a, const Entry& b) { return a.m_name < b.m_name; } );

			for ( const auto& entry : infoEntries )
				RED_LOG( "%hs - %hs", entry.m_name.AsChar(), entry.m_oneLiner.AsChar() );

			return true;
		}
	};

	//---

}

RTTI_BEGIN_ABSTRACT_TYPE_IN_NAMESPACE( ICommandlet, red );
RTTI_END_TYPE();



RTTI_BEGIN_TYPE_IN_NAMESPACE( CVersionCommandlet, red );
	RTTI_PARENT_TYPE( red::ICommandlet )
RTTI_END_TYPE();



RTTI_BEGIN_TYPE_IN_NAMESPACE( CHelpCommandlet, red );
	RTTI_PARENT_TYPE( red::ICommandlet )
RTTI_END_TYPE();



