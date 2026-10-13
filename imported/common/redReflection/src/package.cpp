/**
* Copyright (c) 2017 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"
#include "package.h"
#include "packageBuilder.h"

RED_NO_EMPTY_FILE();

namespace red
{
	static_assert( sizeof( StringDescriptor ) == 4, "DO NOT CHANGE SIZE. IF YOU NEED TO, UPDATE PACKAGE LOADER CODE." );
	static_assert( sizeof( ObjectDescriptor ) == 8, "DO NOT CHANGE SIZE. IF YOU NEED TO, UPDATE PACKAGE LOADER CODE." );
	static_assert( sizeof( ResourceDescriptor ) == 4, "DO NOT CHANGE SIZE. IF YOU NEED TO, UPDATE PACKAGE LOADER CODE." );
}
