/**
* Copyright (c) 2016 CD Projekt Red. All Rights Reserved.
*/

#pragma once

#if defined( RED_DLL ) || defined( RED_WITH_DLL )
	#ifdef RED_EXPORT_redCrypto
		#define REDCRYPTO_API __declspec(dllexport)
		#define REDCRYPTO_API_TEMPLATE
	#else
		#define REDCRYPTO_API __declspec(dllimport)
		#define REDCRYPTO_API_TEMPLATE extern
	#endif
#else
	#define REDCRYPTO_API
	#define REDCRYPTO_API_TEMPLATE extern
#endif
