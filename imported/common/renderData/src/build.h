/**
* Copyright (c) 2016 CD Projekt Red. All Rights Reserved.
*/

#if defined( VANGUARD_ASSET_HOST )
#include <functional>
#include <new>
#include <type_traits>
#include <utility>
#ifndef RED_CONTAINER_ALLOW_DEFAULT_POOL
#define RED_CONTAINER_ALLOW_DEFAULT_POOL
#endif
#include "../include/renderDataApi.h"
#include "../../redSystem/include/redSystemPublic.h"
#include "../../redMemory/include/redMemoryPublic.h"
#include "../../redContainers/include/redContainersPublic.h"
#include "../../redMath/include/redMathPublic.h"
#include "../../redIO/include/redIOPublic.h"
#include "../../redCore/include/redCorePublic.h"
#include "../../redFileSystem/include/redFileSystemPublic.h"
#include "../../redJobs2/include/redJobs2Public.h"
#include "../../redReflection/include/redReflectionPublic.h"
#include "../../redImage/include/redImageApi.h"
#include "../../resource/include/resourcePublic.h"
#include "../../resourceMaterial/include/resourceMaterialApi.h"
#include "../include/renderingMemory.h"
#include "../include/renderSimpleTypes.h"
#include "../include/renderObjectPtr.h"
#include "../include/bufferChunkType.h"
#else
#include "renderDataInternal.h"
#endif
