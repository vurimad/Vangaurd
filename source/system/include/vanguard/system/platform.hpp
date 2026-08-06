#pragma once

#if defined(_WIN32)
#define VG_PLATFORM_WINDOWS 1
#else
#define VG_PLATFORM_WINDOWS 0
#endif

#if defined(__linux__)
#define VG_PLATFORM_LINUX 1
#else
#define VG_PLATFORM_LINUX 0
#endif

#if !VG_PLATFORM_WINDOWS && !VG_PLATFORM_LINUX
#error Vanguard does not support this operating system.
#endif

#if defined(_M_X64) || defined(__x86_64__)
#define VG_ARCH_X64 1
#else
#define VG_ARCH_X64 0
#endif

#if defined(_M_ARM64) || defined(__aarch64__)
#define VG_ARCH_ARM64 1
#else
#define VG_ARCH_ARM64 0
#endif

#if !VG_ARCH_X64 && !VG_ARCH_ARM64
#error Vanguard requires a supported 64-bit architecture.
#endif

#if defined(__BYTE_ORDER__) && (__BYTE_ORDER__ == __ORDER_BIG_ENDIAN__)
#define VG_ENDIAN_BIG 1
#define VG_ENDIAN_LITTLE 0
#else
#define VG_ENDIAN_BIG 0
#define VG_ENDIAN_LITTLE 1
#endif
