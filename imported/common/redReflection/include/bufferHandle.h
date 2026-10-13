/*
* Copyright (c) 2017 CD Projekt Red. All Rights Reserved.
*/

#pragma once
#include "redReflectionApi.h"
#include "../../../common/redContainers/include/blob.h"

namespace red
{
	class UniqueBuffer;
}

namespace serialization
{
	class RED_REFLECTION_API BufferHandleRO
	{
	public:
		static const Uint32 c_defaultAlignment = 16;

		BufferHandleRO();

		BufferHandleRO( std::nullptr_t );

		BufferHandleRO( const void* data, Uint32 size, Uint32 alignment = c_defaultAlignment, const red::memory::Pool& memoryPool = red::PoolEngine() );

		explicit BufferHandleRO( red::UniqueBuffer&& data );

		explicit BufferHandleRO( Uint32 size, Uint32 alignment = c_defaultAlignment, const red::memory::Pool& memoryPool = red::PoolEngine() );

		const void* Data() const;
		Uint32 Size() const;
		Uint32 GetSize() const;
		Uint32 GetAlignment() const;
        red::BlobView GetView() const;
        red::BlobSpan GetSpan() const;

	private:
		red::SharedPtr< red::UniqueBuffer > m_data;
	};
}