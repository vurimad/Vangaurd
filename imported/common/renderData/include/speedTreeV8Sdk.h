/**
* Copyright (c) 2019 CD Projekt Red. All Rights Reserved.
*/


#pragma once

#define ST_WIND_CODE_SDK
#define ST_WIND_CODE_CPU

#undef ST_DLL_LINK
#define ST_DLL_LINK RENDER_DATA_API
#include "../../../external/speedtree_v8/cpp_source/speedtree_wind_state_mgr.h"
#undef ST_DLL_LINK