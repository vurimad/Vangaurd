/**
* Copyright (c)2017 CD Projekt Red. All Rights Reserved.
*/

#pragma once

#if defined( RED_DLL ) || defined( RED_WITH_DLL )
    #ifdef RED_EXPORT_redFileSystem
        #define RED_FILESYSTEM_API __declspec(dllexport)
        #define RED_FILESYSTEM_API_TEMPLATE
    #else
        #define RED_FILESYSTEM_API __declspec(dllimport)
        #define RED_FILESYSTEM_API_TEMPLATE extern
    #endif
#else
    #define RED_FILESYSTEM_API
    #define RED_FILESYSTEM_API_TEMPLATE extern
#endif
