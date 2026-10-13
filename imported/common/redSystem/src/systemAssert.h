/**
 * Copyright (c) 2016 CD Projekt Red. All Rights Reserved.
 */

#ifndef _RED_SYSTEM_ASSERT_H_
#define _RED_SYSTEM_ASSERT_H_

#include "../include/log.h"

#ifdef RED_ASSERTS_ENABLED
	#define RED_SYSTEM_ASSERT( expression, message, ... ) do { if( !( expression ) ) { RED_LOG_ERROR( "%hs: " message, #expression, ##__VA_ARGS__ ); RED_DEBUG_BREAK(); } } while ( (void)0,0 )
	#define RED_SYSTEM_VERIFY( expression, message, ... ) do { if( !( expression ) ) { RED_LOG_ERROR( "%hs: " message, #expression, ##__VA_ARGS__ ); } } while ( (void)0,0 )
#else
	#define RED_SYSTEM_ASSERT( expression, message, ... ) do { } while ( (void)0,0 )
	#define RED_SYSTEM_VERIFY( expression, message, ... ) do { if( !( expression ) ) {} } while ( (void)0,0 )
#endif

#endif
