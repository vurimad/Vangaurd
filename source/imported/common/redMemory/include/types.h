/**
 * Copyright (c) 2015 CD Projekt Red. All Rights Reserved.
 */

#ifndef _RED_MEMORY_TYPES_H_
#define _RED_MEMORY_TYPES_H_

namespace red
{
namespace memory
{
	typedef red::Int8 i8;
	typedef red::Uint8 u8;

	typedef red::Int16 i16;
	typedef red::Uint16 u16;

	typedef red::Int32 i32;
	typedef red::Uint32 u32;

	typedef red::Int64 i64;
	typedef red::Uint64 u64;

	typedef red::Float f32;
	typedef red::Double f64;

#if defined( RED_MEMORY_ENABLE_REPORT )
	extern FILE* s_oomFile;
#endif
}
}

#define RED_MEMORY_INLINE inline

#ifdef RED_MEMORY_ENABLE_REPORT

#define RED_MEMORY_LOG_REPORT( message, ... )	do { if( s_oomFile ) { std::fprintf( s_oomFile, message, ##__VA_ARGS__ ); std::fputs( "\n", s_oomFile ); } } while ( ( void )0, 0 )
#define RED_MEMORY_FLUSH_REPORT()				do { if( s_oomFile ) { std::fflush( s_oomFile ); } } while ( ( void )0, 0 )

#ifndef RED_LOGGING_ENABLED

#define RED_MEMORY_LOG RED_MEMORY_LOG_REPORT
#define RED_MEMORY_LOG_FLUSH() RED_MEMORY_FLUSH_REPORT()

#else

#define RED_MEMORY_LOG( message, ... ) \
do \
{ \
	RED_LOG( message, ##__VA_ARGS__ ); \
	RED_MEMORY_LOG_REPORT( message, ##__VA_ARGS__ ); \
} while( (void)0,0 )

#define RED_MEMORY_LOG_FLUSH() \
do \
{ \
	RED_LOG_FLUSH_AND_WAIT(); \
	RED_MEMORY_FLUSH_REPORT(); \
} while( (void)0,0 )

#endif

#else

#define RED_MEMORY_LOG( message, ... )	do { } while ( (void)0,0 )
#define RED_MEMORY_LOG_FLUSH()			do { } while ( (void)0,0 )

#endif

#endif
