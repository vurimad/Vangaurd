/**
* Copyright © 2013 CD Projekt Red. All Rights Reserved.
*/
#ifndef RED_THREADS_TYPES_H
#define RED_THREADS_TYPES_H
#pragma once

namespace red {

#if defined( RED_THREADS_PLATFORM_WINDOWS_API ) 
	typedef DWORD				TSpinCount;
	typedef DWORD				TTimespec;
#elif defined( RED_THREADS_PLATFORM_ORBIS_API )
	typedef int					TSpinCount;
	typedef	SceKernelUseconds	TTimespec; // a Uint32
#elif defined( RED_PLATFORM_LINUX )
	typedef int					TSpinCount;
	typedef	unsigned int		TTimespec; // a Uint32
#else
#	error Platform not defined
#endif

	typedef Uint32	TStackSize; // Unsigned 32 bit LCD between the Windows and Pthreads API

#if defined( RED_PLATFORM_ORBIS ) || defined( RED_PLATFORM_LINUX )
	typedef red::Uint64 TAffinityMask;
#elif defined( RED_PLATFORM_WIN32 ) || defined( RED_PLATFORM_WIN64 ) || defined( RED_PLATFORM_DURANGO )
	typedef DWORD_PTR			TAffinityMask;
#else
#error Platform not supported
#endif

} // namespace red

#endif // RED_THREADS_TYPES_H