/*
 * Copyright (c) 2016-2020 CD Projekt Red. All Rights Reserved.
 */

#pragma once

using red::String; // TODO remove (many source files implicitly dependent on this)

namespace red
{
	namespace prv
	{
		// NOTE
		// Can be used for compile time (will be constant evaluated) and runtime (non-recursive).
		// Also safe as it checks if str non-null (which is non-standard behaviour compared to std::string_view).
		constexpr size_t StrLen_Safe( const char* str )
		{
			size_t result{};
			if( str )
			{
				while( *str )
				{
					++result;
					++str;
				}
			}

			return result;
		}

		constexpr Int64 Max( Int64 a, Int64 b )
		{
			return a > b ? a : b;
		}
	}

	constexpr StringView::StringView()
		: m_ptr{ nullptr }
		, m_length{ 0 }
	{}

	constexpr StringView::StringView( std::nullptr_t )
		: m_ptr{ nullptr }
		, m_length{ 0 }
	{}

	constexpr StringView::StringView( const char* str )
		: m_ptr{ str }
		, m_length{ static_cast< Uint32 >( prv::StrLen_Safe( str ) ) }
	{}

	constexpr StringView::StringView( const char* str, const size_t length )
		: m_ptr{ str }
		, m_length{ static_cast< Uint32 >( length ) }
	{}

	template< class TIterator >
	constexpr StringView::StringView( TIterator begin, TIterator end )
		: m_ptr{ begin }
		, m_length{ static_cast< Uint32 >( std::distance( begin, end ) ) }
	{}

	RED_INLINE StringView::StringView( const red::String& strAnsi )
		: m_ptr{ strAnsi.AsChar() }
		, m_length{ strAnsi.Length() }
	{}

	// Clear the view, not the underlying buffer
	RED_INLINE void StringView::Clear()
	{
		m_ptr = nullptr;
		m_length = 0;
	}

	constexpr void StringView::TrimFront( Uint32 count )
	{
		if ( count < m_length )
		{
			m_ptr += count;
			m_length -= count;
		}
		else
		{
			m_ptr += m_length;
			m_length = 0;
		}
	}

	constexpr StringView StringView::TrimFront( Uint32 count ) const
	{
		StringView result{ *this };
		result.TrimFront( count );
		return result;
	}

	constexpr void StringView::TrimBack( Uint32 count )
	{
		const auto delta = count < m_length ? count : m_length;
		m_length -= delta;
	}

	constexpr StringView StringView::TrimBack( Uint32 count ) const
	{
		StringView result{ *this };
		result.TrimBack( count );
		return result;
	}

	constexpr const char* StringView::Data() const
	{
		return m_ptr;
	}

	constexpr Uint32 StringView::Length() const
	{
		return m_length;
	}

	RED_INLINE const char& StringView::At( const Uint32 index ) const
	{
		RED_FATAL_ASSERT( !Empty(), "Cannot index into an empty StringView" );
		RED_FATAL_ASSERT( index < m_length, "Index out of range" );
		return m_ptr[index];
	}

	RED_FORCE_INLINE const char& StringView::operator[]( const Uint32 index ) const
	{
		RED_FATAL_ASSERT( !Empty(), "Cannot index into an empty StringView" );
		RED_FATAL_ASSERT( index < m_length, "Index out of range" );
		return m_ptr[index];
	}

	RED_INLINE const char& StringView::Front() const
	{
		RED_FATAL_ASSERT( !Empty(), "Cannot index into an empty StringView" );
		return m_ptr[0];
	}

	RED_INLINE const char& StringView::Back() const
	{
		RED_FATAL_ASSERT( !Empty(), "Cannot index into an empty StringView" );
		return m_ptr[m_length - 1];
	}

	RED_INLINE String StringView::ToString(const red::memory::Pool &pool) const
	{
		return String( m_ptr, m_length, pool );
	}

	constexpr bool StringView::Empty() const
	{
		return m_length == 0;
	}

	RED_INLINE bool operator==( const StringView& left, const StringView& right )
	{
		return StringView::Equal( left, right );
	}

	RED_INLINE bool operator!=( const StringView& left, const StringView& right )
	{
		return !StringView::Equal( left, right );
	}

	RED_INLINE bool operator<( const StringView& left, const StringView& right )
	{
		return StringView::Compare( left, right ) < 0;
	}

	RED_INLINE bool operator==( const String& left, const StringView& right )
	{
		return StringView::Equal( left, right );
	}

	RED_INLINE bool operator!=( const String& left, const StringView& right )
	{
		return !StringView::Equal( left, right );
	}

	RED_INLINE bool operator<( const String& left, const StringView& right )
	{
		return StringView::Compare( left, right ) < 0;
	}

	RED_INLINE bool operator==( const StringView& left, const String& right )
	{
		return StringView::Equal( left, right );
	}

	RED_INLINE bool operator!=( const StringView& left, const String& right )
	{
		return !StringView::Equal( left, right );
	}

	RED_INLINE bool operator<( const StringView& left, const String& right )
	{
		return StringView::Compare( left, right ) < 0;
	}

	RED_INLINE bool operator==( const char* left, const StringView& right )
	{
		return StringView::Equal( left, right );
	}

	RED_INLINE bool operator!=( const char* left, const StringView& right )
	{
		return !StringView::Equal( left, right );
	}

	RED_INLINE bool operator<( const char* left, const StringView& right )
	{
		return StringView::Compare( left, right ) < 0;
	}

	RED_INLINE bool operator==( const StringView& left, const char* right )
	{
		return StringView::Equal( left, right );
	}

	RED_INLINE bool operator!=( const StringView& left, const char* right )
	{
		return !StringView::Equal( left, right );
	}

	RED_INLINE bool operator<( const StringView& left, const char* right )
	{
		return StringView::Compare( left, right ) < 0;
	}

} // namespace red
