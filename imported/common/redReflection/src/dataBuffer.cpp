/**
* Copyright (c) 2026 RED Vanguard, All Rights Reserved.
*/
#include "build.h"
#include "dataBuffer.h"
#include "rttiRegistration.h"
#include "rttiSystem.h"
#include "rttiType.h"
#include "rttiUtils.h"
#include "textReader.h"
#include "textWriter.h"
#include "../../redFileSystem/include/file.h"
#include "serializationMapping.h"
#include "bufferAsyncProxy.h"

DataBuffer::DataBuffer(const red::memory::Pool& pool)
	: m_buffer( red::MakeEmptyUniqueBuffer(pool, c_defaultAlignment) )
{
}

DataBuffer::DataBuffer( red::UniqueBuffer&& buffer )
	: m_buffer( std::move( buffer ) )
{
}

DataBuffer::DataBuffer( Uint32 size, Uint32 alignment, const red::memory::Pool& pool )
	: m_buffer( size !=0 ? red::CreateUniqueBuffer( pool, size, alignment ) : red::MakeEmptyUniqueBuffer( pool, alignment ) )
{
}

DataBuffer::DataBuffer( DataBuffer&& other )
	: m_buffer( std::move( other.m_buffer ) )
{
}

DataBuffer::~DataBuffer()
{
}

DataBuffer& DataBuffer::operator=( DataBuffer&& other )
{
	if ( this != &other )
	{
		m_buffer = std::move( other.m_buffer );
	}
	return *this;
}

DataBuffer& DataBuffer::operator=( red::UniqueBuffer&& buffer )
{
	m_buffer = std::move( buffer );
	return *this;
}

DataBuffer DataBuffer::Copy( const void* data, Uint32 size, const red::memory::Pool& pool )
{
	return DataBuffer::Copy(data, size, c_defaultAlignment, pool);
}

DataBuffer DataBuffer::Copy( const void* data, Uint32 size, Uint32 alignment, const red::memory::Pool& pool )
{
	DataBuffer result( size, alignment, pool );
	red::Memcpy( result.Data(), data, size );
	return result;
}

DataBuffer DataBuffer::Copy( const DataBuffer& buffer, const red::memory::Pool& pool )
{
	return DataBuffer::Copy( buffer.Data(), buffer.Size(), pool );
}

void DataBuffer::Clear()
{
	m_buffer.Reset();
}

void* DataBuffer::Data()
{
	return m_buffer.Get();
}

const void* DataBuffer::Data() const
{
	return m_buffer.Get();
}

Uint32 DataBuffer::Size() const
{
	return m_buffer.GetSize();
}

bool DataBuffer::Empty() const
{
	return !m_buffer || m_buffer.GetSize() == 0;;
}

Uint32 DataBuffer::GetAlignment() const
{
	return m_buffer.GetAlignment();
}

void DataBuffer::Serialize( IFile& file )
{
	// No job done for the non-file stuff
	if ( file.IsMapper() )
	{
		return;
	}

	// Loading
	if ( file.IsReader() )
	{
		// Load the data size
		Uint32 dataSize = 0;
		file << dataSize;

		if ((dataSize & c_bufferIndexFlag) != 0)
		{
			RED_FATAL_ASSERT(file.HasMapper());

			const auto index = static_cast<serialization::IMapper::BufferIndex>(dataSize & ~c_bufferIndexFlag);
			dataSize = ~0U; // make unusable

			// Prepare the data buffer
			const red::memory::Pool& pool = GetPool().GetHandle() != red::PoolEngine::GetHandle()
				? GetPool() :
				file.GetInternalMemoryPool();

			m_buffer = red::MakeEmptyUniqueBuffer(pool, m_buffer.GetAlignment());

			// Sanity check to just make sure
			RED_FATAL_ASSERT(m_buffer.GetPool().GetHandle() != red::PoolFrame::GetHandle(), "Frame pool for latent I/O forbidden");
			RED_FATAL_ASSERT(m_buffer.GetPool().GetHandle() != red::PoolDoubleBufferedFrame::GetHandle(), "Frame pool for latent I/O forbidden");
			RED_FATAL_ASSERT(m_buffer.GetPool().GetHandle() != job::PoolJobScope::GetHandle(), "Job pool for latent I/O forbidden");

			serialization::IMapper::LoadBufferToken token;
			token.isAutoload = true;
			token.callback = &DataBuffer::StaticDispatchLoadingJobs;
			token.userData = this;

			serialization::BufferAsyncPtr bufferAccess; // don't need to keep after UnmapBuffer() call
			file.GetMapper()->UnmapBuffer(index, token, bufferAccess);

			RED_FATAL_ASSERT(!bufferAccess || !bufferAccess->IsMemoryFileBuffer(), "Unexpected. Not supported yet. Make sure loaded and actually uses expected vs default alignment");
		}
		else
		{
			// Create the data buffer
			const red::memory::Pool& pool = GetPool().GetHandle() != red::PoolEngine::GetHandle() 
				? GetPool() : 
				file.GetInternalMemoryPool();

			m_buffer = red::CreateUniqueBuffer( pool, dataSize, m_buffer.GetAlignment() );

			// Load the data from file
			file.Serialize( m_buffer.Get(), dataSize );
		}
	}
	else
	{
		if (file.IsCooker())
		{
			RED_FATAL_ASSERT(file.HasMapper());
			{
				serialization::IMapper::BufferIndex tmpIndex = 0;
			
				serialization::IMapper::MapBufferFlags flags;
				flags.useCompression = true;
				flags.useFastCompression = false;
				flags.allowNonDefaultCompressionType = file.IsCooker();
				flags.hintGPUMemory = false;
				flags.hintAutoLoadPC = true;
				flags.hintAutoLoadXboxOne = true;
				flags.hintAutoLoadPS4 = true;

				// MANAGE RISK: should refactor MapBuffer() to take a red::UniqueBuffer directly, but then need to change the hashing mapper and make sure other corner cases of nullptr vs size > 0 are correct
				// E.g., allocator could return non-nullptr but size = 0.
				// And could MOVE the buffer into a BufferHandleRO then move back again, but modifying the data while saving is a recipe for crashes if something else reads it at the same time.
				auto bufferCopy = red::CreateUniqueBuffer(m_buffer.GetPool(), m_buffer.Size(), m_buffer.GetAlignment());
				red::Memcpy(bufferCopy.Data(), m_buffer.Data(), m_buffer.Size());			

				auto saveBufferRO = serialization::BufferHandleRO(std::move(bufferCopy));
				serialization::BufferHandleRO precompressedBuffer; // right now there is no precompressed buffer

				file.GetMapper()->MapBuffer({ saveBufferRO, precompressedBuffer, flags }, tmpIndex);

				Uint32 index = ((Uint32)tmpIndex) | c_bufferIndexFlag ; // Encode so we know it isn't a buffer size
				file << index;
			}
		}
		else
		{
			// Save data size
			Uint32 dataSize = m_buffer.GetSize();
			file << dataSize;

			// Save data
			file.Serialize( m_buffer.Get(), dataSize );
		}
	}
}

const red::UniqueBuffer & DataBuffer::GetInternal() const
{
	return m_buffer;
}

const red::memory::Pool& DataBuffer::GetPool()
{
	return m_buffer.GetPool( );
}

serialization::LoadingToken DataBuffer::StaticDispatchLoadingJobs(const serialization::MapperLoadBufferParam& param, serialization::IBufferAsyncProxy& proxy)
{
	auto self = static_cast<DataBuffer*>(param.userData);
	RED_FATAL_ASSERT(self);

	return self->DispatchLoadingJobsInternal(param.throttler, param.priority, proxy, param.inlineData);
}

serialization::LoadingToken DataBuffer::DispatchLoadingJobsInternal(res::ResourceLoaderThrottler* throttler, io::EAsyncPriority priority, serialization::IBufferAsyncProxy& proxy, const serialization::AsyncSourceReadBuffer* inlineData)
{
	serialization::IBufferAsyncProxy::BufferLoadingContext bufferContext;
	bufferContext.target = this;
	bufferContext.callback = &DataBuffer::OnLoadedDataCallback;
	bufferContext.reentrantCallback = &DataBuffer::OnLoadedDataCallback;
	bufferContext.destinationBuffer = &m_buffer;
	bufferContext.alignment = GetAlignment();
	bufferContext.priority = priority;
	bufferContext.throttler = throttler;
	bufferContext.inlineData = inlineData;

	job::Counter loadingCounter;
	m_waitCallbackComplete = red::CreateUniquePtr<job::CompletionDeferral>( loadingCounter.CreateDeferral(this, "DataBuffer") );

	proxy.DispatchLoadingJobs(bufferContext);

	return serialization::LoadingToken(std::move(loadingCounter));
}

void DataBuffer::OnLoadedDataCallback(void* target)
{
	auto* self = static_cast<DataBuffer*>(target);
	self->m_waitCallbackComplete = nullptr;
}

red::BlobView DataBuffer::GetView() const
{
	return red::BlobView( m_buffer.Get(), m_buffer.GetSize() );
}

red::BlobSpan DataBuffer::GetSpan() const
{
	return red::BlobSpan( m_buffer.Get(), m_buffer.GetSize() );
}

bool operator == ( const DataBuffer& left, const DataBuffer& right )
{
	if ( left.Size() != right.Size() )
		return false;

	if ( left.Empty() && right.Empty() )
		return true;

	if ( left.Data() && right.Data() )
		return (0 == red::Memcmp( left.Data(), right.Data(), left.Size() ) );

	return false;
}

bool operator != ( const DataBuffer& left, const DataBuffer& right )
{
	return !(left == right);
}


//----

class DataBufferType : public rtti::IType
{
public:
	virtual const CName GetName() const override final
	{
		return GetTypeName< DataBuffer >();
	}

	virtual Uint32 GetSize() const override final
	{
		return sizeof(DataBuffer);
	}

	virtual Uint32 GetAlignment() const override final
	{
		return 4;
	}

	virtual ERTTITypeType GetType() const override final
	{
		return RT_Simple;
	}

	virtual CName GetRefName() const override final
	{ 
		return rtti::FormatScriptedReferenceTypeName( GetTypeName< DataBuffer >() );
	}

	virtual void Construct( void *object ) const override final
	{
		new ( object ) DataBuffer();
	}

	virtual void Destruct( void *object ) const override final
	{
		((DataBuffer*)object)->~DataBuffer();
	}

	virtual Bool Compare( const void* data1, const void* data2, Uint32 DEPRECATED_flags ) const override final
	{
		RED_FATAL_ASSERT( data1 != nullptr && data2 != nullptr );
		const DataBuffer& left = *static_cast<const DataBuffer*>( data1 );
		const DataBuffer& right = *static_cast<const DataBuffer*>( data2 );
		return left == right;
	}

	virtual void Copy( void* dest, const void* src ) const override final
	{
		// Don't want this copying be default
		//(*(DataBuffer*)dest) = DataBuffer::Copy( (*(const DataBuffer*) src) );
	}

	virtual Bool Serialize( IFile& file, void* data, ISerializable* owner = nullptr ) const override final
	{
		((DataBuffer*)data)->Serialize( file );
		return true;
	}

	virtual Bool NeedsCleaning() const override final
	{
		return true;
	}

	virtual const Bool SerializeToText( text::ITextWriter& writer, const void* data ) const override final
	{
		return false;
	}

	virtual const Bool SerializeFromText( text::ITextReader& reader, void* data ) const override final
	{
		return false;
	}

	virtual const red::memory::Pool & GetInnerTypeMemoryPool() const override final
	{ 
		return red::PoolEngine::GetInstance();
	}
};

void RegisterTypeDataBuffer()
{
	RTTI_REGISTER_AUTO_TYPE_ALIAS( DataBuffer, DataBufferType );
}
