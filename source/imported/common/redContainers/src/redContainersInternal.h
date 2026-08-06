/**
* Copyright (c) 2013-16 CD Projekt Red. All Rights Reserved.
*/

#pragma once

// this makes sure this header is NOT included directly from outside core
#ifndef RED_MODULE_redContainers
	#error "Do not include internal headers directly, please use redContainersPublic.h"
#endif

#include "../../redMemory/include/redMemoryPublic.h"
#include "redContainersPublic.h"

// put additional, private headers here
#include "../include/string/string.h"
#include "../include/string/stringLocale.h"
#include "../include/string/stringView.h"
#include "../include/string/tokenizer.h"
