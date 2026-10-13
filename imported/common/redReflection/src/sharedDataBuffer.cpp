/**
* Copyright (c) 2026 RED Vanguard, All Rights Reserved.
*/
#include "build.h"
#include "sharedDataBuffer.h"
#include "sharedDataBufferImpl.h"

#include "rttiRegistration.h"
#include "rttiType.h"
#include "textReader.h"
#include "textWriter.h"

#include "../../redFileSystem/include/file.h"

//-------------------------------------

SharedDataBuffer::SharedDataBuffer( const SharedDataBuffer& other )
{
	m_data = GetSharedDataBufferCache().Copy( other.m_data );
}

SharedDataBuffer::SharedDataBuffer( const void* data, const Uint32 size )
{
	m_data = GetSharedDataBufferCache().Request( data, size );
}

SharedDataBuffer::~SharedDataBuffer()
{
	if ( m_data != nullptr )
	{
		GetSharedDataBufferCache().Release( m_data );
		m_data  = nullptr;
	}
}

SharedDataBuffer& SharedDataBuffer::operator=( const SharedDataBuffer& other )
{
	if ( m_data != other.m_data )
	{
		if ( m_data )
		{
			GetSharedDataBufferCache().Release( m_data );
		}
		m_data = GetSharedDataBufferCache().Copy( other.m_data );
	}

	return *this;
}

SharedDataBuffer& SharedDataBuffer::operator=( SharedDataBuffer&& other )
{
	auto oldData = m_data;

	m_data = other.m_data;
	other.m_data = nullptr;

	if ( oldData )
	{
		GetSharedDataBufferCache().Release( oldData );
	}

	return *this;
}

const Uint32 SharedDataBuffer::GetSize() const
{
	return m_data ? m_data->GetSize() : 0;
}

const void* SharedDataBuffer::GetData() const
{
	return m_data ? m_data->GetData() : 0;
}

const Uint64 SharedDataBuffer::GetHash() const
{
	return m_data ? m_data->GetHash() : 0;
}

void SharedDataBuffer::Clear()
{
	if ( m_data )
	{
		GetSharedDataBufferCache().Release( m_data );
		m_data = nullptr;
	}
}

void SharedDataBuffer::SetData( const void* data, const Uint32 size )
{
	auto newData = GetSharedDataBufferCache().Request( data, size );

	if ( m_data )
	{
		GetSharedDataBufferCache().Release( m_data );
	}

	m_data = newData;
}

void SharedDataBuffer::Serialize( class IFile& file )
{
	// nothing to do for garbage collector
	if ( file.IsMapper() )
		return;

	// NOTE: This MUST be compatible with DataBuffer serialization

	// Loading
	if ( file.IsReader() )
	{
		// Load the data size
		Uint32 dataSize = 0;
		file << dataSize;

		// Load buffer content
		if ( dataSize > 0 )
		{
			// Load the data into temporary memory
			void* tempMem = nullptr;
			const Bool tempMemDynamic = (dataSize > 65535); // temporary buffer needs to be big
			if ( tempMemDynamic )
			{
				tempMem = RED_ALLOCATE( red::PoolEngine, dataSize );
			}
			else
			{
				tempMem = RED_ALLOCA( dataSize );
			}

			// Load the data
			file.Serialize( tempMem, dataSize );

			// Assign the data
			SetData( tempMem, dataSize );

			// Free temporary buffer if it was allocated from Heap
			if ( tempMemDynamic )
			{
				RED_FREE( red::PoolEngine, tempMem );
				tempMem = nullptr;
			}
		}
		else
		{
			// No data
			Clear();
		}
	}
	else
	{
		// Save data size
		Uint32 dataSize = GetSize();
		file << dataSize;

		// Save data
		if ( dataSize > 0 )
		{
			const void* data = GetData();
			file.Serialize( (void*) data, dataSize );
		}
	}
}

void SharedDataBuffer::SerializeToText( class text::ITextWriter& writer ) const
{
	writer.WriteValue( GetData(), GetSize() );
}

void SharedDataBuffer::SerializeFromText( class text::ITextReader& reader )
{
	// Load the data size
	DataBuffer data;
	if ( reader.ReadValue( data ) )
		SetData( data.Data(), data.Size() );
}

///-----

class SharedDataBufferType : public rtti::IType
{
public:
	virtual const CName GetName() const override final
	{
		return GetTypeName< SharedDataBuffer >();
	}

	virtual Uint32 GetSize() const override final
	{
		return sizeof(SharedDataBuffer);
	}

	virtual Uint32 GetAlignment() const override final
	{
		return 4;
	}

	virtual ERTTITypeType GetType() const override final
	{
		return RT_Simple;
	}

	virtual void Construct( void *object ) const override final
	{
		new ( object ) SharedDataBuffer();
	}

	virtual void Destruct( void *object ) const override final
	{
		((SharedDataBuffer*)object)->~SharedDataBuffer();
	}

	virtual Bool Compare( const void* data1, const void* data2, Uint32 DEPRECATED_flags ) const override final
	{
		const auto& a = *(const SharedDataBuffer*)data1;
		const auto& b = *(const SharedDataBuffer*)data2;
		return a.GetHash() == b.GetHash();
	}

	virtual void Copy( void* dest, const void* src ) const override final
	{
		(*(SharedDataBuffer*)dest) = (*(const SharedDataBuffer*) src);
	}

	virtual Bool Serialize( IFile& file, void* data, ISerializable* owner = nullptr ) const override final
	{
		((SharedDataBuffer*)data)->Serialize( file );
		return true;
	}

	virtual Bool NeedsCleaning() const override final
	{
		return true;
	}

	virtual const Bool SerializeToText( text::ITextWriter& writer, const void* data ) const override final
	{
		const SharedDataBuffer* dataBuffer = static_cast<const SharedDataBuffer*>( data );
		dataBuffer->SerializeToText( writer );
		return true;
	}

	virtual const Bool SerializeFromText( text::ITextReader& reader, void* data ) const override final
	{
		SharedDataBuffer* dataBuffer = static_cast<SharedDataBuffer*>( data );
		dataBuffer->SerializeFromText( reader );
		return true;
	}
};

void RegisterTypeSharedDataBuffer()
{
	RTTI_REGISTER_AUTO_TYPE_ALIAS( SharedDataBuffer, SharedDataBufferType );
}

///-----
