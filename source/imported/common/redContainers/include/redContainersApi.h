/**
* Copyright (c) 2013 CDProjekt Red, Inc. All Rights Reserved.
*/

#pragma once

#if defined( RED_DLL ) || defined( RED_WITH_DLL )
	#ifdef RED_EXPORT_redContainers
		#define RED_CONTAINERS_API __declspec(dllexport)
		#define RED_CONTAINERS_API_TEMPLATE
	#else
		#define RED_CONTAINERS_API __declspec(dllimport)
		#define RED_CONTAINERS_API_TEMPLATE extern
	#endif
#else
	#define RED_CONTAINERS_API
	#define RED_CONTAINERS_API_TEMPLATE extern
#endif
