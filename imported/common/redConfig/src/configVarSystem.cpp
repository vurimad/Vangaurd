/**
* Copyright (c) 2007-16 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"
#include "configVarSystem.h"

#include "configVarRegistry.h"
#include "configVarStorage.h"
#include "../../redFileSystem/include/fileSys.h"
#include "../../redCore/include/singleton.h"


//////////////////////////////////////////////////////////////////////////
// usings
using red::AnsiChar;
using red::String;
using red::AbsolutePath;
using red::DynArray;

namespace Config
{

	CConfigSystem& GetConfigSystem()
	{
		return red::TSingleton< Config::CConfigSystem, red::TNoDestructionLifetime >::GetInstance();
	}

	const char* CConfigSystem::FILE_EXTENSION = ".ini";
	const char* CConfigSystem::FILE_SEARCH_EXTENSION = "*.ini";

	CConfigSystem::CConfigSystem()
		: m_base( RED_NEW( CConfigVarStorage )() )
		, m_user( RED_NEW( CConfigVarStorage )() )
		, m_registry( RED_NEW( CConfigVarRegistry )() )
		, m_paths( red::PoolEngine() )
		, m_configResetInThisSession( false )
	{
	}

	CConfigSystem::~CConfigSystem() = default;

	void CConfigSystem::Reload()
	{
		RED_FATAL_ASSERT( ::SIsMainThread(), "Config loading has to be done on main thread only." );

		red::ScopedLock< red::Mutex > scopedLock( m_lock );
		Load();
	}

	void CConfigSystem::Save()
	{
		RED_FATAL_ASSERT( ::SIsMainThread(), "Config loading has to be done on main thread only." );

		red::ScopedLock< red::Mutex > scopedLock( m_lock );

		// Save only the values that are different than the base values
		// This way we only store what user has actually changed

		// Flush variables from register to m_user
		DynArray< IConfigVar* > registeredVars{ red::PoolEngine() };
		m_registry->EnumVars( registeredVars );

		// Flush savable vars into the storage
		for( auto var : registeredVars )
		{
			String currentValue = String::EMPTY();
			if ( var->HasFlag( eConsoleVarFlag_Save ) && ( var->IsDifferentThanCodeDefault() || m_user->GetEntry( var->GetGroup(), var->GetName(), currentValue ) ) )
			{
				String value( "" );
				var->GetText( value );
				m_user->SetEntry( var->GetGroup(), var->GetName(), value );
			}
		}

		CConfigVarStorage temp;
		m_user->FilterDifferences( *m_base, temp );

		ApplyBaseVersionToStorage( temp );
		temp.Save( m_userConfigPath );
	}

	void CConfigSystem::Init( const red::AbsolutePath& engineConfigRoot,  const red::AbsolutePath& gameConfigRoot, IUserConfigStorage* userConfigStorage )
	{
		m_userConfigPath = gameConfigRoot.AddFilePath( "user.ini" );

		// setup the base configuration stack
		// this was once read from a file but it's not that necessary
		m_paths.PushBack( engineConfigRoot.AddDirPath( "base" ) );
		m_paths.PushBack( gameConfigRoot.AddDirPath( "base" ) );

		// per platform config
#if defined( RED_PLATFORM_WINPC ) || defined( RED_PLATFORM_LINUX )
		m_paths.PushBack( engineConfigRoot.AddDirPath( "platform\\pc" ) );
#elif defined( RED_PLATFORM_DURANGO )
		m_paths.PushBack( engineConfigRoot.AddDirPath( "platform\\xbox1" ) );
#elif defined( RED_PLATFORM_ORBIS )
		m_paths.PushBack( engineConfigRoot.AddDirPath( "platform\\ps4" ) );
#else
		#error "Unknown platform"
#endif
		// dev config files override everything
		m_paths.PushBack( engineConfigRoot.AddDirPath( "dev" ) );
		m_paths.PushBack( gameConfigRoot.AddDirPath( "dev" ) );
	}

	void CConfigSystem::Load()
	{
		RED_FATAL_ASSERT( ::SIsMainThread(), "Config loading has to be done on main thread only." );

		red::ScopedLock< red::Mutex > scopedLock( m_lock );

		m_configResetInThisSession = false;

		// Iterate through all paths (but not the user path)
		for( Uint32 i = 0; i < m_paths.Size(); ++i )
		{
			DynArray< red::AbsolutePath > files{ red::PoolEngine() };
			GFileManager->FindFiles( m_paths[i], String( FILE_SEARCH_EXTENSION ), files, false );
			RED_LOG( "Core: Found %d engine configs in path %hs", files.Size(), m_paths[i].AsChar() );

			for ( const red::AbsolutePath& configFilePath : files )
			{
				if ( m_userConfigPath != configFilePath )
				{
					m_base->Load( configFilePath );
				}
			}
		}
		m_user->Load( m_userConfigPath );

		// Apply base values
		m_registry->Refresh( *m_base );

		// Apply user values if matching base version, otherwise clear user settings
		Int32 baseConfigVersion = GetBaseConfigVersion();
		Int32 userConfigVersion = GetStorageConfigVersion( m_user.Get() );

		Bool applyUserConfigs = true;
		if( userConfigVersion != baseConfigVersion )
		{
			applyUserConfigs = false;
		}

		if( applyUserConfigs == true )
		{
			m_registry->Refresh( *m_user );
			m_configResetInThisSession = false;
			RED_LOG( "Core: User settings applied to config registy." );
		}
		else
		{
			m_user->Clear();
			m_configResetInThisSession = true;
			RED_LOG( "Core: User settings have different version number than base - resetting user settings (does not include input)" );
		}
	}

	Bool CConfigSystem::GetValue( const AnsiChar* groupName, const AnsiChar* keyName, String& outValue ) const
	{
		// find variable
		IConfigVar* var = m_registry->Find( groupName, keyName );
		if ( var )
		{
			return var->GetText( outValue );
		}

		// find in user storage
		if ( m_user->GetEntry( groupName, keyName, outValue ) )
		{
			return true;
		}

		// as a fallback, use the global storage
		return m_base->GetEntry( groupName, keyName, outValue );
	}

	Bool CConfigSystem::SetValue( const AnsiChar* groupName, const AnsiChar* keyName, const String& value )
	{
		// find variable
		IConfigVar* var = m_registry->Find( groupName, keyName );
		if ( var )
		{
			return var->SetText( value );
		}

		// set in the user storage
		return m_user->SetEntry( groupName, keyName, value );
	}

	Bool CConfigSystem::GetDefaultValue( const AnsiChar* groupName, const AnsiChar* keyName, String& outValue ) const
	{
		if( m_base->GetEntry( groupName, keyName, outValue ) )
		{
			return true;
		}

		if( const IConfigVar* var = m_registry->Find( groupName, keyName ) )
		{
			return var->GetTextDefault( outValue );
		}

		return false;
	}

	void CConfigSystem::Shutdown()
	{
	}

	void CConfigSystem::ResetUserSettings()
	{
		RED_FATAL_ASSERT( ::SIsMainThread(), "Config resetting has to be done on main thread only." );

		red::ScopedLock< red::Mutex > scopedLock( m_lock );

		// Reset all config values to their default value from code
		ResetConfigs();

		// Clear user values
		m_user->Clear();

		// Apply the base values
		m_registry->Refresh( *m_base );
	}

	void CConfigSystem::ResetConfigs( const AnsiChar* groupMatch /*= ""*/, const AnsiChar* nameMatch /*= ""*/, const Uint32 includeFlags /*= 0*/, const Uint32 excludeFlags /*= 0*/ )
	{
		RED_FATAL_ASSERT( ::SIsMainThread(), "Config resetting has to be done on main thread only." );

		red::ScopedLock< red::Mutex > scopedLock( m_lock );

		DynArray<IConfigVar*> vars{ red::PoolEngine() };
		m_registry->EnumVars( vars, groupMatch, nameMatch, includeFlags, excludeFlags );

		for( IConfigVar* var : vars )
		{
			var->Reset();
		}
	}

	Int32 CConfigSystem::GetBaseConfigVersion() const
	{
		Int32 version = GetStorageConfigVersion( m_base.Get() );
		RED_FATAL_ASSERT( version != -1, "No config version specified! [General] ConfigVersion=? not found! Or config base version format is invalid! Version format should be a number." );
		return version;
	}

	Int32 CConfigSystem::GetStorageConfigVersion( const CConfigVarStorage* storage ) const
	{
		// Get version as string
		String storageConfigVersionString;
		storage->GetEntry( "General", "ConfigVersion", storageConfigVersionString );

		// Convert version to int
		Bool storageVersionValid = false;
		Int32 storageConfigVersion = -1;
		storageVersionValid = FromString( storageConfigVersionString.AsChar(), storageConfigVersion );

		// If storage version has invalid format set it to -1
		if( storageVersionValid == false )
		{
			storageConfigVersion = -1;
		}

		return storageConfigVersion;
	}

	void CConfigSystem::ApplyBaseVersionToStorage( CConfigVarStorage& storage )
	{
		Int32 baseVersion = GetBaseConfigVersion();
		storage.SetEntry( "General", "ConfigVersion", ToStringDirect( baseVersion ).AsChar() );
	}
} // Config
