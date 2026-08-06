/**
* Copyright (c)2017 CD Projekt Red. All Rights Reserved.
*/

#pragma once

#include "../../redSystem/include/redSystemPublic.h"
#include "../../redMemory/include/redMemoryPublic.h"

// ctremblay: REMOVE ASAP
// As things improve, these warning suppressors should be disabled
RED_DISABLE_WARNING_CLANG("-Woverloaded-virtual");		// Don't report virtual function overloads (bad, but its used in a bunch of places)
RED_DISABLE_WARNING_CLANG("-Wreorder");					// Don't report initializer lists being in the wrong order
RED_DISABLE_WARNING_CLANG("-Wunused-private-field");	// Don't report unused private members
RED_DISABLE_WARNING_CLANG("-Wunused-variable");			// Don't report declared variables that are unused
RED_DISABLE_WARNING_CLANG("-Wformat");					// Don't report how "%hs" is undefined - people are using raw printfs or other format-string annotated functions
RED_DISABLE_WARNING_CLANG("-Wswitch");					// Don't report about unhandled conditions in switch statements
RED_DISABLE_WARNING_CLANG("-Wunused-function");			// Don't report unused functions
RED_DISABLE_WARNING_CLANG("-Wunknown-pragmas");
RED_DISABLE_WARNING_CLANG("-W#pragma-messages");

#include <numeric>

#include "../../redContainers/include/redContainersPublic.h"
#include "../../redCore/include/redCorePublic.h"
#include "../../redMath/include/redMathPublic.h"
#include "../../redFileSystem/include/redFileSystemPublic.h"
#include "../../redConfig/include/redConfigPublic.h"
#include "../../commChannel/include/commChannelPublic.h"
#include "../../redJobs2/include/redJobs2Public.h"

#include "redReflectionApi.h"
#include "settings.h"

#include "types.h"
#include "reflectionPool.h"

