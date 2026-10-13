/**
 * Copyright (c) 2019-2020 CD Projekt Red. All Rights Reserved.
 */

#pragma once

#include "inGameConfigFileUtils.h"

class IFile;

namespace red
{
	class AbsolutePath;
}

namespace InGameConfig
{
	class Registry;
	enum class UserSettingsLoadStatus : Uint8;

	enum class IsPlatformSpecific : Bool
	{
		No, Yes
	};

	class RED_CONFIG_API Reader : private red::NonCopyable
	{
	public:
		Reader( Registry& registry );
		~Reader();

		Bool Init( const red::AbsolutePath& commonSettingsPath, const red::AbsolutePath& platformSettingsPath );
		UserSettingsLoadStatus LoadUserSettings( IFile* file );

	private:
		Bool LoadTemplates( const red::AbsolutePath& commonPath, const red::AbsolutePath& platformPath );
		Bool LoadGroups( const red::AbsolutePath& path, const RapidJsonDocument& doc );
		Bool LoadConfigVars( const red::AbsolutePath& path, const RapidJsonDocument& doc, IsPlatformSpecific isPlatformSpecific );

		Registry& m_registry;
	};
}