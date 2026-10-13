//////////////////////////////////////////////////////////////////////////
//
// headers
#include "build.h"
#include "stringSerialization.h"
#include "../../redMemory/include/uniqueBuffer.h"
#include "../../redContainers/include/arraySpan.h"
#include "../../redContainers/include/ustring/utf16String.h"

namespace
{
	// Simple buffer with a small data optimisation
	class SmallDataBuffer
	{
	public:
		SmallDataBuffer()
			: m_smallBuffer{}
			, m_buffer( m_smallBuffer )
			, m_size( 0 )
		{
		}

		explicit SmallDataBuffer( Uint32 size )
			: m_smallBuffer{}
			, m_buffer( m_smallBuffer )
			, m_size( 0 )
		{
			Resize( size );
		}

		~SmallDataBuffer() = default;

		void Resize( Uint32 size )
		{
			if ( size > SMALL_BUFFER_SIZE )
			{
				m_largeBuffer = red::CreateUniqueBuffer< red::PoolEngine >( size, sizeof( UniChar ) );
				m_buffer = m_largeBuffer.Get();
			}
			m_size = size;
		}

		Uint32 Size() const
		{
			return m_size;
		}

		void* Data() const
		{
			return m_buffer;
		}

		template < typename T >
		T* DataAs() const
		{
			return static_cast< T* >( m_buffer );
		}

	private:
		enum { SMALL_BUFFER_SIZE = 256 };

		Uint8 m_smallBuffer[ SMALL_BUFFER_SIZE ];
		red::UniqueBuffer m_largeBuffer;

		void* m_buffer;
		Uint32 m_size;
	};


	Int32 ReadStringData( IFile& file, SmallDataBuffer& buffer )
	{
		Int32 length = ReadVarint_LEB128_Signed( file );

		Uint32 readByteSize = 0;
		if ( length > 0 )
		{
			readByteSize = length * sizeof( UniChar );
		}
		else if ( length < 0 )
		{
			readByteSize = -length * sizeof( AnsiChar );
		}

		const Uint32 bufferByteSize = readByteSize + sizeof( UniChar );
		buffer.Resize( bufferByteSize );

		if ( readByteSize > 0 )
		{
			file.Serialize( buffer.Data(), readByteSize );
		}

		// Fill the end of the buffer with zeroes to terminate the string
		const auto byteBuffer = buffer.DataAs< Uint8 >() + readByteSize;
		byteBuffer[0] = 0;
		byteBuffer[1] = 0;

		return length;
	}

	bool CanSaveAscii( const Utf16String& str )
	{
		for ( Uint32 i = 0; i < str.Length(); ++i )
		{
			if ( str[i] > 127 )
			{
				return false;
			}
		}
		return true;
	}

	void WriteAsciiStringData( IFile& file, const red::ArraySpan< AnsiChar > data )
	{
		Int32 encodedLength = -static_cast<Int32>( data.Count() );
		WriteVarint_LEB128_Signed( file, encodedLength );
		if ( !data.Empty() )
		{
			file.Serialize( data.Data(), data.SizeInBytes() );
		}
	}

	void WriteUnicodeStringData( IFile& file, const red::ArraySpan< UniChar > data )
	{
		WriteVarint_LEB128_Signed( file, data.Count() );
		if ( !data.Empty() )
		{
			file.Serialize( data.Data(), data.SizeInBytes() );
		}
	}
}


//////////////////////////////////////////////////////////////////////////
//
// storing uni string
void operator<<( IFile& file, red::Utf16String& str )
{
	// Serialize string
	if ( file.IsReader() )
	{
		SmallDataBuffer buffer;
		const Int32 length = ReadStringData( file, buffer );
		if ( length > 0 )
		{
			// todo: add Set methods to Utf16String
			str = red::Utf16String( buffer.DataAs< UniChar >() );
		}
		else if ( length < 0 )
		{
			str = red::Utf16String( ANSI_TO_UNICODE( buffer.DataAs< AnsiChar >() ) );
		}
		else // if ( size == 0 )
		{
			str.Clear();
		}
	}
	else if ( file.IsWriter() )
	{
		if ( str.Empty() || !CanSaveAscii( str ) )
		{
			WriteUnicodeStringData( file, red::ArraySpan< UniChar >( reinterpret_cast<UniChar*>( str.GetPtr() ), str.Length() ) );
		}
		else
		{
			CUnicodeToAnsi converter( str.AsChar() );
			WriteAsciiStringData( file, red::ArraySpan< AnsiChar >( converter, static_cast< Uint32 >( red::Strlen( converter ) ) ) );
		}
	}
}

//////////////////////////////////////////////////////////////////////////
//
// storing ascii string
void operator<<( IFile& file, red::String& str )
{
	// Serialize string
	if ( file.IsReader() )
	{
		SmallDataBuffer buffer;
		const Int32 length = ReadStringData( file, buffer );
		if ( length > 0 )
		{
			str.Set( UNICODE_TO_ANSI( buffer.DataAs< UniChar >() ) );
		}
		else if ( length < 0 )
		{
			str.Set( buffer.DataAs< AnsiChar >(), -length );
		}
		else // if ( size == 0 )
		{
			str.Clear();
		}
	}
	else if ( file.IsWriter() )
	{
		WriteAsciiStringData( file, red::ArraySpan< AnsiChar >( str.AsChar(), str.Length() ) );
	}
}
