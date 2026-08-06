#pragma once

#define BUILD "vanguard"
#define HIDDEN
#if defined(_MSC_VER)
#define INLINE __forceinline
#define THREAD_LOCAL __declspec(thread)
#else
#define INLINE inline __attribute__((always_inline))
#define THREAD_LOCAL _Thread_local
#endif
#define PACKAGE_NAME "libjpeg-turbo"
#define VERSION "3.2.0"
#if defined(_WIN64) || defined(__x86_64__) || defined(__aarch64__)
#define SIZEOF_SIZE_T 8
#else
#define SIZEOF_SIZE_T 4
#endif
#define HAVE_INTRIN_H 1
#define FALLTHROUGH
#define C_ARITH_CODING_SUPPORTED 1
#define D_ARITH_CODING_SUPPORTED 1
