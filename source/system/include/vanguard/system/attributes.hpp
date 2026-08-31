#pragma once

#include <vanguard/system/compiler.hpp>

#define VG_CONCATENATE_IMPL(left, right) left##right
#define VG_CONCATENATE(left, right) VG_CONCATENATE_IMPL(left, right)
#define VG_UNIQUE_NAME(prefix) VG_CONCATENATE(prefix, __COUNTER__)

#define VG_UNUSED(value) static_cast<void>(value)

#if VG_COMPILER_MSVC
#define VG_FORCE_INLINE __forceinline
#define VG_NOINLINE __declspec(noinline)
#define VG_RESTRICT __restrict
#define VG_ASSUME(condition) __assume(condition)
#else
#define VG_FORCE_INLINE inline __attribute__((always_inline))
#define VG_NOINLINE __attribute__((noinline))
#define VG_RESTRICT __restrict__
#define VG_ASSUME(condition)                                                                                                                                   \
    do                                                                                                                                                         \
    {                                                                                                                                                          \
        if (!(condition))                                                                                                                                      \
            __builtin_unreachable();                                                                                                                           \
    } while (false)
#endif

#if VG_COMPILER_CLANG || VG_COMPILER_GCC
#define VG_LIKELY(condition) __builtin_expect(!!(condition), 1)
#define VG_UNLIKELY(condition) __builtin_expect(!!(condition), 0)
#else
#define VG_LIKELY(condition) (condition)
#define VG_UNLIKELY(condition) (condition)
#endif
