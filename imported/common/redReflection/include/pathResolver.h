/**
* Copyright (c) 2026 RED Vanguard, All Rights Reserved.
*/
#pragma once

namespace serialization
{

#ifndef RED_CONFIGURATION_FINAL
	// This is needed for the relinking done by the relink commandlet and during cooking
	class PathResolver
	{
	public:
		virtual res::ResourcePath ResolvePath( const res::ResourcePath& path ) = 0;
	};
#endif

} // serialization
