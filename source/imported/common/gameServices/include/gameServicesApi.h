/**
* Copyright (c) 2016 CD Projekt Red. All Rights Reserved.
*/

#pragma once

#if defined( RED_DLL ) || defined( RED_WITH_DLL )
    #ifdef RED_EXPORT_gameServices
        #define GAME_SERVICES_API __declspec(dllexport)
        #define GAME_SERVICES_API_TEMPLATE
    #else
        #define GAME_SERVICES_API __declspec(dllimport)
        #define GAME_SERVICES_API_TEMPLATE extern
    #endif
#else
    #define GAME_SERVICES_API
    #define GAME_SERVICES_API_TEMPLATE
#endif
