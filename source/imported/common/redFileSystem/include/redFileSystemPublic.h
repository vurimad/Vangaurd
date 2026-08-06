/**
* Copyright (c)2017 CD Projekt Red. All Rights Reserved.
*/

#pragma once

#include "redFileSystemApi.h"

// to avoid huge amount of changes after cleaning up this file
using red::Utf8String;
using red::Utf16String;

RED_FILESYSTEM_API void InitializeFileSyncMemoryPools();