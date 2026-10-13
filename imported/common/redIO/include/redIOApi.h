/**
* Copyright (c) 2013 CD Projekt Red. All Rights Reserved.
*/

#pragma once

#if defined( RED_DLL ) || defined( RED_WITH_DLL )
	#ifdef RED_EXPORT_redIO
		#define REDIO_API __declspec(dllexport)
		#define REDIO_API_TEMPLATE
	#else
		#define REDIO_API __declspec(dllimport)
		#define REDIO_API_TEMPLATE extern
	#endif
#else
	#define REDIO_API 
	#define REDIO_API_TEMPLATE extern
#endif
