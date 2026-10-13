/**
* Copyright (c) 2013 CDProjekt Red, Inc. All Rights Reserved.
*/

#pragma once
#ifndef _RED_SYSTEM_SETTINGS_H_
#define _RED_SYSTEM_SETTINGS_H_

#include "architecture.h"

#if defined( RED_CONFIGURATION_DEBUG )

	#define RED_LOGGING_ENABLED
	#define RED_ASSERTS_ENABLED
	#define RED_NETWORK_ENABLED
	#define RED_USE_ERRORHANDLER
	#define USE_PROFILER

#elif defined( RED_CONFIGURATION_NOPTS )

	#define RED_LOGGING_ENABLED
	#define RED_ASSERTS_ENABLED
	#define RED_NETWORK_ENABLED
	#define RED_USE_ERRORHANDLER
	#define USE_PROFILER

#elif defined( RED_CONFIGURATION_RELEASE )
	
	#define RED_LOGGING_ENABLED
	#define RED_ASSERTS_ENABLED
	#define RED_NETWORK_ENABLED
	#define RED_USE_ERRORHANDLER
	#define USE_PROFILER

#elif defined( RED_CONFIGURATION_FINAL )

	#define RED_USE_ERRORHANDLER

#endif

#ifdef USE_PROFILER

	#define USE_RED_PROFILER
	#define	USE_RED_INGAME_PROFILER

	#if defined(RED_PLATFORM_WINPC)
		#define USE_NVIDIA_PROFILER
		//#define USE_TRACY_PROFILER
		#if !defined(RED_VANGUARD_DISABLE_VTUNE)
			#define USE_VTUNE_PROFILER
		#endif
	#elif defined(RED_PLATFORM_DURANGO)
		#define USE_RED_PROFILER
		#define USE_PIX_PROFILER
	#elif defined(RED_PLATFORM_ORBIS)
		#define USE_RAZOR_PROFILER
	#endif

	#ifdef USE_RED_PROFILER
		#define NEW_PROFILER_ENABLED
	#endif

	#ifdef USE_RED_INGAME_PROFILER
	#ifdef RED_MEMORY_ENABLE_EXTENDED_METRICS
		#define USE_RED_INGAME_MEMORY_PROFILER
	#endif 
	#endif 

#endif


#endif // _RED_SYSTEM_SETTINGS_H_
