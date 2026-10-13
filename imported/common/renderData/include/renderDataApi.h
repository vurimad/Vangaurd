/**
* Copyright (c) 2016 CD Projekt Red. All Rights Reserved.
*/

#pragma once

#if defined( RED_DLL ) || defined( RED_WITH_DLL )
	#ifdef RED_EXPORT_renderData
		#define RENDER_DATA_API __declspec(dllexport)
		#define RENDER_DATA_API_TEMPLATE
	#else
		#define RENDER_DATA_API __declspec(dllimport)
		#define RENDER_DATA_API_TEMPLATE extern
	#endif
#else
	#define RENDER_DATA_API
	#define RENDER_DATA_API_TEMPLATE extern
#endif
