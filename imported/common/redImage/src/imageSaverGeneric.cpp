/**
* Copyright (c) 2017 CDProjekt Red, Inc. All Rights Reserved.
*/

#include "build.h"
#include "imageSaverGeneric.h"

namespace img
{

	static Uint64 c_maxSize{ std::numeric_limits<Uint32>::max() };

	GenericWriter::GenericWriter()
		: IFile( FF_Writer | FF_MemoryBased )
		, m_pos( 0 )
	{}

	void GenericWriter::Reserve(Uint64 size)
	{
		RED_FATAL_ASSERT(size < c_maxSize, "Offset value out of range for storage type");
		m_data.Reserve(static_cast<Uint32>(size));
	}

	Uint64 GenericWriter::GetOffset() const
	{
		return m_pos;
	}

	Uint64 GenericWriter::GetSize() const
	{
		return m_data.Size();
	}

	void GenericWriter::Seek(const Int64 offset)
	{
		RED_FATAL_ASSERT(offset < static_cast< Int64 >( c_maxSize ), "Offset value out of range for storage type");
		RED_FATAL_ASSERT(static_cast<Uint32>(offset) <= m_data.Size(), "Offset out of buffer size");
		m_pos = offset;
	}

	void GenericWriter::Serialize(/*const*/ void* data, const size_t size)
	{
		RED_FATAL_ASSERT(m_pos + size < c_maxSize, "Size too big for used data storage type");
		const Uint32 neededSize = Max<Uint32>(m_data.Size(), static_cast<Uint32>(m_pos + size));
		m_data.Resize(neededSize);
		red::Memcpy(m_data.TypedData() + m_pos, data, size);
		m_pos += size;
	}
} // img
