/**
 * Copyright (c) 2016 CD Projekt Red. All Rights Reserved.
 */

#include "build.h"
#include "assert.h"
#include "../include/serializer.h"

namespace red
{
namespace memory
{
	Serializer::Serializer()
	{}

	Serializer::~Serializer()
	{}

	void Serializer::Initialize( Stream* stream )
	{
		m_streamView = stream;
	}

	void Serializer::Uninitialize()
	{
		m_streamView.Reset();
	}

	u32 Serializer::DataAvailableToWrite() const
	{
		return m_streamView.DataAvailableToWrite();
	}

	Bool Serializer::Serialize( const void* buffer, u32 size )
	{
		RED_MEMORY_ASSERT( buffer, "Can't serialize a null buffer" );
		return m_streamView.WriteBuffer( buffer, size );
	}
}
}
