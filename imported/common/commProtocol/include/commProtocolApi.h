/**
* Copyright (c) 2013 CD Projekt Red. All Rights Reserved.
*/

#pragma once

#if defined( RED_DLL ) || defined( RED_WITH_DLL )
	#ifdef RED_EXPORT_commProtocol
		#define COMMPROTOCOL_API __declspec(dllexport)
		#define COMMPROTOCOL_API_TEMPLATE
	#else
		#define COMMPROTOCOL_API __declspec(dllimport)
		#define COMMPROTOCOL_API_TEMPLATE extern
	#endif
#else
	#define COMMPROTOCOL_API
	#define COMMPROTOCOL_API_TEMPLATE extern
#endif
