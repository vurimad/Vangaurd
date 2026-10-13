/**
* Copyright (c) 2013 CD Projekt Red. All Rights Reserved.
*/

#pragma once

#if defined( RED_DLL ) || defined( RED_WITH_DLL )
	#ifdef RED_EXPORT_commChannel
		#define COMMCHANNEL_API __declspec(dllexport)
		#define COMMCHANNEL_API_TEMPLATE
	#else
		#define COMMCHANNEL_API __declspec(dllimport)
		#define COMMCHANNEL_API_TEMPLATE extern
	#endif
#else
	#define COMMCHANNEL_API
	#define COMMCHANNEL_API_TEMPLATE extern
#endif
