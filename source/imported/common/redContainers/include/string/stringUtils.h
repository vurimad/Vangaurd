/**
 * Copyright (c) 2016 CD Projekt Red. All Rights Reserved.
 */

#pragma once

#include "stringView.h"

namespace red
{
	// Returns true if the string passed in contains any glob matching characters * or ?
	RED_CONTAINERS_API bool IsGlobString( const StringView string );

	// Matches the string against the Glob pattern
	// A Glob pattern is one that is used to match files and names, where:
	// - A * character matches "any number of characters, or none", equivalent to ".*" in a regex
	// - A ? character matches "any single character", equivalent to "." in a regex
	RED_CONTAINERS_API bool GlobMatch( const StringView string, const StringView pattern );

	// Get offset (position) of the suffix number in a string, i.e. for "Alice123" we will get 5 (offset of "123").
	// This is used by GetCoreNameString and by current version (2017.10) CName and NameRegistry.
	// Note, that we skip leading zeros in the number suffix, like "Alice00123" will return 7, the "00" is treated as string prefix.
	// This is so we do not strip leading zeros, since they might be useful.
	RED_CONTAINERS_API Uint32 FindDigitOffset( const red::StringView & view );

	// Extract core name from the string. This should work exactly as CName::GetCoreNameString().
	// Core name means all characters before numerical suffix of the string, or to be exact before the first non zero digit of the numerical suffix.
	// Examples:
	//		GetCoreNameString( "Alice" ) == "Alice"
	//		GetCoreNameString( "Alice123" ) == "Alice"
	//		GetCoreNameString( "Alice002" ) == "Alice00"
	RED_CONTAINERS_API StringView GetCoreNameString( const StringView str );

	// Split the input string into parts, based on supplied separator.
	// Separators are not output as parts. Consecutive separators are treated as one separator (we do not output empty parts).
	RED_CONTAINERS_API red::DynArray<red::StringView> StrSplit( const red::StringView str, char separator );
	RED_CONTAINERS_API red::DynArray<red::StringView> StrSplit( const red::StringView str, const red::StringView separator );
	RED_CONTAINERS_API red::DynArray<red::StringView> StrSplit( const red::StringView str, char separator, const red::memory::Pool& pool );
	RED_CONTAINERS_API red::DynArray<red::StringView> StrSplit( const red::StringView str, const red::StringView separator, const red::memory::Pool& pool );

	// Merge the input string parts into a single string with the given separator
	RED_CONTAINERS_API red::String StrJoin( red::ArraySpan< red::StringView > stringParts, red::StringView seprator );
	RED_CONTAINERS_API red::String StrJoin( red::ArraySpan< const red::String > stringParts, red::StringView seprator );

	// Test if a string is a decimal (base 10) number. The number can be positive or (optionally) negative.
	//		IsIntegerBase10( "123" ) == true
	//		IsIntegerBase10( "Alice" ) == false
	//		IsIntegerBase10( "3.14" ) == false
	RED_CONTAINERS_API Bool IsIntegerBase10( const red::StringView str, Bool allowNegative = true );

	// Trim any whitespace at the start and end of a stringview
	RED_CONTAINERS_API StringView TrimLeft( const StringView& view );

	// Trim any whitespace at the start and end of a stringview
	RED_CONTAINERS_API StringView TrimRight( const StringView& view );

	// Trim any whitespace at the start and end of a stringview
	RED_CONTAINERS_API StringView Trim( const StringView& view );

	// Returns true if string contains nothing but white spaces
	// (i.e. spaces, tabs, new lines, carriage returns)
	RED_CONTAINERS_API Bool IsAllWhiteSpace( const StringView& view );

	// Outputs the @number to the output string. The output number will always be above 1, so displaying 500 will display 500 B, and never 0.5 KB
	// @param precision - number of digits after the decimal point.
	RED_CONTAINERS_API String FormatByteNumber( Uint64 number, Uint32 precision = 1 );

	// Matches the string against pattern, using fuzzy find
	//		FuzzyMatch( "Abrakadabra to czary i magia", "magia")    == true
	//		FuzzyMatch( "Abrakadabra to czary i magia", "aktcim")   == true
	//		FuzzyMatch( "Abrakadabra to czary i magia", "aktc im")  == true
	//		FuzzyMatch( "Abrakadabra to czary i magia", "aktc  im") == false
	//		FuzzyMatch( "Abrakadabra to czary i magia", "kbratoma") == true
	RED_CONTAINERS_API Bool FuzzyMatch( const char* string, const char* pattern );
	RED_CONTAINERS_API Bool FuzzyMatch( const red::String& string, const red::String& pattern );

	// Compares two strings in a more user-friendly fashion treating numbers as an entity rather than just a sequence of characters
	// This means that 9 < 80 where as a pure character based approach will return 80 < 9
	RED_CONTAINERS_API signed int CompareAlphaNum( const red::StringView& left, const red::StringView& right );

	namespace prv
	{
	// DO NOT call this function directly, it's for internal use only
	RED_CONTAINERS_API String Internal_StrCatPieces( std::initializer_list<red::StringView> pieces );
	RED_CONTAINERS_API void Internal_StrAppendPieces( String& str, std::initializer_list<red::StringView> pieces );
	}

	// Faster string concatenation
	// Stolen from abseil but reimplemented for our purposes
	RED_INLINE String StrCat()
	{
		return String();
	}

	RED_INLINE String StrCat( const StringView& a )
	{
		return a.ToString();
	}

	RED_CONTAINERS_API String StrCat( const StringView& a, const StringView& b );
	RED_CONTAINERS_API String StrCat( const StringView& a, const StringView& b, const StringView& c );
	RED_CONTAINERS_API String StrCat( const StringView& a, const StringView& b, const StringView& c, const StringView& d );

	template <typename... Values>
	RED_INLINE String StrCat( const StringView& a, const StringView& b, const StringView& c, const StringView& d, const StringView& e, const Values&... args )
	{
		return prv::Internal_StrCatPieces( { a, b, c, d, e, args... } );
	}

	// Faster string appending
	// Stolen from abseil but reimplemented for our purposes
	RED_INLINE void StrAppend( String& str )
	{
		RED_UNUSED( str );
	}

	RED_CONTAINERS_API void StrAppend( String& str, const StringView& a );
	RED_CONTAINERS_API void StrAppend( String& str, const StringView& a, const StringView& b );
	RED_CONTAINERS_API void StrAppend( String& str, const StringView& a, const StringView& b, const StringView& c );
	RED_CONTAINERS_API void StrAppend( String& str, const StringView& a, const StringView& b, const StringView& c, const StringView& d );

	template <typename... Values>
	RED_INLINE void StrAppend( String& str, const StringView& a, const StringView& b, const StringView& c, const StringView& d, const StringView& e, const Values&... args )
	{
		return prv::Internal_StrAppendPieces( str, { a, b, c, d, e, args... } );
	}

	RED_CONTAINERS_API String Base64Encode( const red::StringView string );
	RED_CONTAINERS_API String Base64Decode( const red::StringView string );
} // namespace red
