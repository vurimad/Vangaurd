#pragma once

#include "../arraySpan.h"

namespace red
{

template < size_t N >
class StringBuffer
{
	RED_STATIC_ASSERT( N > 0 );

public:
	StringBuffer();
	StringBuffer( const char* chr, Uint32 length );
	StringBuffer( const char* chr );
	StringBuffer( const red::String &str );
	StringBuffer( red::StringView str );

	char* AsChar();
	const char* AsChar() const;
	red::StringView AsStringView() const { return{ m_buf, Length() }; } // NOTE calculates length as length not stored here
	red::ArraySpan< char > AsArraySpan() { return red::ArraySpan< char >( m_buf, N ); }

	size_t Length() const;
	size_t Size() const { return Length(); }
	size_t MaxSize() const { return N - 1; }

	char& operator[]( size_t n );
	const char& operator[]( size_t n ) const;

	void Replace( char from, char to );

	void Append( const char* txt );
	void Append( const red::String& txt );
	void Append( red::StringView txt );

	StringBuffer< N >& operator+=( const char* txt );
	StringBuffer< N >& operator+=( const red::String& txt );
	StringBuffer< N >& operator+=( red::StringView txt );

	bool operator==( const char* chr ) const;
	bool operator==( const red::String& s ) const;
	bool operator==( const StringBuffer& s ) const;

private:
	char m_buf[ N ];
};


template < size_t N >
StringBuffer< N >::StringBuffer()
{
	m_buf[0] = 0;
}

template < size_t N >
StringBuffer< N >::StringBuffer( const char* chr, const Uint32 length )
{
	// NOTE strcpy will error on trunctation
	const auto remaining = N - 1 - 0;
	const auto sourceToCopy = math::Min( remaining, static_cast< size_t >( length ) );
	red::Strcpy( m_buf, chr, N, sourceToCopy );
}

template < size_t N >
StringBuffer< N >::StringBuffer( const char* chr )
	: StringBuffer( chr, static_cast< Uint32 >( red::Strlen( chr, N - 1 ) ) )
{}

template < size_t N >
StringBuffer< N >::StringBuffer( const red::String& str )
	: StringBuffer( str.AsChar(), str.Length() )
{}

template < size_t N >
StringBuffer< N >::StringBuffer( const red::StringView str )
	: StringBuffer( str.Data(), str.Length() )
{}

template < size_t N >
char* StringBuffer< N >::AsChar()
{
	return m_buf;
}

template < size_t N >
const char* StringBuffer< N >::AsChar() const
{
	return m_buf;
}

template < size_t N >
size_t StringBuffer< N >::Length() const
{
	return red::Strlen( m_buf, N - 1 );
}

template < size_t N >
char& StringBuffer< N >::operator[]( size_t n )
{
	return m_buf[n];
}

template < size_t N >
const char& StringBuffer< N >::operator[]( size_t n ) const
{
	return m_buf[n];
}

template < size_t N >
void StringBuffer< N >::Replace( char from, char to )
{
	size_t length = Length();

	for ( size_t i = 0; i < length; ++i )
	{
		if ( m_buf[i] == from )
		{
			m_buf[i] = to;
		}
	}
}

template < size_t N >
void StringBuffer< N >::Append( const char* txt )
{
	const auto remaining = N - 1 - Length();
	const auto sourceToCopy = math::Min( remaining, red::Strlen( txt, N - 1 ) );
	red::Strcat( m_buf, txt, N, sourceToCopy );
}

template < size_t N >
void StringBuffer< N >::Append( const red::String& txt )
{
	// NOTE string is null terminated but length freely available
	const auto remaining = N - 1 - Length();
	const auto sourceToCopy = math::Min( remaining, static_cast< size_t >( txt.Length() ) );
	red::Strcat( m_buf, txt.AsChar(), N, sourceToCopy );
}

template < size_t N >
void StringBuffer< N >::Append( const red::StringView txt )
{
	// NOTE string view might not be null terminated
	const auto remaining = N - 1 - Length();
	const auto sourceToCopy = math::Min( remaining, static_cast< size_t >( txt.Length() ) );
	red::Strcat( m_buf, txt.Data(), N, sourceToCopy );
}

template < size_t N >
StringBuffer< N >& StringBuffer< N >::operator+=( const char* txt )
{
	Append( txt );
	return *this;
}

template < size_t N >
StringBuffer< N >& StringBuffer< N >::operator+=( const red::String& txt )
{
	Append( txt );
	return *this;
}

template < size_t N >
StringBuffer< N >& StringBuffer< N >::operator+=( const red::StringView txt )
{
	Append( txt );
	return *this;
}

template < size_t N >
bool StringBuffer< N >::operator==( const char* chr ) const
{
	return red::Strcmp( m_buf, chr, N ) == 0;
}

template < size_t N >
bool StringBuffer< N >::operator==( const red::String& s ) const
{
	return red::Strcmp( m_buf, s.AsChar(), N ) == 0;
}

template < size_t N >
bool StringBuffer< N >::operator==( const StringBuffer& s ) const
{
	return red::Strcmp( m_buf, s.m_buf, N ) == 0;
}

} // red
