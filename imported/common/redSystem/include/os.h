/**
* Copyright (c) 2013 CDProjekt Red, Inc. All Rights Reserved.
*/

#ifndef _RED_OS_H_
#define _RED_OS_H_

#if defined( RED_CONFIGURATION_DEBUG )
# if defined( RED_COMPILER_MSC ) && !defined( __MSVC_RUNTIME_CHECKS )
# error Runtime checks are disabled in debug! Fix premake use of basicruntimechecks (vs2010overrides option that overrides module vc2010.basicRuntimeChecks)
# endif
#endif

#if defined( RED_COMPILER_CLANG ) && !defined( __FAST_MATH__  )
# error Fastmath is disabled
#endif

#if defined( RED_PLATFORM_WIN32 ) || defined ( RED_PLATFORM_WIN64 )

# pragma warning( push )
# pragma warning( disable : 4668 ) // 'symbol' is not defined as a preprocessor macro, replacing with '0' for 'directives'

# ifdef _CPPUNWIND
//# 	error Exception handling enabled. Are the shared properties in the project file broken?
# endif

// Exceptions from the STL disabled
# if !defined( _HAS_EXCEPTIONS ) || ( _HAS_EXCEPTIONS != 0 )
#  error _HAS_EXCEPTIONS != 0. Are the shared properties in the project file broken?
# endif

# define _WIN32_WINNT		_WIN32_WINNT_WIN7
# define NTDDI_VERSION	NTDDI_WIN7
# ifndef NOMINMAX
#  define NOMINMAX
# endif

# define NOATOM
//#	define NOMEMMGR		// Used in a couple of places
//#	define NOMENUS		// Used by output.cpp
//#	define NOMETAFILE	// Used by GDI+
# define NOOPENFILE
# define NOSERVICE
# define NOHELP
//#	define NOTEXTMETRIC // Used by Direct X
# define NOWH
# define NOMCX

# include <SdkDdkVer.h>
# include <windows.h>

# pragma warning( pop )

#elif defined( RED_PLATFORM_ORBIS )
# include <kernel.h>
# include <scebase.h>

RED_DISABLE_WARNING_CLANG( "-Wunused-function" );
RED_DISABLE_WARNING_CLANG( "-Wunused-variable" );

// Try to have some warning parity with Win32. Static analysis could still always be used to cleanup code in general, RED_UNUSED/RED_TOUCH macro everywhere make a mess.
RED_DISABLE_WARNING_CLANG( "-Wunused-private-field" ); // often warns for padding or debug variables
# if __has_warning("-Wunused-lambda-capture")
RED_DISABLE_WARNING_CLANG("-Wunused-lambda-capture")
# endif

#elif defined( RED_PLATFORM_DURANGO )
# pragma warning( push )
# pragma warning( disable : 4668 ) // 'symbol' is not defined as a preprocessor macro, replacing with '0' for 'directives'
# include <xdk.h>

# define NOMINMAX
# define NOATOM
//#	define NOMEMMGR		// Used in a couple of places
//#	define NOMENUS		// Used by output.cpp
//#	define NOMETAFILE	// Used by GDI+
# define NOOPENFILE
# define NOSERVICE
# define NOHELP
//#	define NOTEXTMETRIC // Used by Direct X
# define NOWH
# define NOMCX

# include <Windows.h>
# pragma warning( pop )

#elif defined( RED_PLATFORM_LINUX )
#	ifndef _GNU_SOURCE
#		define _GNU_SOURCE
#	endif
#	ifndef _LARGEFILE_SOURCE
#		define _LARGEFILE_SOURCE
#	endif
#	ifndef _LARGEFILE64_SOURCE
#		define _LARGEFILE64_SOURCE
#	endif
#	if defined(_FILE_OFFSET_BITS)
#		if _FILE_OFFSET_BITS != 64
#			error _FILE_OFFSET_BITS pre-defined to unsupported value!
#		endif
#	else
#		define _FILE_OFFSET_BITS 64
#	endif
#	include <fcntl.h>
#	include <unistd.h>
#	include <time.h>
#	include <sys/types.h>
#	include <sys/syscall.h>
#	include <sys/stat.h>
#	include <sys/time.h>
	// where should I actually put these?
#	include <cstdarg> 
#	include <utility>
#	include <pthread.h>

#else
#	error Undefined Architecture
#endif

#endif // _RED_OS_H_
