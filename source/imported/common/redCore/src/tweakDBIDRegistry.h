/**
* Copyright (c) 2017 CD Projekt Red. All Rights Reserved.
*/

#pragma once

#include "build.h"
#include "names.h"
#include "tweakDBID.h"
#include "../../redContainers/include/string/stringView.h"

namespace red
{
	class String;
}

namespace game
{
	namespace data
	{
		void Debug_ReserveRegistryStorage( Uint32 amount );

		void Debug_CreateRegistryEntry( const red::String& str, const TweakDBIDHash& hash );

		void Debug_CreateRegistryEntry( red::String&& str, const TweakDBIDHash& hash );

		void Debug_CreateRegistryEntry( CName name, const TweakDBIDHash& hash );

		TweakDBIDHash Debug_MergeRegistryEntries( const TweakDBIDHash& lhs, const TweakDBIDHash& rhs, const TweakDBIDHash& merged = TweakDBIDHash() );

		const red::String& Debug_GetRegistryString( TweakDBIDHash hash );

		red::StringView Debug_GetRegistryStringView( TweakDBIDHash hash );
	}
}
