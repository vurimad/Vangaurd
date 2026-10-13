/**
 * Copyright (c) 2016 CD Projekt Red. All Rights Reserved.
 */

#ifndef _RED_MEMORY_MUTEX_H_
#define _RED_MEMORY_MUTEX_H_

namespace red
{
namespace memory
{
	class Mutex
	{
	public:
		Mutex();
		~Mutex();

		void Acquire();
		void Release();

	private:

		Mutex( const Mutex & );
		Mutex & operator=( const Mutex& );

#if defined( RED_PLATFORM_WINPC ) || defined( RED_PLATFORM_DURANGO )
		CRITICAL_SECTION m_criticalSection;

#elif defined( RED_PLATFORM_ORBIS )
		ScePthreadMutex	m_mutex;

#elif defined (RED_PLATFORM_LINUX )
		pthread_mutex_t m_mutex;

#endif
	};
}
}

#endif
