/**
* Copyright (c) 2026 RED Vanguard, All Rights Reserved.
*/
#pragma once

#include "../../redMemory/include/uniqueBuffer.h"
#include "../../redContainers/include/blob.h"
#include "rttiTypeName.h"
#include "serializationLoadingToken.h"

class IFile;

namespace serialization
{
	class IBufferAsyncProxy;
	struct AsyncSourceReadBuffer;
	struct MapperLoadBufferParam;
}

namespace res
{
	class ResourceLoaderThrottler;
}

namespace job
{
	class Counter;
	class CompletionDeferral;
}

// Generic buffer for resource data, wraps a UniqueBuffer and adds serialization
// Does not support copying by using the copy constructor or copy assignment operator
// Must use the static Copy method to explicitly copy the memory buffer contained within
class RED_REFLECTION_API DataBuffer
{
public:
	// should bump global file version, but too late now.
	static const Uint32 c_bufferIndexFlag = RED_FLAG(31);
	static const Uint32 c_defaultAlignment = 16;

	DataBuffer(const red::memory::Pool& pool = red::PoolEngine());
	explicit DataBuffer( red::UniqueBuffer&& buffer );
	explicit DataBuffer( Uint32 size, Uint32 alignment = c_defaultAlignment,  const red::memory::Pool& pool = red::PoolEngine() );

	DataBuffer( const DataBuffer& other ) = delete;
	DataBuffer( DataBuffer&& other );

	~DataBuffer();

	DataBuffer& operator=( const DataBuffer& other ) = delete;
	DataBuffer& operator=( DataBuffer&& other );
	DataBuffer& operator=( red::UniqueBuffer&& buffer );

	// Copy functions used to copy data into a buffer, returns a new buffer
	static DataBuffer Copy( const void* data, Uint32 size, const red::memory::Pool& pool = red::PoolEngine() );
	static DataBuffer Copy( const void* data, Uint32 size, Uint32 alignment, const red::memory::Pool& pool = red::PoolEngine() );
	static DataBuffer Copy( const DataBuffer& buffer, const red::memory::Pool& pool = red::PoolEngine() );

	// Simple accessors
	void* Data();
	const void* Data() const;
	Uint32 Size() const;
	Uint32 GetAlignment() const;
	bool Empty() const;

	void Clear();

	void Serialize( IFile& file );

	red::BlobView GetView() const;
    red::BlobSpan GetSpan() const;
	const red::UniqueBuffer & GetInternal() const;
	
	const red::memory::Pool& GetPool();
private:
	static serialization::LoadingToken StaticDispatchLoadingJobs(const serialization::MapperLoadBufferParam& param, serialization::IBufferAsyncProxy& proxy);
	serialization::LoadingToken DispatchLoadingJobsInternal(res::ResourceLoaderThrottler* throttler, io::EAsyncPriority priority, serialization::IBufferAsyncProxy& proxy, const serialization::AsyncSourceReadBuffer* inlineData);
	static void OnLoadedDataCallback(void* target);

	red::UniqueBuffer m_buffer;

	// Note: technically not needed with archives because we read in the object header plus data in one go
	red::UniquePtr<job::CompletionDeferral> m_waitCallbackComplete;
};

RED_REFLECTION_API bool operator == ( const DataBuffer& left, const DataBuffer& right );
RED_REFLECTION_API bool operator != ( const DataBuffer& left, const DataBuffer& right );

//------

// Manual serialization
RED_FORCE_INLINE void operator<<( IFile& file, DataBuffer& val )
{
	val.Serialize( file );
}

//------

RTTI_DECLARE_TYPE_NAME( DataBuffer );

//------