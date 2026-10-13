/**
 * Copyright (c) 2016 CD Projekt Red. All Rights Reserved.
 */

#pragma once

#if defined( RED_DLL ) || defined( RED_WITH_DLL )
	#ifdef RED_EXPORT_redMemory
		#define RED_MEMORY_API __declspec(dllexport)
		#define RED_MEMORY_API_TEMPLATE
	#else
		#define RED_MEMORY_API __declspec(dllimport)
		#define RED_MEMORY_API_TEMPLATE extern	
	#endif
#else
	#define RED_MEMORY_API 
	#define RED_MEMORY_API_TEMPLATE extern
#endif
