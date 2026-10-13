/**
* Copyright (c) 2016 CD Projekt Red. All Rights Reserved.
*/

#pragma once

#if defined( RED_DLL ) || defined( RED_WITH_DLL )
	#ifdef RED_EXPORT_redCompression
		#define REDCOMPRESSION_API __declspec(dllexport)
		#define REDCOMPRESSION_API_TEMPLATE
	#else
		#define REDCOMPRESSION_API __declspec(dllimport)
		#define REDCOMPRESSION_API_TEMPLATE extern
	#endif
#else
	#define REDCOMPRESSION_API
	#define REDCOMPRESSION_API_TEMPLATE extern
#endif
