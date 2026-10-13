/**
 * Copyright (c) 2016-2020 CD Projekt Red. All Rights Reserved.
 */

#pragma once

#include "./string.h" // NOTE explicit relative path to avoid name collision with cstdlib

// Pack red::StringView into minimal space (12 bytes)
#pragma pack(push, 4)

namespace red
{
	// Class that represents a read-only view of string data. The data itself may or may not
	// be null terminated, and null terminator may be far later in the data.
	// Prefer passing by value.
	class RED_CONTAINERS_API StringView
	{
	public:
		using const_iterator = const char*;
		using const_reverse_iterator = std::reverse_iterator< const_iterator >;

		static constexpr Uint32 npos{ std::numeric_limits< Uint32 >::max() };

		constexpr StringView();
		constexpr StringView( std::nullptr_t );
		constexpr StringView( const char* str );
		constexpr StringView( const char* str, size_t length );

		template< class TIterator >
		constexpr explicit StringView( TIterator begin, TIterator end );

		/* TODO explicit */ StringView( const red::String& strAnsi );

		constexpr StringView( StringView&& ) = default;
		constexpr StringView( const StringView& ) = default;

		StringView& operator=( StringView&& other ) = default;
		StringView& operator=( const StringView& other ) = default;

		// Clear the view, not the underlying buffer
		void Clear();

		// Trim view by count characters from the front / left
		constexpr void TrimFront( Uint32 count );
		constexpr StringView TrimFront( Uint32 count ) const;
		constexpr void RemovePrefix( Uint32 count ) { TrimFront( count ); }

		// Trim view by count characters from the back / right
		constexpr void TrimBack( Uint32 count );
		constexpr StringView TrimBack( Uint32 count ) const;
		constexpr void RemoveSuffix( Uint32 count ) { TrimBack( count ); }

		// returns raw string data - be careful, the data may not be null terminated
		constexpr const char* Data() const;
		constexpr Uint32 Length() const;
		constexpr Uint32 Size() const { return Length(); }

		// NOTE this max size matches red::String
		constexpr Uint32 MaxSize() const { return /* TODO String::DYNAMIC_MAX_LENGTH_MASK */ 0xc000'0000 ^ std::numeric_limits< Uint32 >::max(); }

		/* TODO constexpr */ const char& At( Uint32 index ) const;
		/* TODO constexpr */ const char& operator[]( Uint32 index ) const;
		/* TODO constexpr */ const char& Front() const;
		/* TODO constexpr */ const char& Back() const;

		red::String ToString( const red::memory::Pool& pool RED_CONTAINER_STRING_DEFAULT_POOL ) const;

		constexpr bool Empty() const;

		friend bool operator==( const StringView& left, const StringView& right );
		friend bool operator!=( const StringView& left, const StringView& right );
		friend bool operator<( const StringView& left, const StringView& right );
		friend bool operator==( const String& left, const StringView& right );
		friend bool operator!=( const String& left, const StringView& right );
		friend bool operator<( const String& left, const StringView& right );
		friend bool operator==( const StringView& left, const String& right );
		friend bool operator!=( const StringView& left, const String& right );
		friend bool operator<( const StringView& left, const String& right );
		friend bool operator==( const char* left, const StringView& right );
		friend bool operator!=( const char* left, const StringView& right );
		friend bool operator<( const char* left, const StringView& right );
		friend bool operator==( const StringView& left, const char* right );
		friend bool operator!=( const StringView& left, const char* right );
		friend bool operator<( const StringView& left, const char* right );

		bool CompareIgnoreCase( StringView stringView ) const;

		bool StartsWith( char chr ) const;
		bool StartsWith( StringView stringView ) const;
		bool EndsWith( char chr ) const;
		bool EndsWith( StringView stringView ) const;
		bool StartsWithIgnoreCase( char chr ) const;
		bool StartsWithIgnoreCase( StringView stringView ) const;
		bool EndsWithIgnoreCase( char chr ) const;
		bool EndsWithIgnoreCase( StringView stringView ) const;

		Uint32 Find( char chr ) const;
		Uint32 Find( StringView stringView ) const;
		Uint32 Find( char chr, Uint32 index ) const;
		Uint32 Find( StringView stringView, Uint32 index ) const;
		Uint32 FindReverse( char chr ) const;
		Uint32 FindReverse( StringView stringView ) const;
		Uint32 FindReverse( char chr, Uint32 index ) const;
		Uint32 FindReverse( StringView stringView, Uint32 index ) const;

		Uint32 FindAnyOf( StringView chars ) const;
		Uint32 FindAnyOfReverse( StringView chars ) const;
		Uint32 FindAnyOf( StringView chars, Uint32 index ) const;
		Uint32 FindAnyOfReverse( StringView chars, Uint32 index ) const;

		// Return a slice of the view starting from 'begin' and ending up at 'end'
		// with 'begin' being inclusive and 'end' exclusive
		StringView Slice( Uint32 begin, Uint32 end ) const;

		// Return subview of length 'length' starting from index 'offset'
		StringView SubView( Uint32 offset, Uint32 length = npos ) const;

		// iterators, range-based for-loop
		RED_INLINE const_iterator begin() const				{ return m_ptr; }
		RED_INLINE const_iterator end() const				{ return m_ptr + m_length; }
		RED_INLINE const_iterator cbegin() const			{ return begin(); }
		RED_INLINE const_iterator cend() const				{ return end(); }
		RED_INLINE const_reverse_iterator rbegin() const	{ return crbegin(); }
		RED_INLINE const_reverse_iterator rend() const		{ return crend(); }
		RED_INLINE const_reverse_iterator crbegin() const	{ return const_reverse_iterator( cend() ); }
		RED_INLINE const_reverse_iterator crend() const		{ return const_reverse_iterator( cbegin() ); }	

	private:
		Uint32 Distance( const_iterator first, const_iterator last ) const;
		Uint32 Distance( const_reverse_iterator first, const_reverse_iterator last ) const;

		static bool Equal( const StringView& left, const StringView& right );
		static Int32 Compare( const StringView& left, const StringView& right );

		const char* m_ptr;
		Uint32		m_length;
	};

	static_assert( sizeof( StringView ) == 12, "Size of StringView is expected to be 12 bytes" );

} // namespace red

#pragma pack(pop)

#include "stringView.hpp"
