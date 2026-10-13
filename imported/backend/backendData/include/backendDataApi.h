/**
* Copyright (c) 2016 CD Projekt Red. All Rights Reserved.
*/

#pragma once

#if defined( RED_DLL ) || defined( RED_WITH_DLL )
	#ifdef RED_EXPORT_backendData
		#define BACKEND_DATA_API __declspec(dllexport)
		#define BACKEND_DATA_API_TEMPLATE
	#else
		#define BACKEND_DATA_API __declspec(dllimport)
		#define BACKEND_DATA_API_TEMPLATE extern
	#endif
#else
	#define BACKEND_DATA_API
	#define BACKEND_DATA_API_TEMPLATE extern
#endif
