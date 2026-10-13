/**
* Copyright (c) 2016 CDProjekt Red, Inc. All Rights Reserved.
*/

#pragma once

#if defined( RED_DLL ) || defined( RED_WITH_DLL )
	#ifdef RED_EXPORT_redJobs2
		#define REDJOBS2_API __declspec(dllexport)
		#define REDJOBS2_API_TEMPLATE
	#else
		#define REDJOBS2_API __declspec(dllimport)
		#define REDJOBS2_API_TEMPLATE extern
	#endif
#else
	#define REDJOBS2_API
	#define REDJOBS2_API_TEMPLATE extern
#endif
