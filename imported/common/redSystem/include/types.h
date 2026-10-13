/**
* Copyright (c) 2013 CDProjekt Red, Inc. All Rights Reserved.
*/

#pragma once

// For size_t, ptrdiff_t
#include <cstddef>

// For intptr_t, uintptr_t
#include <cstdint>
#include <cinttypes>

// For _SCE_BREAK
#if defined( RED_PLATFORM_ORBIS )
	#include <libdbg.h>
#endif

// Temporary namespace
namespace red
{
	typedef bool				Bool;

	typedef char				Int8;
	typedef unsigned char		Uint8;

	typedef std::int16_t		Int16;
	typedef std::uint16_t		Uint16;

	typedef std::int32_t		Int32;
	typedef std::uint32_t		Uint32;

	typedef std::int64_t		Int64;
	typedef std::uint64_t		Uint64;

	typedef float				Float;
	typedef double				Double;

	typedef wchar_t				UniChar;
	typedef char				AnsiChar;

	//////////////////////////////////////////////////////////////////////////
	// Unicode
#if defined( UNICODE )

	typedef UniChar				Char;
#	define TXT(s)				L##s

#else
	// Prevent mix and matching across projects.
#error Should be using Unicode but not defined
	typedef AnsiChar			Char;
#	define TXT(s)				s

#endif

	//////////////////////////////////////////////////////////////////////////
	// Type definitions

	typedef size_t				MemSize;
	typedef ptrdiff_t			MemDiff;
	typedef intptr_t			MemInt;
	typedef	uintptr_t			MemUint;
}

typedef red::Bool Bool;
typedef red::Int8 Int8;
typedef red::Uint8 Uint8;
typedef red::Int16 Int16;
typedef red::Uint16 Uint16;
typedef red::Int32 Int32;
typedef red::Uint32 Uint32;
typedef red::Int64 Int64;
typedef red::Uint64 Uint64;
typedef red::Int32 Int32;
typedef red::Uint32 Uint32;
typedef red::Float Float;
typedef red::Double Double;
typedef red::UniChar UniChar;
typedef red::AnsiChar AnsiChar;
typedef red::Char Char;
