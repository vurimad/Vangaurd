/**
* Copyright (c) 2007-2016 CD Projekt Red. All Rights Reserved.
*/
#pragma once

#if defined( RED_DLL ) || defined( RED_WITH_DLL )
	#ifdef RED_EXPORT_renderBackend
		#define RENDERBACKEND_API __declspec(dllexport)
		#define RENDERBACKEND_API_TEMPLATE
	#else
		#define RENDERBACKEND_API __declspec(dllimport)
		#define RENDERBACKEND_API_TEMPLATE extern
	#endif
#else
	#define RENDERBACKEND_API
	#define RENDERBACKEND_API_TEMPLATE extern
#endif