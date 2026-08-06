/**
* Copyright (c) 2013 CDProjekt Red, Inc. All Rights Reserved.
*/

#ifndef _RED_SYSTEM_THREADS_H_
#define _RED_SYSTEM_THREADS_H_

#include "redSystemPublic.h"

#if defined( RED_COMPILER_MSC )
#	define RED_TLS __declspec(thread)
#elif defined( RED_COMPILER_CLANG )
#	define RED_TLS __thread
#else
#	error Compiler not supported
#endif

namespace red
{
	struct ThreadId
	{
		constexpr ThreadId() : id( 0 ) {}
		explicit constexpr ThreadId(Uint32 tId) : id(tId) {}
		Uint32 id;

		RED_INLINE Bool operator==( const ThreadId& other ) const
		{
			return id == other.id;
		}

		RED_INLINE Bool operator!=( const ThreadId& other ) const
		{
			return !( *this == other );
		}

		RED_INLINE Uint32 AsNumber() const
		{
			return (Uint32) id;
		}

		void InitWithCurrentThread()
		{
#if defined( RED_PLATFORM_WIN32 ) || defined( RED_PLATFORM_WIN64 ) || defined( RED_PLATFORM_DURANGO )
			// Don't ask ... 
			// I simply manually inlined ::GetCurrentThreadId().
			Int64 value = __readgsqword( 0x30 ); // Thread Information Block start here. 
			id = *reinterpret_cast<Uint32*>(value + 0x48); // Thread Id is offseted by 0x48.
#elif defined( RED_PLATFORM_ORBIS )
			id = static_cast< Uint32 >( ::scePthreadGetthreadid() );
#elif defined( RED_PLATFORM_LINUX )
			id = static_cast< Uint32 >( ::pthread_self() );
#else
# error Unsupported platform!
#endif
		}

		RED_INLINE Bool IsValid() const 
		{
			return id != 0;
		}

		static ThreadId CurrentThread()
		{
			ThreadId threadId;
			threadId.InitWithCurrentThread();
			return threadId;
		}
	};
}

#endif //_RED_SYSTEM_THREADS_H_
