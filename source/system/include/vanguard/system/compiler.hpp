#pragma once

#if defined(_MSC_VER)
#define VG_COMPILER_MSVC 1
#else
#define VG_COMPILER_MSVC 0
#endif

#if defined(__clang__)
#define VG_COMPILER_CLANG 1
#else
#define VG_COMPILER_CLANG 0
#endif

#if defined(__GNUC__) && !defined(__clang__)
#define VG_COMPILER_GCC 1
#else
#define VG_COMPILER_GCC 0
#endif

#if !VG_COMPILER_MSVC && !VG_COMPILER_CLANG && !VG_COMPILER_GCC
#error Vanguard does not support this compiler.
#endif
