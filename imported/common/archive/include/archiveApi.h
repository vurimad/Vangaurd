/**
* Copyright (c) 2007-2016 CD Projekt Red. All Rights Reserved.
*/
#pragma once
 
#if defined( RED_DLL ) || defined( RED_WITH_DLL )
    #ifdef RED_EXPORT_archive
        #define ARCHIVE_API __declspec(dllexport)
		#define ARCHIVE_API_TEMPLATE
    #else
        #define ARCHIVE_API __declspec(dllimport)
		#define ARCHIVE_API_TEMPLATE extern
    #endif
#else
    #define ARCHIVE_API
	#define ARCHIVE_API_TEMPLATE extern
#endif