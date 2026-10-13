/**
* Copyright (c) 2016 CD Projekt Red. All Rights Reserved.
*/

#pragma once

#if defined( RED_DLL ) || defined( RED_WITH_DLL )
	#ifdef RED_EXPORT_backendMaterial
		#define BACKEND_MATERIAL_API __declspec(dllexport)
		#define BACKEND_MATERIAL_API_TEMPLATE
	#else
		#define BACKEND_MATERIAL_API __declspec(dllimport)
		#define BACKEND_MATERIAL_API_TEMPLATE extern
	#endif
#else
	#define BACKEND_MATERIAL_API 
	#define BACKEND_MATERIAL_API_TEMPLATE extern
#endif