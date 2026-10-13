/*
 * Copyright (c) 2016-2020 CD Projekt Red. All Rights Reserved.
 */

#include "build.h"
#include "string/stringView.h"

namespace red {

constexpr Uint32 StringView::npos;

Uint32 StringView::Distance( const_iterator first, const_iterator last ) const
{
	return static_cast< Uint32 >( std::distance( first, last ) );
}

Uint32 StringView::Distance( const_reverse_iterator first, const_reverse_iterator last ) const
{
	return m_length - static_cast< Uint32 >( std::distance( first, last ) );
}

// NOTE specialised equal to skip min
bool StringView::Equal( const StringView& left, const StringView& right )
{
	if( left.Length() != right.Length() )
	{
		return false;
	}
	return Strcmp( left.Data(), right.Data(), left.Length() ) == 0;
}

Int32 StringView::Compare( const StringView& left, const StringView& right )
{
	const auto length = Min( left.Length(), right.Length() );
	const auto result = Strcmp( left.Data(), right.Data(), length );
	if( result == 0 )
	{
		// NOTE safe cast to signed as string max size is (1 << 30) - 1 and signed bit is available
		return static_cast< Int32 >( left.Length() ) - static_cast< Int32 >( right.Length() );
	}
	return result;
}

bool StringView::StartsWith( const char chr ) const
{
	return !Empty()
		&& Front() == chr;
}

bool StringView::StartsWith( const StringView stringView ) const
{
	return m_length >= stringView.m_length
		&& Strcmp( m_ptr, stringView.m_ptr, stringView.m_length ) == 0;
}

bool StringView::EndsWith( const char chr ) const
{
	return !Empty()
		&& Back() == chr;
}

bool StringView::EndsWith( const StringView stringView ) const
{
	return m_length >= stringView.m_length
		&& Strcmp( m_ptr + ( m_length - stringView.m_length ), stringView.m_ptr, stringView.m_length ) == 0;
}

bool StringView::StartsWithIgnoreCase( const char chr ) const
{
	return !Empty()
		&& ToLower( Front() ) == ToLower( chr );
}

bool StringView::StartsWithIgnoreCase( const StringView stringView ) const
{
	return m_length >= stringView.m_length
		&& StrcmpNC( m_ptr, stringView.m_ptr, stringView.m_length ) == 0;
}

bool StringView::EndsWithIgnoreCase( const char chr ) const
{
	return !Empty()
		&& ToLower( Back() ) == ToLower( chr );
}

bool StringView::EndsWithIgnoreCase( const StringView stringView ) const
{
	return m_length >= stringView.m_length
		&& StrcmpNC( m_ptr + ( m_length - stringView.m_length ), stringView.m_ptr, stringView.m_length ) == 0;
}

bool StringView::CompareIgnoreCase( const StringView stringView ) const
{
	return m_length == stringView.m_length && StrcmpNC( m_ptr, stringView.m_ptr, m_length ) == 0;
}

Uint32 StringView::Find( const char chr ) const
{
	const const_iterator iter = std::find( cbegin(), cend(), chr );
	return ( iter == cend() ) ? npos : Distance( cbegin(), iter );
}

Uint32 StringView::Find( const StringView stringView ) const
{
	const const_iterator iter = std::search( cbegin(), cend(), stringView.cbegin(), stringView.cend() );
	return ( iter == cend() ) ? npos : Distance( cbegin(), iter );
}

Uint32 StringView::Find( const char chr, const Uint32 index ) const
{
	RED_FATAL_ASSERT( index <= m_length, "Index out of range" );
	const const_iterator iter = std::find( cbegin() + index, cend(), chr );
	return ( iter == cend() ) ? npos : Distance( cbegin(), iter );
}

Uint32 StringView::Find( const StringView stringView, const Uint32 index ) const
{
	RED_FATAL_ASSERT( index <= m_length, "Index out of range" );
	const const_iterator iter = std::search( cbegin() + index, cend(), stringView.cbegin(), stringView.cend() );
	return ( iter == cend() ) ? npos : Distance( cbegin(), iter );
}

Uint32 StringView::FindReverse( const char chr ) const
{
	const const_reverse_iterator iter = std::find( crbegin(), crend(), chr );
	return ( iter == crend() ) ? npos : Distance( crbegin(), iter ) - 1; // 1 is the length of the string of a single character
}

Uint32 StringView::FindReverse( const StringView stringView ) const
{
	const const_reverse_iterator iter = std::search( crbegin(), crend(), stringView.crbegin(), stringView.crend() );
	return ( iter == crend() ) ? npos : Distance( crbegin(), iter ) - stringView.Length();
}

Uint32 StringView::FindReverse( const char chr, const Uint32 index ) const
{
	RED_FATAL_ASSERT( index <= m_length, "Index out of range" );
	const const_reverse_iterator iter = std::find( crbegin() + ( m_length - index ), crend(), chr );
	return ( iter == crend() ) ? npos : Distance( crbegin(), iter ) - 1;
}

Uint32 StringView::FindReverse( const StringView stringView, const Uint32 index ) const
{
	RED_FATAL_ASSERT( index <= m_length, "Index out of range" );
	const const_reverse_iterator iter = std::search( crbegin() + ( m_length - index ), crend(), stringView.crbegin(), stringView.crend() );
	return ( iter == crend() ) ? npos : Distance( crbegin(), iter ) - stringView.Length();
}

Uint32 StringView::FindAnyOf( const StringView chars ) const
{
	auto finder = [chars]( const char c ) -> bool { return std::find( chars.cbegin(), chars.cend(), c ) != chars.cend(); };
	const const_iterator iter = std::find_if( cbegin(), cend(), finder );
	return ( iter == cend() ) ? npos : Distance( cbegin(), iter );
}

Uint32 StringView::FindAnyOfReverse( const StringView chars ) const
{
	const auto finder = [chars]( const char c ) -> bool { return std::find( chars.cbegin(), chars.cend(), c ) != chars.cend(); };
	const const_reverse_iterator iter = std::find_if( crbegin(), crend(), finder );
	return ( iter == crend() ) ? npos : Distance( crbegin(), iter ) - 1;
}

Uint32 StringView::FindAnyOf( const StringView chars, Uint32 index ) const
{
	RED_FATAL_ASSERT( index <= m_length, "Index out of range" );
	const auto finder = [chars]( const char c ) -> bool { return std::find( chars.cbegin(), chars.cend(), c ) != chars.cend(); };
	const_iterator iter = std::find_if( cbegin() + index, cend(), finder );
	return ( iter == cend() ) ? npos : Distance( cbegin(), iter );
}

Uint32 StringView::FindAnyOfReverse( const StringView chars, Uint32 index ) const
{
	RED_FATAL_ASSERT( index <= m_length, "Index out of range" );
	const auto finder = [chars]( const char c ) -> bool { return std::find( chars.cbegin(), chars.cend(), c ) != chars.cend(); };
	const const_reverse_iterator iter = std::find_if( crbegin() + ( m_length - index ), crend(), finder );
	return ( iter == crend() ) ? npos : Distance( crbegin(), iter ) - 1;
}

StringView StringView::Slice( Uint32 begin, Uint32 end ) const
{
	RED_FATAL_ASSERT( begin <= m_length, "begin index is out of range" );
	RED_FATAL_ASSERT( end <= m_length, "end index is out of range" );
	RED_FATAL_ASSERT( begin <= end, "begin and end index in wrong order" );
	return{ m_ptr + begin, ( end - begin ) };
}

StringView StringView::SubView( Uint32 offset, Uint32 length ) const
{
	RED_FATAL_ASSERT( offset <= m_length, "offset is out of range" );
	if( length == npos || offset + length > m_length )
	{
		length = m_length - offset;
	}
	return{ m_ptr + offset, length };
}

} // namespace red
