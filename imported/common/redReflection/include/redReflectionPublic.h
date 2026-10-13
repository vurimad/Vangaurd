/**
* Copyright (c)2017 CD Projekt Red. All Rights Reserved.
*/

#pragma once

// Red System Integration
#include "../../redSystem/include/redSystemPublic.h"
#include "../../redMemory/include/redMemoryPublic.h"
#include "../../redCore/include/redCorePublic.h"
#include "../../redCompression/include/redCompressionPublic.h"
#include "../../redContainers/include/redContainersPublic.h"
#include "../../redJobs2/include/redJobs2Public.h"
#include "../../redConfig/include/redConfigPublic.h"
#include "../../redFileSystem/include/redFileSystemPublic.h"
#include "../../redMath/include/redMathPublic.h"

#include "../../commChannel/include/commChannelPublic.h"

#include "redReflectionApi.h"
#include "settings.h"

// As things improve, these warning suppressors should be disabled
RED_DISABLE_WARNING_CLANG("-Woverloaded-virtual");		// Don't report virtual function overloads (bad, but its used in a bunch of places)
RED_DISABLE_WARNING_CLANG("-Wreorder");					// Don't report initializer lists being in the wrong order
RED_DISABLE_WARNING_CLANG("-Wunused-private-field");	// Don't report unused private members
RED_DISABLE_WARNING_CLANG("-Wunused-variable");			// Don't report declared variables that are unused
RED_DISABLE_WARNING_CLANG("-Wformat");					// Don't report how "%hs" is undefined - people are using raw printfs or other format-string annotated functions
RED_DISABLE_WARNING_CLANG("-Wswitch");					// Don't report about unhandled conditions in switch statements
RED_DISABLE_WARNING_CLANG("-Wunused-function");			// Don't report unused functions

// Kernel libraries

#include "../../redMath/include/numericalUtils.h"
#include "../../redCore/include/absolutePath.h"

//////////////////////////////////////////////////////////////////////////

#include "types.h"
#include "memory.h"
#include "mathForward.h"
#include "serializableId.h"

//
#include "stringRTTI.h"
#include "containersRTTI.h"

// Serialization common
#include "serializationUtils.h"
#include "handleSerialization.h"
#include "containersSerialization.h"
#include "handle.h"
#include "weakHandle.h"

// RTTI system - low level (new) - to be moved to separate project
#include "rttiCommon.h"
#include "rttiTypeName.h"
#include "rttiSystem.h"
#include "rttiValueHolder.h"
#include "rttiSingleValueHolder.h"
#include "rttiAccessPath.h"
#include "rttiType.h"
#include "rttiSimpleType.h"
#include "rttiPointerTypes.h"
#include "rttiArrayTypes.h"
#include "rttiFundamentalTypes.h"

// RTTI - to refactor
#include "enumBuilder.h"
#include "bitFieldBuilder.h"
#include "rttiClassBuilder.h"
#include "rttiClass.h"
#include "rttiPropertyBuilder.h"
#include "rttiPropertyOverrideBuilder.h"
#include "rttiEnum.h"
#include "rttiArrayTypesImpl.h"
#include "rttiPointerTypesImpl.h"

// RTTI system (basic)
#include "rttiSystem.h"

// RTTI function calling
#include "rttiFunctionCalling.h"

// Resource loading
#include "resourceLoader.h"

// Global multiplayer setup
#include "multiplayerSetup.h"

#include "mathCommon.h"

#include "engineTime.h"

#include "frustum.h"

namespace serialization
{
	class IAsyncSource;
	typedef red::SharedPtr<IAsyncSource> AsyncSourcePtr;
	typedef red::UniquePtr<IAsyncSource> AsyncSourceUniquePtr;
}
