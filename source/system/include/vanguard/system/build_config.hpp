#pragma once

#ifndef VG_BUILD_DEBUG
#define VG_BUILD_DEBUG 0
#endif

#ifndef VG_BUILD_DEVELOPMENT
#define VG_BUILD_DEVELOPMENT 0
#endif

#ifndef VG_BUILD_PROFILE
#define VG_BUILD_PROFILE 0
#endif

#ifndef VG_BUILD_SHIPPING
#define VG_BUILD_SHIPPING 0
#endif

#ifndef VG_ENABLE_ASSERTS
#define VG_ENABLE_ASSERTS 0
#endif

#if (VG_BUILD_DEBUG + VG_BUILD_DEVELOPMENT + VG_BUILD_PROFILE + VG_BUILD_SHIPPING) != 1
#error Exactly one Vanguard build configuration must be selected.
#endif
