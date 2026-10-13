/**
* Copyright (c) 2016 CDProjekt Red, Inc. All Rights Reserved.
*/

#pragma once

// this makes sure this header is NOT included directly from outside core
#ifndef RED_MODULE_redImage
	#error "Do not include internal headers directly, please use redImage.h"
#endif

#if defined( VANGUARD_ASSET_HOST )
#include <new>
#include <type_traits>
#include <utility>
#include "../include/redImagePublic.h"
#else
#include "redImageInternal.h"
#endif
