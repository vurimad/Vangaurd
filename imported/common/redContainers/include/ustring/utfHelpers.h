#pragma once

namespace red {

class RED_CONTAINERS_API UtfStringIterator
{
public:
	static_assert( sizeof( red::CodePoint ) == 4, "Code point size must be equal to 4 bytes" );

	enum Encoding
	{
		UTF8,
		UTF32,
	};

	// load from C-style string (expects UTF-8)
	UtfStringIterator( const char* buffer );

	// load from a string view (expects UTF-8)
	RED_FORCE_INLINE UtfStringIterator( const StringView string )
		: m_buffer( reinterpret_cast<const Uint8*>( string.Data() ) )
		, m_bufferSize( string.Length() )
		, m_byteOffset( 0 )
		, m_encoding( UTF8 )
	{
	}

	// load UTF-32 buffer
	RED_FORCE_INLINE UtfStringIterator( const red::CodePoint* buffer, Uint32 length )
		: m_buffer( reinterpret_cast<const Uint8*>( buffer ) )
		, m_bufferSize( length * sizeof( red::CodePoint ) )
		, m_byteOffset( 0 )
		, m_encoding( UTF32 )
	{
		RED_FATAL_ASSERT( buffer, "Invalid string buffer" );
	}

	// load UTF-32 buffer
	RED_FORCE_INLINE UtfStringIterator( const red::ArraySpan< const red::CodePoint > buffer )
		: m_buffer( reinterpret_cast<const Uint8*>( buffer.Data() ) )
		, m_bufferSize( buffer.Size() * sizeof( red::CodePoint ) )
		, m_byteOffset( 0 )
		, m_encoding( UTF32 )
	{
	}

	// load custom
	RED_FORCE_INLINE UtfStringIterator( const Uint8* buffer, Uint32 bufferSize, Encoding encoding )
		: m_buffer( buffer )
		, m_bufferSize( bufferSize )
		, m_byteOffset( 0 )
		, m_encoding( encoding )
	{
		RED_FATAL_ASSERT( buffer, "Invalid string buffer" );
	}

	UtfStringIterator( const UtfStringIterator& ) = default;
	UtfStringIterator& operator = ( const UtfStringIterator & ) = default;
	
	RED_FORCE_INLINE const Uint8* GetBuffer() const { return m_buffer; }
	RED_FORCE_INLINE Encoding GetEncoding() const { return m_encoding; }
	RED_FORCE_INLINE Uint32 GetBufferSize() const { return m_bufferSize; }
	RED_FORCE_INLINE Uint32 GetCurrentOffset() const { return m_byteOffset; }

	RED_FORCE_INLINE bool IsValid() const { return m_byteOffset < m_bufferSize; }

	red::CodePoint ParseNextCharacter();

	Uint32 ComputeLength() const;

private:
	const Uint8* m_buffer;
	Uint32 m_bufferSize;
	Uint32 m_byteOffset;	// number of bytes parsed
	Encoding m_encoding;
};

RED_CONTAINERS_API bool ConvertUtf8ToUtf32( const red::StringView input, red::DynArray< red::CodePoint >& output );
RED_CONTAINERS_API bool ValidateUtf8( const StringView input );

} // red