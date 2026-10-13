/**
* Copyright (c) 2016 CD Projekt Red. All Rights Reserved.
*/

#pragma once

#if defined( RED_DLL ) || defined( RED_WITH_DLL )
	#ifdef RED_EXPORT_engine
		#define RED_ENGINE_API __declspec(dllexport)
		#define RED_ENGINE_API_TEMPLATE
	#else
		#define RED_ENGINE_API __declspec(dllimport)
		#define RED_ENGINE_API_TEMPLATE extern
	#endif
#else
	#define RED_ENGINE_API
	#define RED_ENGINE_API_TEMPLATE
#endif
