/*
 * Copyright (c) 2016 CD Projekt Red. All Rights Reserved.
 */

#include "build.h"
#include "string/stringUtils.h"
#include "string/stringView.h"

#include "../../redSystem/include/base64.h"

#include <numeric>

namespace red
{
	bool IsGlobString( const StringView string )
	{
		return string.Find( '*' ) != StringView::npos || string.Find( '?' ) != StringView::npos;
	}

	bool GlobMatch( const StringView string, const StringView pattern )
	{
		if ( string.Empty() )
		{
			if ( pattern.Empty() )
			{
				return true;
			}

			if ( pattern.Front() == '*' )
			{
				return GlobMatch( string, pattern.SubView( 1 ) );
			}

			return false;
		}

		if ( pattern.Empty() )
		{
			return false;
		}

		if ( pattern.Front() == '?' || pattern.Front() == string.Front() )
		{
			return GlobMatch( string.SubView( 1 ), pattern.SubView( 1 ) );
		}

		if ( pattern.Front() == '*' )
		{
			return GlobMatch( string, pattern.SubView( 1 ) ) || GlobMatch( string.SubView( 1 ), pattern );
		}

		return false;
	}

	RED_INLINE bool IsCharacterDigit( const char c )						/// @todo DP: we could share this with CName & NameRegistry code, but project dependencies stand in the way
	{
		// ctremblay 99% of the time, character will be > than '9' so test it first.
		return c <= '9' && c >= '0';
	}

	Uint32 FindDigitOffset( const red::StringView & view )
	{
		Uint32 digitOffset = view.Length();
		for ( auto iter = view.rbegin(), end = view.rend(); iter != end; ++iter )
		{
			// '0' check is to make sure we keep 0 from this form : "MyName000123"
			// Could maybe/probably do something better though. Like storing number string length.
			if ( IsCharacterDigit( *iter ) )
			{
				if ( *iter != '0' )
				{
					digitOffset = static_cast<Uint32>(std::distance( iter, end )) - 1;
				}
			}
			else
			{
				break;
			}
		}

		return digitOffset;
	}

	StringView GetCoreNameString( const StringView str )
	{
		Uint32 digitOffset = FindDigitOffset( str );
		return str.SubView( 0, digitOffset );
	}

	//------------------------------------------------------------------------------

	static const char* ParseNumber( const char* beg, const char* end, Uint64& number )
	{
		// NOTE: This isn't actually an ideal situation here since the C string parsing function
		// can actually go past end while parsing the number. This technically breaks the view
		// but in the interests of pragmatism this will do for now and we will assume people 
		// will not pass us views that end in the middle of numbers

		char* ptr = nullptr;
		red::StringToInt( number, beg, &ptr, BaseTen ); // Don't care for the bool result, having MAX or MIN in number is fine
		return ptr < end ? ptr : end; // Fixup end here so we appear to stay in bounds
	}

	// Adapted from http://www.davekoelle.com/alphanum.html the hpp non MFC version
	signed int CompareAlphaNum( const red::StringView& left, const red::StringView& right )
	{
		enum class State { String, Number };
		State state = State::String;

		const char* lptr = left.begin();
		const char* lend = left.end();
		const char* rptr = right.begin();
		const char* rend = right.end();

		while ( lptr != lend && rptr != rend )
		{
			if ( state == State::String )
			{
				while ( lptr != lend && rptr != rend )
				{
					const char lchr = *lptr;
					const char rchr = *rptr;

					const bool ldigit = IsCharacterDigit( lchr );
					const bool rdigit = IsCharacterDigit( rchr );
					if ( ldigit && rdigit )
					{
						state = State::Number;
						break;
					}

					if ( lchr != rchr ) // Note: Don't need to compare ldigit != rdigit here
					{
						return lchr < rchr ? -1 : 1;
					}

					++lptr;
					++rptr;
				}
			}
			else // state == State::Number
			{
				Uint64 lnumber = 0ull, rnumber = 0ull;
				lptr = ParseNumber( lptr, lend, lnumber );
				rptr = ParseNumber( rptr, rend, rnumber );

				if ( lnumber != rnumber )
				{
					return lnumber < rnumber ? -1 : 1;
				}

				state = State::String;
			}
		}

		if ( rptr != rend )
		{
			return -1;
		}
		if ( lptr != lend )
		{
			return 1;
		}
		return 0;
	}

	//------------------------------------------------------------------------------

	red::DynArray< red::StringView > StrSplit( const red::StringView str, char separator )
	{
		return StrSplit( str, separator, red::PoolEngine() );
	}

	red::DynArray<red::StringView> StrSplit( const red::StringView str, const red::StringView separator )
	{
		return StrSplit( str, separator, red::PoolEngine() );
	}

	red::DynArray< red::StringView > StrSplit( const red::StringView str, char separator, const red::memory::Pool& pool )
	{
		red::DynArray< red::StringView > result{ pool };

		Uint32 strSize = str.Length();
		Uint32 separatorSize = 1u;

		Uint32 lastPos = 0;
		while ( lastPos < strSize )
		{
			Uint32 nextPos;
			Uint32 separatorPos = str.Find( separator, lastPos );
			if ( separatorPos == red::StringView::npos )
			{
				nextPos = separatorPos = strSize;
			}
			else
			{
				nextPos = separatorPos + separatorSize;
			}

			if ( lastPos < separatorPos )
			{
				result.PushBack( str.Slice( lastPos, separatorPos ) );
			}

			lastPos = nextPos;
		}

		return result;
	}
	
	red::DynArray<red::StringView> StrSplit( const red::StringView str, const red::StringView separator, const red::memory::Pool& pool )
	{
		red::DynArray< red::StringView > result{ pool };

		Uint32 strSize = str.Length();
		Uint32 separatorSize = separator.Length();

		Uint32 lastPos = 0;
		while ( lastPos < strSize )
		{
			Uint32 nextPos;
			Uint32 separatorPos = str.Find( separator, lastPos );
			if ( separatorPos == red::StringView::npos )
			{
				nextPos = separatorPos = strSize;
			}
			else
			{
				nextPos = separatorPos + separatorSize;
			}

			if ( lastPos < separatorPos )
			{
				result.PushBack( str.Slice( lastPos, separatorPos ) );
			}

			lastPos = nextPos;
		}

		return result;
	}

	//------------------------------------------------------------------------------

	red::String StrJoin( red::ArraySpan< red::StringView > stringParts, red::StringView separator )
	{
		red::String result;

		if ( !stringParts.Empty() )
		{
			Uint32 stringSize = std::accumulate( stringParts.begin(), stringParts.end(), separator.Length() * (stringParts.Size() - 1),
				[]( Uint32 total, const red::StringView& str ) -> Uint32
			{
				return total + str.Length();
			} );

			result.Reserve( stringSize );

			red::StringView& firstStr = stringParts.PopFront();
			result.Append( firstStr );

			for ( const auto& str : stringParts )
			{
				result.Append( separator );
				result.Append( str );
			}
		}

		return result;
	}

	red::String StrJoin( red::ArraySpan< const red::String > stringParts, red::StringView separator )
	{
		red::String result;

		if ( !stringParts.Empty() )
		{
			Uint32 stringSize = std::accumulate( stringParts.begin(), stringParts.end(), separator.Length() * (stringParts.Size() - 1),
				[]( Uint32 total, const red::StringView& str ) -> Uint32
			{
				return total + str.Length();
			} );

			result.Reserve( stringSize );

			const red::String& firstStr = stringParts.PopFront();
			result.Append( firstStr );

			for ( const auto& str : stringParts )
			{
				result.Append( separator );
				result.Append( str );
			}
		}

		return result;
	}

	//------------------------------------------------------------------------------

	namespace prv
	{

	static char* Internal_Append( char* dest, const red::StringView& str )
	{
		char* result = dest + str.Length();
		if ( str.Length() != 0 )
		{
			red::Memcpy( dest, str.Data(), str.Length() );
		}
		return result;
	}

	String Internal_StrCatPieces( std::initializer_list< red::StringView > pieces )
	{
		Uint32 stringSize = 0;
		for ( const auto& piece : pieces )
		{
			stringSize += piece.Length();
		}

		red::String result;
		result.Resize( stringSize );
		char* dest = result.AsChar();
		for ( const red::StringView& piece : pieces )
		{
			const Uint32 pieceLength = piece.Length();
			if ( pieceLength != 0 )
			{
				red::Memcpy( dest, piece.Data(), pieceLength );
				dest += pieceLength;
			}
		}

		return result;
	}

	} // prv

	String StrCat( const StringView& a, const StringView& b )
	{
		red::String result{ a.Length() + b.Length() };
		result.Append( a );
		result.Append( b );
		return result;
	}

	String StrCat( const StringView& a, const StringView& b, const StringView& c )
	{
		red::String result{ a.Length() + b.Length() + c.Length() };
		result.Append( a );
		result.Append( b );
		result.Append( c );
		return result;
	}

	String StrCat( const StringView& a, const StringView& b, const StringView& c, const StringView& d )
	{
		red::String result{ a.Length() + b.Length() + c.Length() + d.Length() };
		result.Append( a );
		result.Append( b );
		result.Append( c );
		result.Append( d );
		return result;
	}

	// TODO Add assert here to check string overlap

	void StrAppend( String& str, const StringView& a )
	{
		str.Append( a );
	}
	
	void StrAppend( String& str, const StringView& a, const StringView& b )
	{
		str.Reserve( str.Length() + a.Length() + b.Length() );
		str.Append( a );
		str.Append( b );
	}
	
	void StrAppend( String& str, const StringView& a, const StringView& b, const StringView& c )
	{
		str.Reserve( str.Length() + a.Length() + b.Length() + c.Length() );
		str.Append( a );
		str.Append( b );
		str.Append( c );
	}
	
	void StrAppend( String& str, const StringView& a, const StringView& b, const StringView& c, const StringView& d )
	{
		str.Reserve( str.Length() + a.Length() + b.Length() + c.Length() + d.Length() );
		str.Append( a );
		str.Append( b );
		str.Append( c );
		str.Append( d );
	}

	namespace prv
	{

	void Internal_StrAppendPieces( String& str, std::initializer_list<red::StringView> pieces )
	{
		Uint32 stringSize = 0;
		for ( const auto& piece : pieces )
		{
			stringSize += piece.Length();
		}

		str.Resize( stringSize );
		char* dest = str.AsChar();
		for ( const red::StringView& piece : pieces )
		{
			const Uint32 pieceLength = piece.Length();
			if ( pieceLength != 0 )
			{
				red::Memcpy( dest, piece.Data(), pieceLength );
				dest += pieceLength;
			}
		}
	}

	} // prv

	//------------------------------------------------------------------------------

	Bool IsIntegerBase10( const red::StringView str, Bool allowNegative )
	{
		const AnsiChar* p = str.Data();
		const AnsiChar* pEnd = p + str.Length();

		if ( p == pEnd )
			return false;

		if ( allowNegative && (*p == '-') )
		{
			++p;
			if ( p == pEnd )
				return false;     // Single minus character is not a number.
		}

		do
		{
			AnsiChar c = *(p++);
			if ( (c < '0') || (c > '9') )
				return false;
		} while ( p < pEnd );

		return true;
	}

	StringView TrimLeft( const StringView& view )
	{
		for ( Uint32 i = 0; i < view.Length(); ++i )
		{
			const char c = view[ i ];

			if ( !IsWhiteSpace( c ) )
			{
				return view.SubView( i );
			}
		}

		return {};
	}

	StringView TrimRight( const StringView& view )
	{
		for ( Int32 i = view.Length() - 1; i >= 0; --i )
		{
			const char c = view[ i ];

			if ( !IsWhiteSpace( c ) )
			{
				return view.SubView( 0, i + 1 );
			}
		}

		return {};
	}

	StringView Trim( const StringView& view )
	{
		return TrimLeft( TrimRight( view ) );
	}

	Bool IsAllWhiteSpace( const StringView& view )
	{
		for ( Uint32 i = 0; i < view.Length(); ++i )
		{
			const char c = view[ i ];

			if ( !red::IsWhiteSpace( c ) )
			{
				return false;
			}
		}

		return true;
	}

	static void local_PrintF( char* buf, Uint32 bufSize, const char* format, ... )
	{
		va_list arglist;
		va_start( arglist, format );
		red::VSNPrintF( buf, bufSize, format, arglist );
		va_end( arglist );
	}

	String FormatByteNumber( Uint64 number, Uint32 precision )
	{
		enum class StringDisplayByteFormat
		{
			B, KB, MB, GB,
			COUNT
		};

		static constexpr Uint64 baseDivider[(size_t)StringDisplayByteFormat::COUNT] = {
			1ll,
			RED_KILO_BYTE( 1ll ),
			RED_MEGA_BYTE( 1ll ),
			RED_GIGA_BYTE( 1ll )
		};

		static const char* appendices[ static_cast<size_t>( StringDisplayByteFormat::COUNT ) ] = {
			" B",
			" KB",
			" MB",
			" GB"
		};

		if ( number == 0ll )
		{
			return String( "0 B" );
		}

		Uint32 formatPrecision = 0;
		StringDisplayByteFormat format = StringDisplayByteFormat::B;
		if ( number / baseDivider[ static_cast<size_t>( StringDisplayByteFormat::GB ) ] > 0 )
		{
			format = StringDisplayByteFormat::GB;
			formatPrecision = 9;
		}
		else if ( number / baseDivider[ static_cast<size_t>( StringDisplayByteFormat::MB ) ] > 0 )
		{
			format = StringDisplayByteFormat::MB;
			formatPrecision = 6;
		}
		else if ( number / baseDivider[ static_cast<size_t>( StringDisplayByteFormat::KB ) ] > 0 )
		{
			format = StringDisplayByteFormat::KB;
			formatPrecision = 3;
		}

		//Clamp the precision to make sense
		precision = Min( precision, formatPrecision );

		const Uint64 divider = baseDivider[ static_cast<size_t>(format) ] / static_cast<Uint64>( pow( 10, precision ) );
		number /= divider;

		const Uint32 nbOfDigits = static_cast<Uint32>( log10( number ) ) + 1;
		const Uint32 decimalPointDot = precision != 0 ? 1 : 0;
		const Uint32 preDecimalPointDots = ( nbOfDigits > precision ) ? ( nbOfDigits - precision - 1 ) / 3 : 0;
		constexpr Uint32 postDecimalPointDots = 0;
		const Uint32 dotsToInsert = preDecimalPointDots + decimalPointDot + postDecimalPointDots;

		const char* appendix = appendices[ static_cast<size_t>( format ) ];
		const Uint32 formatAppendixLength = static_cast<Uint32>( strlen( appendix ) );

		String outputString;
		outputString.Reserve( nbOfDigits + dotsToInsert + formatAppendixLength );

		char buffer[64];
		local_PrintF( buffer, RED_ARRAY_COUNT( buffer ), "%llu", number );
		outputString.Set( buffer );

		Uint32 idx = outputString.Length();
		//Insert decimal point dot.
		if ( precision != 0 )
		{
			idx -= precision;
			outputString.Insert( idx, '.' );
		}
		else
		{
			--idx;
		}

		if ( preDecimalPointDots > 0 )
		{
			Uint32 counter = 0;
			while ( idx > 0 )
			{
				if ( counter == 3 )
				{
					outputString.Insert( idx, ',' );
					counter = 0;
				}
				else
				{
					idx--;
					counter++;
				}
			}
		}

		outputString.Append( appendix, formatAppendixLength );

		return outputString;
	}

	// https://www.forrestthewoods.com/blog/reverse_engineering_sublime_texts_fuzzy_match/
	Bool FuzzyMatch( const char* string, const char* pattern )
	{
		while ( *pattern != '\0' && *string != '\0' )
		{
			if ( tolower( *pattern ) == tolower( *string ) )
			{
				++pattern;
			}
			++string;
		}

		return *pattern == '\0';
	}

	Bool FuzzyMatch( const red::String& string, const red::String& pattern )
	{
		return FuzzyMatch( string.AsChar(), pattern.AsChar() );
	}

	String Base64Encode( const StringView string )
	{
		// adding 1 because red::Base64Encode adds null byte
		const Uint32 encodedDataSize = red::Base64EncodedSize( string.Length() ) + 1;

		red::String output;
		output.Resize( encodedDataSize );

		const Bool result = red::Base64Encode( string.Data(), string.Length(), &output[ 0 ], encodedDataSize );
		RED_ASSERT( result, "Failed to encode string to base64" );

		return output;
	}

	String Base64Decode( const StringView string )
	{
		const Uint32 decodedSize = red::Base64DecodedSize( string.Length() );

		red::String output;
		output.Resize( decodedSize );

		if ( !red::Base64Decode( string.Data(), string.Length(), &output[ 0 ], decodedSize ) )
		{
			return String::EMPTY();
		}

		output.TrimRight( '\0' );

		return output;
	}

} // namespace red
