/**
* Copyright (c) 2013 CD Projekt Red. All Rights Reserved.
*/

#pragma once

#if defined( RED_DLL ) || defined( RED_WITH_DLL )
	#ifdef RED_EXPORT_redNetwork
		#define REDNETWORK_API __declspec(dllexport)
		#define REDNETWORK_API_TEMPLATE
	#else
		#define REDNETWORK_API __declspec(dllimport)
		#define REDNETWORK_API_TEMPLATE extern
	#endif
#else
	#define REDNETWORK_API
	#define REDNETWORK_API_TEMPLATE extern
#endif
