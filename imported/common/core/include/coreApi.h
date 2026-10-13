/**
 * Copyright (c) 2016 CD Projekt Red. All Rights Reserved.
 */

#pragma once

#if defined( RED_DLL ) || defined( RED_WITH_DLL )
	#ifdef RED_EXPORT_core
		#define RED_CORE_API __declspec(dllexport)
		#define RED_CORE_API_TEMPLATE
	#else
		#define RED_CORE_API __declspec(dllimport)
		#define RED_CORE_API_TEMPLATE extern	
	#endif
#else
	#define RED_CORE_API 
	#define RED_CORE_API_TEMPLATE extern
#endif
