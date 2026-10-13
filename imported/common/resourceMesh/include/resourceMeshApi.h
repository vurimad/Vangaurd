/**
* Copyright (C)2017 CD Projekt Red. All Rights Reserved.
*/

#pragma once

#if defined( RED_DLL ) || defined( RED_WITH_DLL )
    #ifdef RED_EXPORT_resourceMesh
        #define RESOURCEMESH_API __declspec(dllexport)
        #define RESOURCEMESH_API_TEMPLATE
    #else
        #define RESOURCEMESH_API __declspec(dllimport)
        #define RESOURCEMESH_API_TEMPLATE extern
    #endif
#else
    #define RESOURCEMESH_API
    #define RESOURCEMESH_API_TEMPLATE extern
#endif
