/**
 * Copyright (c) 2016 CD Projekt Red. All Rights Reserved.
 */

#ifndef _RED_MEMORY_SCOPED_LOCK_H_
#define _RED_MEMORY_SCOPED_LOCK_H_

namespace red
{
namespace memory
{
	template< typename T >
	class ScopedLock
	{
	public:
		explicit ScopedLock( T& syncObject );
		~ScopedLock();

	private:

		ScopedLock( const ScopedLock& );
		ScopedLock & operator=( const ScopedLock & );

		T * m_object;
	};

	template< typename T >
	class ScopedSharedLock
	{
	public:
		explicit ScopedSharedLock( T& syncObject );
		~ScopedSharedLock();

	private:

		ScopedSharedLock( const ScopedSharedLock& );
		ScopedSharedLock & operator=( const ScopedSharedLock & );

		T * m_object;
	};
}
}

#include "scopedLock.hpp"

#endif
