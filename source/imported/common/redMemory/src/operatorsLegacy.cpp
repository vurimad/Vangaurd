/**
 * Copyright (c) 2016 CD Projekt Red. All Rights Reserved.
 */

#include "build.h"

//////////////////////////////////////////////////////////////////////////
// Microsoft platforms allow us to define global new / delete in Core.
// PS4 will only let us define them in object files explicitly linked to main
// Therefore, we only define them on MS compilers here
#ifdef RED_COMPILER_MSC
	#include "operatorsLegacy.h"
#endif
