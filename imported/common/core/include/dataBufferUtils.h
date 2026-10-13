#pragma once

struct RED_CORE_API DataPartionerHelper
{
	const Uint8* m_begin;
	const Uint32 m_dataSizeInBytes;
	Uint32 m_dataOffsetInBytes;

	DataPartionerHelper( const void* data, Uint32 dataSize );

	template< typename T >
	red::ArraySpan<const T> Span( Uint32 count )
	{
		if ( !count )
			return red::ArraySpan<const T>();

		const Uint32 sizeInBytes = count * sizeof( T );
		const Uint8* begin = m_begin + m_dataOffsetInBytes;
		const Uint8* end = begin + sizeInBytes;

		m_dataOffsetInBytes += sizeInBytes;
		RED_FATAL_ASSERT( m_dataOffsetInBytes <= m_dataSizeInBytes );
		RED_FATAL_ASSERT( end <= m_begin + m_dataSizeInBytes );

		return red::MakeArraySpan( (const T*)begin, (const T*)end );
	}

	void SanityCheck();
};

struct RED_CORE_API DataCopyHelper
{
	red::ArraySpan<Uint8> output;
	Uint32 offset = 0;

	DataCopyHelper( void* data, Uint32 size );
	DataCopyHelper( red::ArraySpan<Uint8> out );

	template<typename T>
	void AppendArray( const red::DynArray<T>& arr )
	{
		Append( arr.Data(), arr.DataSize() );
	}

    template<typename T>
    void AppendSpan( const red::ArraySpan<T>& arr )
    {
        Append( arr.Data(), arr.SizeInBytes() );
    }

	void Append( const void* data, Uint32 dataSize );
	void SanityCheck();
};