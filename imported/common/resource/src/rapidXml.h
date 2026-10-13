/**
 * Copyright (c) 2016 CD Projekt Red. All Rights Reserved.
 */

#pragma once

#define RAPIDXML_NO_EXCEPTIONS
#define RAPIDXML_STATIC_POOL_SIZE	32 * 1024
#include "rapidxml/rapidxml.hpp"

namespace rapidxml
{
	Bool HasError();
	void ResetErrors();

}	// namespace rapidxml
