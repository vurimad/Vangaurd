/**
* Copyright (c) 2016 CD Projekt Red. All Rights Reserved.
*/

#pragma once

#if defined( RED_DLL ) || defined( RED_WITH_DLL )
	#ifdef RED_EXPORT_redCore
		#define REDCORE_API __declspec(dllexport)
		#define REDCORE_API_TEMPLATE
	#else
		#define REDCORE_API __declspec(dllimport)
		#define REDCORE_API_TEMPLATE extern
	#endif
#else
	#define REDCORE_API
	#define REDCORE_API_TEMPLATE extern
#endif
