/**
 * Copyright (c) 2017 CD Projekt Red. All Rights Reserved.
 */

#include "build.h"
#include "../include/deserializer.h"
#include "assert.h"

namespace red
{
namespace memory
{
	Deserializer::Deserializer()
	{}

	Deserializer::~Deserializer()
	{}

	void Deserializer::Initialize( Stream* stream )
	{
		m_streamView = stream;
	}

	void Deserializer::Uninitialize()
	{
		m_streamView.Reset();
	}

	u32 Deserializer::DataAvailableToRead() const
	{
		return m_streamView.DataAvailableToRead();
	}

	void Deserializer::Deserialize( void * buffer, u32 size, u32 & readSize )
	{
		RED_MEMORY_ASSERT( buffer, "Can't serialize a null buffer" );
		m_streamView.ReadBuffer( buffer, size, readSize );
	}

	void Deserializer::Seek( u32 size )
	{
		RED_MEMORY_ASSERT( size <= DataAvailableToRead(), "Can't move read index out of internal buffer" );
		m_streamView.MoveReadIndex( size );
	}
}
}
