/**
* Copyright (c) 2007-16 CD Projekt Red. All Rights Reserved.
*/
#pragma once

#include "../../redCore/include/absolutePath.h"

// How configuration works
//  It consists of 4 layers:
//     - Base config (manual entries, read only)
//     - Platform config, per platform: PC, Xbox1, PS4 (manual entries, read only)
//     - Game config (manual entries, read only)
//     - User config (read write, can be used in game, stored in user profile if needed), also per platform
//
//  The values for config vars "visible" to the outside world are calculated as a merged result of those 4 layers.
//
//  Also, when an user config is saved the values that are THE same as in the read-only layers are not saved, only the delta is.

namespace Config
{
	class CConfigVarStorage;
	class CConfigVarRegistry;
	class CRemoteConfig;

	class IUserConfigStorage;

	/// Config system master class
	class RED_CONFIG_API CConfigSystem : private red::NonCopyable
	{
	public:
		CConfigSystem();
		~CConfigSystem();

		// initialize config system
		void Init( const red::AbsolutePath& engineConfigRoot,  const red::AbsolutePath& gameConfigRoot, IUserConfigStorage* userConfigStorage );

		// reload configuration
		void Reload();

		// reset user settings
		void ResetUserSettings();

		// load configuration
		void Load();

		// flush changes in the settings
		void Save();

		// get value for given group and key
		Bool GetValue( const red::AnsiChar* groupName, const red::AnsiChar* keyName, red::String& outValue ) const;

		// set value for given group and key (propagates to a variable if found)
		Bool SetValue( const red::AnsiChar* groupName, const red::AnsiChar* keyName, const red::String& value );

		// get value for given group and key
		Bool GetDefaultValue( const red::AnsiChar* groupName, const red::AnsiChar* keyName, red::String& outValue ) const;

		// reset values of all registered configs to their default values
		void ResetConfigs( const red::AnsiChar* groupMatch = "", const red::AnsiChar* nameMatch = "", const Uint32 includeFlags = 0, const Uint32 excludeFlags = 0 );

		// shutdown system, detach from user config loader saver
		void Shutdown();

		// When configs are reset, we have to inform game about that
		RED_INLINE Bool AreConfigResetInThisSession() const { return m_configResetInThisSession; }

		// get config registry
		RED_INLINE CConfigVarRegistry& GetRegistry() const { return *m_registry; }

	private:
		// each group of values lie in different storage
		red::UniquePtr< CConfigVarStorage >	m_base; // merged read only config
		red::UniquePtr< CConfigVarStorage >	m_user; // dynamic config

		// main config registry
		red::UniquePtr< CConfigVarRegistry >	m_registry;

		// stored user file name
		red::DynArray<red::AbsolutePath>		m_paths;		// All config paths in override order
		red::AbsolutePath m_userConfigPath;

		// Always returns valid version - otherwise asserts
		Int32 GetBaseConfigVersion() const;

		// If user version can't be read - returns -1
		Int32 GetStorageConfigVersion( const CConfigVarStorage* storage ) const;

		// Applies config version from m_base to given storage
		void ApplyBaseVersionToStorage( CConfigVarStorage& storage );

		// file extension
		static const char* FILE_EXTENSION;
		static const char* FILE_SEARCH_EXTENSION;

		red::Mutex m_lock;

		Bool m_configResetInThisSession;
	};

	enum EPlatform
	{
		ePlatform_PC,
		ePlatform_XB1,
		ePlatform_PS4,
		ePlatform_Unknown,
	};
	
	// Returns active platform string
	template< typename TChar >
	RED_INLINE constexpr const TChar* GetPlatformString()
	{
#if defined(RED_PLATFORM_WINPC)
		return RED_TEMPLATE_TXT( TChar, "pc" );
#elif defined(RED_PLATFORM_DURANGO)
		return RED_TEMPLATE_TXT( TChar, "xbox1" );
#elif defined(RED_PLATFORM_ORBIS)
		return RED_TEMPLATE_TXT( TChar, "ps4" );
#else
		return RED_TEMPLATE_TXT( TChar, "unknown" );
#endif
	}

	// Returns active platform enum
	RED_INLINE constexpr EPlatform GetPlatform()
	{
#if defined(RED_PLATFORM_WINPC)
		return ePlatform_PC;
#elif defined(RED_PLATFORM_DURANGO)
		return ePlatform_XB1;
#elif defined(RED_PLATFORM_ORBIS)
		return ePlatform_PS4;
#else
		return ePlatform_Unknown;
#endif
	}
	
	RED_CONFIG_API CConfigSystem& GetConfigSystem();

} // Config
