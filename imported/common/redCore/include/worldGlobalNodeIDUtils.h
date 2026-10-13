/**
* Copyright (c) 2018 CD Projekt Red. All Rights Reserved.
*/

#pragma once

#include "../../redContainers/include/string/string.h"
#include "../../redContainers/include/string/stringView.h"

namespace red
{
	namespace globalid
	{
		REDCORE_API Bool IsRelativeGlobalIDPath( const StringView path );

		REDCORE_API String MakeRelativeDeltaPath( const StringView referencePath, const StringView destPath );

		REDCORE_API String MakeRelativeDeltaGlobalIDPath( const StringView referencePath, const StringView destPath );

	} // globalid

} // red
