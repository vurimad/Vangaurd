/**
* Copyright (c) 2013 CDProjekt Red, Inc. All Rights Reserved.
*/

#ifndef _RED_ARCHITECTURE_H_
#define _RED_ARCHITECTURE_H_

//////////////////////////////////////////////////////////////////////////
// Compiler
#if !defined( RED_COMPILER_MSC ) && !defined( RED_COMPILER_CLANG )
#	error Unsupported compiler
#endif

#ifdef __clang__
#	if __has_warning("-Winconsistent-missing-override")
#		pragma clang diagnostic ignored "-Winconsistent-missing-override"
#	endif
#	if __has_warning("-Wunused-local-typedef")
#		pragma clang diagnostic ignored "-Wunused-local-typedef"
#	endif
// New clang complains about this in some external GFx4 and physx headers... :(
#	if __has_warning("-Winfinite-recursion")
#		pragma clang diagnostic ignored "-Winfinite-recursion"
#	endif
#	if __has_warning("-Wc++14-extensions")
#		pragma clang diagnostic ignored "-Wc++14-extensions"
#	endif

#endif

//////////////////////////////////////////////////////////////////////////
// Instruction set

#if defined ( __AVX__ )
#define RED_USE_AVX
#define RED_USE_SSE2
#define RED_USE_SSE
#elif defined ( __SSE2__ ) || defined( _M_X64 ) || defined( __x86_64__ ) || _M_IX86_FP > 1
#define RED_USE_SSE2
#define RED_USE_SSE
#elif defined ( __SSE__ ) || defined( _M_X64 ) || defined( __x86_64__ )  || _M_IX86_FP > 0
#define RED_USE_SSE
#error Instructionset below the minimum requirements
#else
RED_MESSAGE( "SIMD instruction set switched off" );
#endif

//////////////////////////////////////////////////////////////////////////
// Operating System
#if defined( _DURANGO )
#	define RED_PLATFORM_DURANGO
#elif defined( _WIN64 )
#	define RED_PLATFORM_WIN64
#elif defined( _WIN32 )
#	define RED_PLATFORM_WIN32
#elif defined( __linux__ )
#	define RED_PLATFORM_LINUX
#	if defined( __x86_64__ )
#		define RED_PLATFORM_LINUX64
#	else
#		define RED_PLATFORM_LINUX32
#	endif
#elif defined( __ORBIS__ )
#	include <sdk_version.h>
//	Need a better place for this check, but not checking if less-than as a 
// forceful reminder to update the min SDK version when we update the SDK
#	if !defined( SCE_ORBIS_SDK_VERSION )
#		error PS4 SDK version not defined? Failed to include sdk_version.h?
#	elif SCE_ORBIS_SDK_VERSION < 0x03508051u
#		error Unsupported PS4 SDK version. Either upgrade your SDK to v3.5+ or update this check.
#	endif
// Extra annoying checks if we need to make sure the right compiler is being used. Should really check features, though.
// #	if !defined( __clang__ )
// #		error Not using clang on Orbis?
// #	elif __clang_major__ < 3
// #		error Clang major version < 3
// #	elif __clang_minor__ < 5
// #		error Clang minor version < 5
// #	elif __clang_patchlevel__ < 0
// #		error Clang patchlevel < 0
// #	endif
#	define RED_PLATFORM_ORBIS
#else
#	error Undefined operating system
#endif

#if defined( RED_PLATFORM_DURANGO ) || defined( RED_PLATFORM_ORBIS )
#	define RED_PLATFORM_CONSOLE
#endif

#if defined( RED_PLATFORM_WIN32 ) || defined( RED_PLATFORM_WIN64 )
#	define RED_PLATFORM_WINPC
#endif

//////////////////////////////////////////////////////////////////////////
// Architecture
#if defined( _M_X64 ) || defined( __x86_64__ )
#	define RED_ARCH_X64
#	define RED_ENDIAN_LITTLE
#elif defined( _M_IA64 )
#	define RED_ARCH_IA64
#elif defined( _M_IX86 ) || defined( __i386__ ) || defined( __i386 )
#	define RED_ARCH_X86
#	define RED_ENDIAN_LITTLE
#else
#	error Undefined processor architecture
#endif

#ifdef RED_COMPILER_CLANG
#	ifdef RED_PLATFORM_LINUX
#		ifndef RED_USE_AVX
#			error Proper intrsinics not enabled. Try -march=core-avx2 on the command line or fix bitUtils.h to fallback on other methods!
#		endif
#	endif // RED_PLATFORM_LINUX
#	include <x86intrin.h>
#endif

// All byteswapping is ifdefed out with this at the moment
// this should only happen in the cooker (if ever) and never in the game
// #define RED_ENDIAN_SWAP_SUPPORT_DEPRECATED

//////////////////////////////////////////////////////////////////////////
// DLL HACKS

#if defined(RED_COMPILER_MSC) && ( defined(RED_DLL) || defined( RED_WITH_DLL ) )
	#pragma warning( disable: 4275 ) // base class does not have dllExport
    #pragma warning( disable: 4251) // class '' needs to have dll-interface to be used by clients of class ''
	#pragma warning( disable: 4091 ) // '__declspec(dllexport)' : ignored on left of '' when no variable is declared
#endif

#endif //_RED_ARCHITECTURE_H_
