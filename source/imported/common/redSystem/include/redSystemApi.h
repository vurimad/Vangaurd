/**
* Copyright (c) 2013 CDProjekt Red, Inc. All Rights Reserved.
*/

#pragma once

#if defined( RED_DLL ) || defined( RED_WITH_DLL )
	#ifdef RED_EXPORT_redSystem
		#define REDSYSTEM_API __declspec(dllexport)
		#define REDSYSTEM_API_TEMPLATE
	#else
		#define REDSYSTEM_API __declspec(dllimport)
		#define REDSYSTEM_API_TEMPLATE extern
	#endif
#else
	#define REDSYSTEM_API
	#define REDSYSTEM_API_TEMPLATE extern
#endif
