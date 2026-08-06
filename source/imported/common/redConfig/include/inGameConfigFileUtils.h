/**
 * Copyright (c) 2019-2020 CD Projekt Red. All Rights Reserved.
 */

#pragma once

#include "../../../external/rapidjson/include/rapidjson/reader.h"
#include "../../../external/rapidjson/include/rapidjson/filewritestream.h"
#include "../../../external/rapidjson/include/rapidjson/document.h"
#include "../../../external/rapidjson/include/rapidjson/prettywriter.h"
#include "../../../external/rapidjson/include/rapidjson/error/en.h"
#include "../../redContainers/include/string/string.h"
#include "../../redSystem/include/types.h"

namespace InGameConfig
{
	class JSONAllocator
	{
	public:

		RED_FORCE_INLINE void* Malloc( size_t size )
		{
			return RED_ALLOCATE( PoolInGameConfigResource, size );
		}

		RED_FORCE_INLINE void* Realloc( void* originalPtr, size_t originalSize, size_t newSize )
		{
			return RED_REALLOCATE( PoolInGameConfigResource, originalPtr, newSize );
		}

		RED_FORCE_INLINE static void Free( void *ptr )
		{
			RED_FREE( PoolInGameConfigResource, ptr );
		}
	};

	class JsonTextWriter
	{
	public:
		typedef AnsiChar Ch;

		JsonTextWriter( red::String& text )
			: m_text( text )
		{
		}

		void Put( const AnsiChar ch )
		{
			m_text += ch;
		}

		void Flush()
		{
		}

		const red::String& GetJsonText()
		{
			return m_text;
		}

	private:
		red::String& m_text;
	};

	using RapidJsonDocument = rapidjson::GenericDocument< rapidjson::UTF8< red::AnsiChar >, rapidjson::MemoryPoolAllocator< JSONAllocator >, JSONAllocator >;
	using RapidJsonValue = rapidjson::GenericValue< rapidjson::UTF8< red::AnsiChar >, rapidjson::MemoryPoolAllocator< JSONAllocator > >;
	using RapidJsonWriter = rapidjson::PrettyWriter< JsonTextWriter, rapidjson::UTF8< red::AnsiChar >, rapidjson::UTF8< red::AnsiChar >, JSONAllocator >;
}
