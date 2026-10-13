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
#include "../include/renderBackendApi.h"
#include "../../redSystem/include/redSystemPublic.h"
#include "../../redMemory/include/redMemoryPublic.h"
#include "../../redContainers/include/redContainersPublic.h"
#include "../../redMath/include/redMathPublic.h"
#include "../../redReflection/include/redReflectionPublic.h"
#include "../../redFileSystem/include/redFileSystemPublic.h"
#include "../../redImage/include/redImageApi.h"
#include "../../renderData/include/renderDataApi.h"
#include "../../resource/include/resourcePublic.h"
#include "../../resourceMaterial/include/resourceMaterialApi.h"
#include "../../renderData/include/renderingMemory.h"
#include "../../renderData/include/renderSimpleTypes.h"
#include "../../renderData/include/renderObjectPtr.h"
#include "../../renderData/include/renderResource.h"
#include "../../renderData/include/gpuDataProcessor.h"
#include "../../renderData/include/bufferChunkType.h"
#else
#include "renderBackendInternal.h"
#endif
