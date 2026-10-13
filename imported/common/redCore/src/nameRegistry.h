/**
* Copyright (c) 2015-20 CD Projekt Red. All Rights Reserved.
*/

#pragma once

namespace red
{
	using CNameHash = Uint64;

	void Debug_RegisterNameString( CNameHash hash, red::StringView view );
	red::StringView Debug_GetNameString( CNameHash hash );
}
