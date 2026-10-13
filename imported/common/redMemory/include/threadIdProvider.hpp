/**
 * Copyright (c) 2015 CD Projekt Red. All Rights Reserved.
 */

#ifndef _RED_MEMORY_THREAD_ID_PROVIDER_HPP_
#define _RED_MEMORY_THREAD_ID_PROVIDER_HPP_

namespace red
{
namespace memory
{
	RED_MEMORY_INLINE ThreadId ThreadIdProvider::GetCurrentId() const
	{

#if defined( RED_PLATFORM_WINPC ) || defined( RED_PLATFORM_DURANGO )
		// Don't ask ... 
		// I simply manually inlined ::GetCurrentThreadId().
		i64 value = __readgsqword( 0x30 ); // Thread Information Block start here. 
		const u32 id = *reinterpret_cast< u32* >( value + 0x48 ); // Thread Id is offseted by 0x48.

#elif defined( RED_PLATFORM_ORBIS )
		const ThreadId id = static_cast< ThreadId >( ::scePthreadGetthreadid() );

#elif defined( RED_PLATFORM_LINUX )
		const ThreadId id = static_cast< ThreadId >( ::pthread_self() );
#else
#error Unsupported platform!

#endif

		return id;
	}
}
}

#endif
