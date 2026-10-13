/*
* Copyright (c) 2018 CD Projekt Red. All Rights Reserved.
*/

#pragma once

#include "../../../external/rapidjson/include/rapidjson/prettywriter.h"
#include "../../../external/rapidjson/include/rapidjson/reader.h"
#include "../../../external/rapidjson/include/rapidjson/filewritestream.h"
#include "../../../external/rapidjson/include/rapidjson/document.h"

#include "jsonAllocator.h"

namespace rapidjson
{
	using JSONDocument = rapidjson::GenericDocument< rapidjson::UTF8< red::AnsiChar >, rapidjson::MemoryPoolAllocator< res::json::JSONAllocator >, res::json::JSONAllocator >;
	using JSONValue = rapidjson::GenericValue< rapidjson::UTF8< red::AnsiChar >, rapidjson::MemoryPoolAllocator< res::json::JSONAllocator > >;
}
