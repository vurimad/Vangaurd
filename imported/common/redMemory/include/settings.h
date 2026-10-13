/**
 * Copyright (c) 2015 CD Projekt Red. All Rights Reserved.
 */

#ifndef _RED_MEMORY_SETTINGS_H_
#define _RED_MEMORY_SETTINGS_H_

//////////////////////////////////////////////////////////////////////////

// #define RED_MEMORY_UNIT_TEST										-> Enable this for all target that will compile memory unit test.
// #define RED_MEMORY_ENABLE_HOOKS									-> This is use to hook debug metadata or metrics in the allocation process. This need to be enable or no debug/metrics/metadata will be present. 
// #define RED_MEMORY_ENABLE_METRICS								-> Basic metrics system. Only compute memory allocated per pool by default
// #define RED_MEMORY_ENABLE_EXTENDED_METRICS						-> Add more info about pool allocations. (Like alloc count, alloc per frame etc..)
// #define RED_MEMORY_ENABLE_LOGGING								-> Enable Extended logging. It will forward to System logger.
// #define RED_MEMORY_FORCE_DEBUG_ALLOCATOR
// -> Route all allocation the DebugAllocator. Use with GFlags to trap memory stomp
// #define RED_MEMORY_ALLOW_DEBUG_ALLOCATOR							-> Allow enabling debug allocator per pool, pools should be defined in debugPools.list in the same folder as game. Use with GFlags to trap memory stomp
// #define RED_MEMORY_ENABLE_ASSERTS								-> Compile with assertion code.
// #define RED_MEMORY_ENABLE_FRAME_ALLOCATOR_CHECK					-> Decommit previously used pages when resetting FrameAllocator.
// #define RED_MEMORY_WIPE_MEMORY_ON_ALLOCATE						-> Memset 0 all allocated block. Not compatible with RED_MEMORY_ENABLE_HOOKS.
// #define RED_MEMORY_ENABLE_ADDITIONAL_SLAB_FREE_LIST_PROCESSING	-> Enable additional free list processing for SLAB allocator. It could be useful to track down random free list memory corruptions.
// #define RED_MEMORY_ENABLE_EXTENDED_SLAB_ALLOCATOR_REPORT			-> Enable extended SlabAllocator memory report containig information about free list memory
// #define RED_MEMORY_ENABLE_DEFAULT_ALLOCATION						-> Doing any kind of allocation on Default Pool will not cause compilation failure or assertion of any kind. 
// #define RED_MEMORY_ENABLE_EXTENDED_THREAD_REGISTRATION			-> Bump max thread support from 16 to 32. This introduce most likely one more cache on each slab allocator allocation request.
// #define RED_MEMORY_ENABLE_POOL_VALIDATION_IN_FUNCTION			-> Enable pool validation in red.Function.
// #define RED_MEMORY_ENABLE_POOL_INITALIZATION_VALIDATION			-> Enable checking if allocated memory is taken from initialized pool.
// #define RED_USE_ORBIS_MEMORY_ANALYZER							-> Enable integration with Orbis Memory Analyzer
// #define RED_MEMORY_ENABLE_REPORT									-> Enable memory report

//////////////////////////////////////////////////////////////////////////

#if defined( RED_LOGGING_ENABLED )

	#define RED_MEMORY_ENABLE_LOGGING

#endif

#if defined( RED_PLATFORM_WINPC )
	
	#define RED_MEMORY_ENABLE_EXTENDED_THREAD_REGISTRATION

#endif 

//////////////////////////////////////////////////////////////////////////

#if defined( RED_CONFIGURATION_DEBUG )

	#define RED_MEMORY_UNIT_TEST
	#define RED_MEMORY_ENABLE_METRICS
	#define RED_MEMORY_ENABLE_EXTENDED_METRICS
	#define RED_MEMORY_ENABLE_ASSERTS
	#define RED_MEMORY_ENABLE_FRAME_ALLOCATOR_CHECK
	#define RED_MEMORY_ENABLE_HOOKS	
	#define RED_MEMORY_ENABLE_POOL_VALIDATION_IN_FUNCTION
	#define RED_MEMORY_ENABLE_REPORT
	#define RED_MEMORY_ENABLE_REPORT_EXTRAS
	#define RED_MEMORY_ENABLE_EXTENDED_SLAB_ALLOCATOR_REPORT

	#ifdef RED_PLATFORM_WIN64
	#	define RED_MEMORY_ALLOW_DEBUG_ALLOCATOR
	#endif

	#ifdef RED_PLATFORM_ORBIS
		#define RED_USE_ORBIS_MEMORY_ANALYZER
	#endif

#elif defined( RED_CONFIGURATION_NOPTS )

	#define RED_MEMORY_UNIT_TEST
	#define RED_MEMORY_ENABLE_METRICS
	#define RED_MEMORY_ENABLE_EXTENDED_METRICS
	#define RED_MEMORY_ENABLE_ASSERTS
	#define RED_MEMORY_ENABLE_FRAME_ALLOCATOR_CHECK
	#define RED_MEMORY_ENABLE_HOOKS	
	#define RED_MEMORY_ENABLE_POOL_VALIDATION_IN_FUNCTION
	#define RED_MEMORY_ENABLE_REPORT
	#define RED_MEMORY_ENABLE_REPORT_EXTRAS
	#define RED_MEMORY_ENABLE_EXTENDED_SLAB_ALLOCATOR_REPORT

	#ifdef RED_PLATFORM_WIN64
	#	define RED_MEMORY_ALLOW_DEBUG_ALLOCATOR
	#endif

	#ifdef RED_PLATFORM_ORBIS
		#define RED_USE_ORBIS_MEMORY_ANALYZER
	#endif

#elif defined( RED_CONFIGURATION_RELEASE )

	#define RED_MEMORY_UNIT_TEST		
	#define RED_MEMORY_ENABLE_METRICS	
	#define RED_MEMORY_ENABLE_EXTENDED_METRICS
	#define RED_MEMORY_ENABLE_ASSERTS
	#define RED_MEMORY_ENABLE_HOOKS	
	#define RED_MEMORY_ENABLE_POOL_VALIDATION_IN_FUNCTION
	#define RED_MEMORY_ENABLE_REPORT
	#define RED_MEMORY_ENABLE_EXTENDED_SLAB_ALLOCATOR_REPORT
	#define RED_MEMORY_ENABLE_REPORT_EXTRAS

	#ifdef RED_PLATFORM_WIN64
	#	define RED_MEMORY_ALLOW_DEBUG_ALLOCATOR
	#endif

	#ifdef RED_PLATFORM_ORBIS
		#define RED_USE_ORBIS_MEMORY_ANALYZER
	#endif

#elif defined( RED_CONFIGURATION_FINAL )

	// TODO: disable this before shipping and certification
	#define RED_MEMORY_ENABLE_REPORT
	#define RED_MEMORY_ENABLE_REPORT_EXTRAS

#ifdef USE_PROFILER 
	#define RED_MEMORY_ENABLE_EXTENDED_SLAB_ALLOCATOR_REPORT
#endif

#if defined RED_PLATFORM_ORBIS && defined( USE_PROFILER )
	#define RED_USE_ORBIS_MEMORY_ANALYZER
#endif

 	#define RED_MEMORY_WIPE_MEMORY_ON_ALLOCATE

#endif

//////////////////////////////////////////////////////////////////////////

#endif