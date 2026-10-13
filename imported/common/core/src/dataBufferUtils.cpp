#include "build.h"
#include "dataBufferUtils.h"

DataPartionerHelper::DataPartionerHelper( const void* data, Uint32 dataSize ) 
	: m_begin( (const Uint8*)data ), m_dataSizeInBytes( dataSize )
	, m_dataOffsetInBytes( 0 )
{}

void DataPartionerHelper::SanityCheck()
{
	RED_FATAL_ASSERT( (MemSize)(m_begin + m_dataOffsetInBytes) == (MemSize)(m_begin + m_dataSizeInBytes) );
}

DataCopyHelper::DataCopyHelper( red::ArraySpan<Uint8> out ) 
	: output( out )
{}


DataCopyHelper::DataCopyHelper( void* data, Uint32 size )
{
	output = red::MakeArraySpan( (Uint8*)data, size );
}

void DataCopyHelper::Append( const void* data, Uint32 dataSize )
{
	if ( dataSize == 0 )
		return;

	RED_FATAL_ASSERT( offset + dataSize <= output.Size() );
	red::Memcpy( output.Data() + offset, data, dataSize );
	offset += dataSize;
}

void DataCopyHelper::SanityCheck()
{
	RED_FATAL_ASSERT( offset == output.Size() );
}
