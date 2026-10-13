/**
* Copyright (C)2017 CD Projekt Red. All Rights Reserved.
*/

#pragma once

#if defined( RED_DLL ) || defined( RED_WITH_DLL )
    #ifdef RED_EXPORT_resourceMaterial
        #define RESOURCEMATERIAL_API __declspec(dllexport)
        #define RESOURCEMATERIAL_API_TEMPLATE
    #else
        #define RESOURCEMATERIAL_API __declspec(dllimport)
        #define RESOURCEMATERIAL_API_TEMPLATE extern
    #endif
#else
    #define RESOURCEMATERIAL_API
    #define RESOURCEMATERIAL_API_TEMPLATE extern
#endif
