/**
* Copyright (c) 2017 CD Projekt Red. All Rights Reserved.
*/

#pragma once

#if defined( RED_CONFIGURATION_DEBUG )

	#define RED_DEBUG_SERVER_ENABLED
	
	// ctremblay: This settings is use for Entity, Component and node render debug. Probably should be move to World project level.
	#define RED_ENABLE_RENDER_DEBUG 

#elif defined( RED_CONFIGURATION_NOPTS )

	#define RED_DEBUG_SERVER_ENABLED
	#define RED_ENABLE_RENDER_DEBUG 

#elif defined( RED_CONFIGURATION_RELEASE )
	
	#define RED_DEBUG_SERVER_ENABLED
	#define RED_ENABLE_RENDER_DEBUG 

#elif defined( RED_CONFIGURATION_FINAL )
	
#endif
