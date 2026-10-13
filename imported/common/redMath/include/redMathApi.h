/**
* Copyright (c) 2013 CD Projekt Red. All Rights Reserved.
*/

#pragma once

#if defined( RED_DLL ) || defined( RED_WITH_DLL )
	#ifdef RED_EXPORT_redMath
		#define REDMATH_API __declspec(dllexport)
		#define REDMATH_API_TEMPLATE
	#else
		#define REDMATH_API __declspec(dllimport)
		#define REDMATH_API_TEMPLATE extern
	#endif
#else
	#define REDMATH_API
	#define REDMATH_API_TEMPLATE extern
#endif
