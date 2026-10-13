/**
* Copyright (c) 2013 CD Projekt Red. All Rights Reserved.
*/

#pragma once

#ifndef RED_MODULE_commChannel
	#error "Do not include precompile headers directly, please use commChannelPublic.h"
#endif

#include "commChannelPublic.h"

#include "../../../common/redSystem/include/redThreadsAtomic.h"
#include "../../../common/redSystem/include/redThreadsThread.h"
#include "../../../common/redContainers/include/dynArray.h"
#include "../../../common/redContainers/include/queue.h"
#include "../../../common/redMemory/include/uniqueBuffer.h"
#include "../../../common/redMemory/include/sharedPtr.h"
#include "../../../common/redMemory/include/uniquePtr.h"

#include <functional>

#include "protoSerializationCommon.h"
