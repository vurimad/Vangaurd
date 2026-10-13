/**
* Copyright (c)2017 CD Projekt Red. All Rights Reserved.
*/

#pragma once

#if defined( RED_DLL ) || defined( RED_WITH_DLL )
    #ifdef RED_EXPORT_redConfig
        #define RED_CONFIG_API __declspec(dllexport)
        #define RED_CONFIG_API_TEMPLATE
    #else
        #define RED_CONFIG_API __declspec(dllimport)
        #define RED_CONFIG_API_TEMPLATE extern
    #endif
#else
    #define RED_CONFIG_API
    #define RED_CONFIG_API_TEMPLATE extern
#endif
