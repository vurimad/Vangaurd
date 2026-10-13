/**
* Copyright (c) 2015 CD Projekt Red. All Rights Reserved.
*/

#ifndef _RED_MEMORY_ASSERT_H
#define _RED_MEMORY_ASSERT_H


#define ALWAYSENABLED_RED_MEMORY_FATAL_ASSERT( expression, ... )	ALWAYSENABLED_RED_FATAL_ASSERT( expression, ## __VA_ARGS__ )
#define ALWAYSENABLED_RED_MEMORY_FATAL( message, ... )				ALWAYSENABLED_RED_FATAL( message, ## __VA_ARGS__ )


#ifdef RED_MEMORY_ENABLE_ASSERTS

	#define RED_MEMORY_ASSERT( condition, txt, ... )	RED_FATAL_ASSERT( condition, txt, ## __VA_ARGS__ )
	#define RED_MEMORY_HALT( txt, ... )					RED_FATAL( txt, ## __VA_ARGS__ )

#else

	#define RED_MEMORY_ASSERT( ... )	do { } while ( (void)0,0 )
	#define RED_MEMORY_HALT( ... )		do { } while ( (void)0,0 )

#endif

#endif