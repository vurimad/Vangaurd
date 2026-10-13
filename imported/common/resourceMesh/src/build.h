/**
* Copyright (C)2017 CD Projekt Red. All Rights Reserved.
*/

#pragma once

#if defined( VANGUARD_ASSET_HOST )
#include <algorithm>
#include <functional>
#include <new>
#include <type_traits>
#include <utility>
#ifndef RED_CONTAINER_ALLOW_DEFAULT_POOL
#define RED_CONTAINER_ALLOW_DEFAULT_POOL
#endif
#include "../include/resourceMeshApi.h"
#include "../../redSystem/include/redSystemPublic.h"
#include "../../redMemory/include/redMemoryPublic.h"
#include "../../redContainers/include/redContainersPublic.h"
#include "../../redMath/include/redMathPublic.h"
#include "../../redIO/include/redIOPublic.h"
#include "../../redCore/include/redCorePublic.h"
#include "../../redFileSystem/include/redFileSystemPublic.h"
#include "../../redJobs2/include/redJobs2Public.h"
#include "../../redReflection/include/redReflectionPublic.h"
#include "../../renderData/include/renderDataApi.h"
#include "../../resource/include/resourcePublic.h"
#include "../../resourceMaterial/include/resourceMaterialApi.h"
#include "../../renderData/include/renderingMemory.h"
#include "../../renderData/include/renderSimpleTypes.h"
#include "../../renderData/include/renderObjectPtr.h"
#include "../../renderData/include/renderResource.h"
#include "../../renderData/include/renderResourceBlob.h"
#include "../../renderData/include/bufferChunkType.h"
#include "../../renderData/include/renderPublicEnums.h"
#else
#include "resourceMeshInternal.h"
#endif
