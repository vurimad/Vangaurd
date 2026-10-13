/**
* Copyright (c) 2013 CD Projekt Red. All Rights Reserved.
*/
#pragma once

#if defined( VANGUARD_ASSET_HOST )
#ifndef RED_CONTAINER_ALLOW_DEFAULT_POOL
#define RED_CONTAINER_ALLOW_DEFAULT_POOL
#endif
#include <new>
#include <type_traits>
#include <utility>
#include "../include/backendDataApi.h"
#include "../../../common/redSystem/include/redSystemPublic.h"
#ifndef RAPIDJSON_ASSERT
#define RAPIDJSON_ASSERT( expression ) RED_ASSERT( expression )
#endif
#include "../../../common/redMemory/include/redMemoryPublic.h"
#include "../../../common/redContainers/include/redContainersPublic.h"
#include "../../../common/redMath/include/redMathPublic.h"
#include "../../../common/redIO/include/redIOPublic.h"
#include "../../../common/redCore/include/redCorePublic.h"
#include "../../../common/redFileSystem/include/redFileSystemPublic.h"
#include "../../../common/redJobs2/include/redJobs2Public.h"
#include "../../../common/redReflection/include/redReflectionPublic.h"
#include "../../../common/resource/include/resourcePublic.h"
#include "../../../common/redCrypto/include/redCryptoPublic.h"
#include "../../backendEngine/include/backendEngineApi.h"
#else
#include "backendDataPublic.h"
#include "../../../common/redJobs2/include/redJobs2Public.h"
#include "../../../common/resource/include/resourcePublic.h"
#include "../../backendEngine/include/backendEnginePublic.h"
#include "../../../common/app/include/appPublic.h" // Only for loading data cache config file
#include "../../../common/networkServices/include/networkServicesPublic.h"
#endif
