/*
 * Copyright (c) 2018 CD Projekt Red. All Rights Reserved.
 */

#pragma once

#include "../../../common/redSystem/include/log.h"

#undef RED_LOG
#undef RED_LOG_INFO
#undef RED_LOG_WARNING
#undef RED_LOG_ERROR
#undef RED_LOG_TRACE
#undef RED_LOG_DEBUG

#define RED_LOG( message, ... ) RED_LOG_CATEGORY( red::LoggerCategory_DataBuild, message, ##__VA_ARGS__ )
#define RED_LOG_INFO( message, ... ) RED_LOG_CATEGORY_INFO( red::LoggerCategory_DataBuild, message, ##__VA_ARGS__ )
#define RED_LOG_WARNING( message, ...  ) RED_LOG_CATEGORY_WARNING( red::LoggerCategory_DataBuild, message, ##__VA_ARGS__ )
#define RED_LOG_ERROR( message, ...  ) RED_LOG_CATEGORY_ERROR( red::LoggerCategory_DataBuild, message, ##__VA_ARGS__ )
#define RED_LOG_TRACE( message, ... ) RED_LOG_CATEGORY_TRACE( red::LoggerCategory_DataBuild, message, ##__VA_ARGS__ )
#define RED_LOG_DEBUG( message, ...	) RED_LOG_CATEGORY_DEBUG( red::LoggerCategory_DataBuild, message, ##__VA_ARGS__ )
