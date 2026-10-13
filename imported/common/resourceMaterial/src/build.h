/**
* Copyright (C)2017 CD Projekt Red. All Rights Reserved.
*/

#pragma once

#if defined( VANGUARD_ASSET_HOST )
#include <functional>
#include <new>
#include <type_traits>
#include <utility>
#endif

#include "../../redSystem/include/redSystemPublic.h"
#include "../../redMemory/include/redMemoryPublic.h"
#include "../../redContainers/include/redContainersPublic.h"
#include "../../redMath/include/redMathPublic.h"
#include "../../redIO/include/redIOPublic.h"
#include "../../redCore/include/redCorePublic.h"
#include "../../redFileSystem/include/redFileSystemPublic.h"
#include "../../redReflection/include/redReflectionPublic.h"
#if defined( VANGUARD_ASSET_HOST )
#include "../../redImage/include/redImageApi.h"
#include "../../renderData/include/renderDataApi.h"
#include "../../renderData/include/renderingMemory.h"
#include "../../renderData/include/renderSimpleTypes.h"
#include "../../renderData/include/renderObjectPtr.h"
#include "../../renderData/include/renderResource.h"
#include "../../resource/include/resourcePublic.h"
#else
#include "../../renderData/include/renderDataPublic.h"
#endif

#include "resourceMaterialApi.h"
