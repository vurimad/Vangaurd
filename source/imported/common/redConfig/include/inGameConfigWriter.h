/**
 * Copyright (c) 2019-2020 CD Projekt Red. All Rights Reserved.
 */

#pragma once

namespace InGameConfig
{
	class Registry;

	class RED_CONFIG_API Writer : red::NonCopyable
	{
	public:
		Writer( const Registry& registry );
		~Writer();

		Uint32 SaveUserSettings( IFile* file );

	private:
		const Registry& m_registry;
	};
}