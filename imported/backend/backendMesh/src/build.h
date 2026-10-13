/**
* Copyright (c) 2013 CD Projekt Red. All Rights Reserved.
*/
#pragma once

#if defined( VANGUARD_ASSET_HOST )
#include <algorithm>
#include <functional>
#include <new>
#include <numeric>
#include <type_traits>
#include <utility>
#ifndef RED_CONTAINER_ALLOW_DEFAULT_POOL
#define RED_CONTAINER_ALLOW_DEFAULT_POOL
#endif
#include "../include/backendMeshApi.h"
#include "../../../common/redSystem/include/redSystemPublic.h"
#include "../../../common/redMemory/include/redMemoryPublic.h"
#include "../../../common/redContainers/include/redContainersPublic.h"
#include "../../../common/redMath/include/redMathPublic.h"
#include "../../../common/redIO/include/redIOPublic.h"
#include "../../../common/redCore/include/redCorePublic.h"
#include "../../../common/redFileSystem/include/redFileSystemPublic.h"
#include "../../../common/redJobs2/include/redJobs2Public.h"
#include "../../../common/redReflection/include/redReflectionPublic.h"
#include "../../../common/redCrypto/include/redCryptoApi.h"
#include "../../../common/core/include/coreApi.h"
#include "../../../common/renderData/include/renderDataApi.h"
#include "../../../common/renderData/include/renderingMemory.h"
#include "../../../common/renderData/include/renderObjectPtr.h"
#include "../../../common/renderData/include/renderResource.h"
#include "../../../common/renderData/include/renderResourceBlob.h"
#include "../../../common/renderData/include/bufferChunkType.h"
#include "../../../common/renderBackend/include/renderBackendApi.h"
#include "../../../common/resource/include/resourcePublic.h"
#include "../../../common/resourceMaterial/include/resourceMaterialApi.h"
#include "../../../common/resourceMesh/include/resourceMeshApi.h"
#include "../../backendEngine/include/backendEngineApi.h"
#include "../../backendData/include/backendDataApi.h"
#include "../../backendMaterial/include/backendMaterialApi.h"
#else
#include "backendMeshPublic.h"
#include "../../backendEngine/include/backendEnginePublic.h"
#include "../../backendData/include/backendDataPublic.h"
#include "../../backendMaterial/include/backendMaterialPublic.h"
#include "../../../common/resource/include/resourcePublic.h"
#endif

#include <numeric>
