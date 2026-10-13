/**
* Copyright (c) 2016 CD Projekt Red. All Rights Reserved.
*/

#pragma once

#if defined( RED_DLL ) || defined( RED_WITH_DLL )
	#ifdef RED_EXPORT_backendMesh
		#define BACKEND_MESH_API __declspec(dllexport)
		#define BACKEND_MESH_API_TEMPLATE
	#else
		#define BACKEND_MESH_API __declspec(dllimport)
		#define BACKEND_MESH_API_TEMPLATE extern
	#endif
#else
	#define BACKEND_MESH_API
	#define BACKEND_MESH_API_TEMPLATE extern
#endif
