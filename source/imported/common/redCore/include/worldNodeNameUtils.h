/**
* Copyright (c) 2018 CD Projekt Red. All Rights Reserved.
*/

#pragma once

#include "../../redContainers/include/string/string.h"
#include "../../redContainers/include/string/stringView.h"

namespace red
{
	REDCORE_API Bool ValidateGlobalNodeName( StringView name );

	REDCORE_API void FixNodeName( String& name );
	REDCORE_API void FixGlobalNodeName( String& name );

	// Utility function, adds "{}" brackets to the name, correctly handles numerical suffix.
	REDCORE_API String MarkNodeNameAsManual( const red::StringView& initialName );
	// Utility function, removes "{}" brackets from the name (if they are present), correctly handles numerical suffix.
	REDCORE_API String UnmarkNodeNameAsManual( const red::StringView& initialName );
	// Utility function, test if the name is marked as manual (has "{}" brackets), correctly handles numerical suffix.
	REDCORE_API Bool IsNodeNameMarkedAsManual( const red::StringView& initialName );


} // red
