/**
* Copyright (c) 2016 CDProjekt Red, Inc. All Rights Reserved.
*/

#pragma once

#if defined( RED_DLL ) || defined( RED_WITH_DLL )
	#ifdef RED_EXPORT_redImage
		#define REDIMAGE_API __declspec(dllexport)
		#define REDIMAGE_API_TEMPLATE
	#else
		#define REDIMAGE_API __declspec(dllimport)
		#define REDIMAGE_API_TEMPLATE extern
	#endif
#else
	#define REDIMAGE_API
	#define REDIMAGE_API_TEMPLATE extern
#endif
