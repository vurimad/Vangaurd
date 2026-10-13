#include "build.h"
#include "string.h"

#include "../../include/string/stringView.h"
#include "../../redMemory/include/poolUtils.h"

#ifdef RED_DLL
	class RED_CONTAINERS_API red::String;
#endif

//////////////////////////////////////////////////////////////////////////
// usings
using red::String;
using red::DynArray;
using red::StringView;


#if defined( RED_USE_STRING_STATS )

red::Atomic< Uint32 > g_activeStringCount;
red::Atomic< Uint32 > g_activeDynamicStringCount;
red::Atomic< Int64 > g_dynamicStringMemory;

Uint32 red::Debug_GetActiveStringCount()
{
	return g_activeStringCount.GetValue();
}

Uint32 red::Debug_GetActiveDynamicStringCount()
{
	return g_activeDynamicStringCount.GetValue();
}

Int64 red::Debug_GetDynamicStringMemory()
{
	return g_dynamicStringMemory.GetValue();
}

#define RED_INCREMENT_STRING_COUNT() g_activeStringCount.Increment()
#define RED_DECREMENT_STRING_COUNT() g_activeStringCount.Decrement()
#define RED_INCREMENT_DYNAMIC_COUNT() g_activeDynamicStringCount.Increment()
#define RED_DECREMENT_DYNAMIC_COUNT() g_activeDynamicStringCount.Decrement()

#define RED_INCREASE_DYNAMIC_MEMORY( size ) g_dynamicStringMemory.ExchangeAdd( size );
#define RED_DECREASE_DYNAMIC_MEMORY( size ) g_dynamicStringMemory.ExchangeAdd(  -static_cast<Int64>(size) );

#else

#define RED_INCREMENT_STRING_COUNT()
#define RED_DECREMENT_STRING_COUNT()
#define RED_INCREMENT_DYNAMIC_COUNT()
#define RED_DECREMENT_DYNAMIC_COUNT()

#define RED_INCREASE_DYNAMIC_MEMORY( size )
#define RED_DECREASE_DYNAMIC_MEMORY( size )

#endif


/////////////////////////////////////////////////////////////////////////
// empty string data
const String& String::EMPTY()
{
	static String theEmptyString;
	return theEmptyString;
}

//////////////////////////////////////////////////////////////////////////
// set string length to 0 without deallocation
void String::Clear()
{
	// set len to 0
	_SetLength( 0 );
	_Terminate( 0 );
}

////////////////////////////////////////////////////////////////////////////////////////////////////
// create string with external buffer (const max capacity), user deals with buffer 'cstr' life-time
String String::CreateExternal( const char* cstr, const Uint32 maxCapacity, const Bool nullTerminate )
{
	// init
	const Uint32 cstrLen = nullTerminate ? 0 : static_cast<Uint32>( red::Strlen( cstr ) );
	const Uint32 capacity = Max( cstrLen, maxCapacity - 1 );  

	// set as external
	String tempStr;
	tempStr.e.mode = SM_External;
	tempStr.e.capacity = capacity;
	tempStr.e.length = cstrLen;
	tempStr.e.buf = const_cast<char*>( cstr );
	tempStr.e.poolAddr = 0ULL;						//not used

	// terminate if needed
	if ( nullTerminate )
	{
		RED_FATAL_ASSERT( cstrLen <= capacity );
		tempStr._Terminate( cstrLen );
	}

	return tempStr;
}

//////////////////////////////////////////////////////////////////////////
// todo: Get left part of the string
String String::LeftString( Uint32 count ) const
{
	return String( AsChar(), Clamp< Uint32 >( count, 0, Length() ) );
}

//////////////////////////////////////////////////////////////////////////
// todo: Get right part of string
String String::RightString( Uint32 count ) const
{
	const Uint32 start = Length() - Clamp< Uint32 >( count, 0, Length() );
	return String( AsChar() + start );
}

//////////////////////////////////////////////////////////////////////////
// todo: Get middle part of the string
String String::MidString( Uint32 start, Uint32 count ) const
{
	Uint32 end = start + count;
	start = Clamp< Uint32 >( start, 0, Length() );
	end = Clamp< Uint32 >( end, start, Length() );
	return String( AsChar() + start, end - start );
}

//////////////////////////////////////////////////////////////////////////

String& String::Append( const char* str, const Uint32 length )
{
	if ( length && str )
	{
		if ( !Data() )
		{
			Set( str, length );
		}
		else
		{
			const Uint32 oldLen = Length();
			const Uint32 newLen = oldLen + length;
			if ( Reserve( newLen ) )
			{
				// copy string data
				char* destBuffer = AsChar() + oldLen;
				red::Memcpy( destBuffer, str, length );

				_SetLength( newLen );
				_Terminate( newLen );
			}
		}
	}

	return *this;
}

String& String::Append( const char newChar )
{
	return Append( &newChar, 1 );
}

String& String::Append( const String& str )
{
	return Append( str.AsChar(), str.Length() );
}

String& String::Append( const StringView& str )
{
	return Append( str.Data(), str.Length() );
}


//////////////////////////////////////////////////////////////////////////
// internal
const Bool String::_IndexOf( const char wantedChar, Uint32& foundAtIndex, const Uint32 startIndex, const Bool noCase ) const
{
	const Uint32 len = Length();
	if ( len == 0 || len <= startIndex )
	{
		return false;
	}

	// init
	const Uint32 charsToCheck = len - startIndex;
	const char* currentChar = &AsChar()[ startIndex ];

	// case sensitive searching
	if ( !noCase )
	{
		for ( Uint32 no = 0; no < charsToCheck; ++no )
		{
			if ( *currentChar == wantedChar )
			{
				foundAtIndex = startIndex + no;
				return true;
			}

			++currentChar;
		}
	}

	// no case sensitive searching
	else
	{
		char lowerWantedChar = tolower( wantedChar );

		for ( Uint32 no = 0; no < charsToCheck; ++no )
		{
			if ( tolower( *currentChar ) == lowerWantedChar )
			{
				foundAtIndex = startIndex + no;
				return true;
			}

			++currentChar;
		}
	}

	// Not found
	return false;
}

//////////////////////////////////////////////////////////////////////////
// internal
const Bool String::_IndexOf( const char* wantedCString, Uint32& foundAtIndex, const Uint32 startIndex, const Bool noCase ) const
{
	// get length of the substring
	const Uint32 patternLength = wantedCString ? static_cast<Uint32>( red::Strlen( wantedCString ) ) : 0;
	const Uint32 len = Length();

	if ( len == 0 || patternLength == 0 || len < patternLength )
	{
		return false;
	}

	// init
	const Uint32 start = Clamp( startIndex, 0U, len-patternLength );
	const Uint32 charsToCheck = 1 + len - start - patternLength;
	const char* currentChar = &AsChar()[ start ];

	// case sensitive searching
	if ( !noCase )
	{
		for ( Uint32 no = 0; no < charsToCheck; ++no )
		{
			if ( red::Strcmp( currentChar, wantedCString, patternLength ) == 0 )
			{
				foundAtIndex = start + no;
				return true;
			}

			++currentChar;
		}
	}

	// no case sensitive searching
	else
	{
		for ( Uint32 no = 0; no < charsToCheck; ++no )
		{
			if ( red::StrcmpNC( currentChar, wantedCString, patternLength ) == 0 )
			{
				foundAtIndex = start + no;
				return true;
			}

			++currentChar;
		}
	}

	// Not found
	return false;
}

//////////////////////////////////////////////////////////////////////////
// internal
const Bool String::_IndexOfLast( const char wantedChar, Uint32& foundAtIndex, const Uint32 startIndex, const Bool noCase ) const
{
	const Uint32 len = Length();
	if ( len == 0 )
	{
		return false;
	}

	// init
	const Uint32 start = Clamp( startIndex, 0U, len - 1 );
	const Uint32 charsToCheck = 1 + start;
	const char* currentChar = &AsChar()[ start ];

	// case sensitive searching
	if ( !noCase )
	{
		for ( Uint32 no = 0; no < charsToCheck; ++no )
		{
			if ( *currentChar == wantedChar )
			{
				foundAtIndex = start - no;
				return true;
			}

			--currentChar;
		}
	}

	// no case sensitive searching
	else
	{
		char lowerWantedChar = tolower( wantedChar );

		for ( Uint32 no = 0; no < charsToCheck; ++no )
		{
			if ( tolower( *currentChar ) == lowerWantedChar )
			{
				foundAtIndex = start - no;
				return true;
			}

			--currentChar;
		}
	}

	// Not found
	return false;
}

//////////////////////////////////////////////////////////////////////////
// internal
const Bool String::_IndexOfLast( const char* wantedCString, Uint32& foundAtIndex, const Uint32 startIndex, const Bool noCase ) const
{
	// get length of the substring
	const Uint32 len = Length();
	const Uint32 patternLength = wantedCString ? (Uint32)Strlen( wantedCString ) : 0;

	if ( len == 0 || patternLength == 0 || len < patternLength )
	{
		return false;
	}

	// init
	const Uint32 start = Clamp( startIndex, 0U, len-patternLength );
	const Uint32 charsToCheck = 1 + start;
	const char* currentChar = &AsChar()[ start ];

	// case sensitive searching
	if ( !noCase )
	{
		for ( Uint32 no = 0; no < charsToCheck; ++no )
		{
			if ( Strcmp( currentChar, wantedCString, patternLength ) == 0 )
			{
				foundAtIndex = start - no;
				return true;
			}

			--currentChar;
		}
	}

	// no case sensitive searching
	else
	{
		for ( Uint32 no = 0; no < charsToCheck; ++no )
		{
			if ( StrcmpNC( currentChar, wantedCString, patternLength ) == 0 )
			{
				foundAtIndex = start - no;
				return true;
			}

			--currentChar;
		}
	}

	// Not found
	return false;
}

//////////////////////////////////////////////////
// todo: Returns the number of tokens found
Uint32 String::GetTokens( const char delim, Bool unique, DynArray< String >& tokens ) const
{
	const Uint32 length = Length();

	Uint32 result = 0;
	Uint32 index = 0;
	Uint32 tokenStart = 0;

	const char* buf = AsChar();
	for ( ; index<length; ++index )
	{
		if ( buf[ index ] == delim )
		{
			String tokenValue = MidString( tokenStart, index - tokenStart );
			if ( unique )
			{
				if ( red::alg::PushBackUnique( tokens, tokenValue ) )
				{
					++result;
				}
			}
			else
			{
				tokens.PushBack( tokenValue );
				++result;
			}

			tokenStart = index + 1;
		}
	}

	if ( tokenStart != length )
	{
		String tokenValue = MidString( tokenStart, length - tokenStart );
		if ( unique )
		{
			if ( red::alg::PushBackUnique( tokens, tokenValue ) )
			{
				++result;
			}
		}
		else
		{
			tokens.PushBack( tokenValue );
			++result;
		}
	}
	return result;
}

//////////////////////////////////////////////////////////////////////////
// todo:
// returns true if any of the given filter strings is a part of this string
Bool String::MatchAny( const DynArray< String > &filters ) const
{
	// Empty filter list
	if ( filters.Size() == 0 )
	{
		return true;
	}

	// Check filters
	const Uint32 filtersCount = filters.Size();
	for ( Uint32 i = 0; i < filtersCount; ++i )
	{
		if ( Contains( filters[ i ] ) )
		{
			return true;
		}
	}

	// Not matched
	return false;
}

//////////////////////////////////////////////////////////////////////////
// todo: Returns true if every of the given filter strings is a part of this string
Bool String::MatchAll( const DynArray< String >& filters ) const
{
	// Empty filter list
	if ( filters.Size() == 0 )
	{
		return true;
	}

	// Check filters
	const Uint32 filtersCount = filters.Size();
	for ( Uint32 i = 0; i < filtersCount; ++i )
	{
		if ( !Contains( filters[ i ] ) )
		{
			return false;
		}
	}

	// Matched
	return true;
}

//////////////////////////////////////////////////////////////////////////
// todo:
Bool String::Replace( const String& src, const String& target, Bool rightSide )
{
	String left;
	String right;

	if ( rightSide ? SplitFromRight( src, &left, &right ) : Split( src, &left, &right ) )
	{
		*this = left + target + right;
		return true;
	}

	return false;
}

//////////////////////////////////////////////////////////////////////////
// todo: Replace substring with another
Bool String::ReplaceAll( const String& src, const String& target, const red::memory::Pool &pool)
{

	DynArray<String> chunks { pool };

	String text = *this;
	String left;
	String right;
	while ( text.Split( src, &left, &right ) )
	{
		text = right;
		chunks.PushBack( left );
	}

	// Add the final left over
	chunks.PushBack( text );
	chunks.Shrink();

	String newStr;
	size_t count = chunks.Size();
	for ( size_t i = 0; i < count; ++i )
	{
		newStr += chunks[ static_cast< Int32 >( i ) ];
		if ( i < ( count - 1 ) )
		{
			newStr += target;
		}
	}

	*this = newStr;
	return !!chunks.Size();
}

//////////////////////////////////////////////////////////////////////////
// todo:
void String::ReplaceAll( char src, char target )
{
	const Uint32 len = Length();
	char* buf = AsChar();
	for ( Uint32 i = 0; i < len; ++i )
	{
		if ( buf[ i ] == src )
		{
			buf[ i ] = target;
		}
	}
}

void String::Replace( char src, char target, Uint32 startIndex, Uint32 endIndex )
{
	const Uint32 end = Min( Length(), endIndex );
	char* buf = AsChar();
	for ( Uint32 i = startIndex; i < end; ++i )
	{
		if ( buf[ i ] == src )
		{
			buf[ i ] = target;
		}
	}
}

//////////////////////////////////////////////////////////////////////////
// todo: Split string into two parts
const Bool String::_Split( const String& separator, String* leftPart, String* rightPart, Bool rightSide ) const
{
	if ( Empty() )
		return false;

	// Find where to split
	Uint32 splitPos;
	const Bool result = rightSide ? IndexOfLast( separator, splitPos ) : IndexOf( separator, splitPos );
	if ( result )
	{
		// Get left part
		if ( leftPart )
		{
			*leftPart = LeftString( splitPos );
		}

		// Get right part
		if ( rightPart )
		{
			*rightPart = MidString( splitPos + separator.Length(), Length() );
		}
	}

	return result;
}

//////////////////////////////////////////////////////////////////////////
// todo: Split string into two parts
const Bool String::_Split( const DynArray< String >& separators, String* leftPart, String* rightPart, Bool rightSide ) const
{
	if ( Empty() )
		return false;

	// Find where to split
	Uint32 splitPos = rightSide ? 0 : Length();
	Uint32 seperatorIndex = 0;

	Uint32 pos;
	Bool found = false;
	const Uint32 separatorsCount = separators.Size();
	for ( Uint32 i = 0; i < separatorsCount; ++i )
	{
		const Bool ret = rightSide ? IndexOfLast( separators[ i ], pos ) : IndexOf( separators[ i ], pos );
		if ( ret )
		{
			if ( (rightSide && pos >= splitPos) || (!rightSide && pos <= splitPos) )
			{
				splitPos = pos;
				seperatorIndex = i;
				found = true;

				break;
			}
		}
	}

	if ( found )
	{
		// Get left part
		if ( leftPart )
		{
			*leftPart = LeftString( splitPos );
		}

		// Get right part
		if ( rightPart )
		{
			*rightPart = MidString( splitPos + separators[ seperatorIndex ].Length() );
		}
	}

	return found;
}

//////////////////////////////////////////////////////////////////////////
// todo: Split into a list of tokens
DynArray< String > String::Split(const String& separator, bool includeEmpty, const red::memory::Pool &pool) const
{
	
	DynArray< String > items { pool };

	String text = *this;
	String left;
	String right;

	while ( text.Split( separator, &left, &right ) )
	{
		text = right;

		if ( !left.Empty() || includeEmpty )
		{
			items.PushBack( left );
		}
	}

	// Add the final left over
	// Do not test for 'includeEmpty', because here empty token means end-of-string
	if ( !text.Empty() )
	{
		items.PushBack( text );
	}

	// Done
	items.Shrink();
	return items;
}

//////////////////////////////////////////////////////////////////////////
// todo: Split into a list of tokens
DynArray< String > String::Split(const DynArray< String >& separators, const red::memory::Pool &pool) const
{
	
	DynArray< String > items{ pool };

	String text = *this;
	String left;
	String right;

	while ( text.Split( separators, &left, &right ) )
	{
		text = right;
		left.Trim();
		if ( !left.Empty() )
		{
			items.PushBack( left );
		}
	}

	// Trim the rest of the text
	text.Trim();

	// Add the final left over
	if ( !text.Empty() )
	{
		items.PushBack( text );
	}

	// Done
	items.Shrink();
	return items;
}

//////////////////////////////////////////////////////////////////////////
// todo: slice into parts using given separator
void String::Slice( DynArray< String >& parts, const String& separator ) const
{
	String str = *this;

	// Cut string into parts
	while ( !str.Empty() )
	{
		// Split
		String left;
		String right;
		if ( !str.Split( separator, &left, &right ) )
		{
			// Take all that's left
			left = str;
		}

		// New part
		parts.PushBack( left );

		// Continue slicing with what's left
		str = right;
	}
}

String String::Join( const red::ArraySpan< const String > &parts, const String &glue, const red::memory::Pool &pool)
{
	String ret { pool };

	if ( !parts.Empty() )
	{
		Uint32 length = 0;
		for ( auto it = parts.begin(), end = parts.end(); it != end; ++it )
		{
			length += it->Length();
		}
		length += (parts.Size() - 1) * glue.Length();

		ret.Reserve( length );
		auto it = parts.begin(), end = parts.end();
		ret += *it;
		for( ++it; it != end; ++it )
		{
			ret += glue;
			ret += *it;
		}
	}

	return ret;
}

//////////////////////////////////////////////////////////////////////////
// todo: check if ending matches pattern
Bool String::EndsWith( const String& str ) const
{
	const size_t len = str.Length();
	if ( len <= Length() )
	{
		return red::Strcmp( AsChar() + Length() - len, str.AsChar(), static_cast< Int32 >( len ) ) == 0;
	}
	return false;
}

//////////////////////////////////////////////////////////////////////////
// todo: check if start matches pattern
Bool String::BeginsWith( const String &str ) const
{
	const size_t len = str.Length();
	if ( len <= Length() )
	{
		return red::Strcmp( AsChar(), str.AsChar(), static_cast< Int32 >( len ) ) == 0;
	}
	return false;
}

Bool String::BeginsWithNoCase( const String &str ) const
{
	const size_t len = str.Length();
	if ( len <= Length() )
	{
		return red::StrcmpNC( AsChar(), str.AsChar(), static_cast< Int32 >( len ) ) == 0;
	}
	return false;
}

//////////////////////////////////////////////////////////////////////////
// returns part of the string before given separator, returns empty string if separator is not found
String String::_StringBefore( const String& separator, Bool rightSide ) const
{
	String left;
	if ( rightSide ? SplitFromRight( separator, &left, nullptr ) : Split( separator, &left, nullptr ) )
	{
		return left;
	}
	return String::EMPTY();
}

//////////////////////////////////////////////////////////////////////////
// returns part of the string after given separator, returns empty string if separator is not found
String String::_StringAfter( const String &separator, Bool rightSide ) const
{
	String right;
	if ( rightSide ? SplitFromRight( separator, nullptr, &right ) : Split( separator, nullptr, &right ) )
	{
		return right;
	}
	return String::EMPTY();
}

//////////////////////////////////////////////////////////////////////////
//
char String::Front() const
{
	if ( !Empty() )
	{
		return AsChar()[ 0 ];
	}
	return 0;
}

//////////////////////////////////////////////////////////////////////////
//
char String::Back() const
{
	if ( !Empty() )
	{
		return AsChar()[ Length() - 1 ];
	}
	return 0;
}

//////////////////////////////////////////////////////////////////////////
// todo: return lower case string
String& String::ToLower()
{
	red::StrToLower( AsChar(), Length() );
	return *this;
}

//////////////////////////////////////////////////////////////////////////
// todo: return upper case string
String& String::ToUpper()
{
	red::StrToUpper( AsChar(), Length() );
	return *this;
}

//////////////////////////////////////////////////////////////////////////
// todo: removes all white space from the string ending
String& String::TrimRight()
{
	Uint32 newSize = Length();
	const String & constString = *this;
	const char* buf = constString.AsChar();
	while( newSize > 0 && red::IsWhiteSpace( buf[ newSize - 1 ] ) )
	{
		--newSize;
	}
	if ( newSize > 0 )
	{
		*this = LeftString( newSize );
	}
	else
	{
		Clear();
	}

	return *this;
}

//////////////////////////////////////////////////////////////////////////
// todo: removes all occurrences of 'c' from the string ending
String& String::TrimRight( char _char )
{
	const Uint32 dataSize = Length();
	if ( dataSize == 0 )
	{
		return *this;
	}
	Uint32 newSize = dataSize;
	const char* buf = AsChar();
	while ( newSize > 0 && buf[ newSize - 1 ] == _char )
	{
		--newSize;
	}
	if ( newSize > 0 )
	{
		*this = LeftString( newSize );
	}
	else
	{
		Clear();
	}

	return *this;
}

//////////////////////////////////////////////////////////////////////////
// todo: removes all occurrences of 'c' from the string beginning
String& String::TrimLeft( char _char )
{
	const Uint32 dataSize = Length();
	if ( dataSize == 0 )
	{
		return *this;
	}
	Uint32 shift = 0;
	const char* buf = AsChar();
	while ( shift < dataSize && buf[ shift ] == _char )
	{
		++shift;
	}
	if ( shift < dataSize )
	{
		*this = RightString( dataSize - shift );
	}
	else
	{
		Clear();
	}

	return *this;
}

//////////////////////////////////////////////////////////////////////////
// todo: removes all white space from the string beginning
String& String::TrimLeft()
{
	const Uint32 dataSize = Length();
	if ( dataSize == 0 )
	{
		return *this;
	}
	Uint32 shift = 0;
	const char* buf = AsChar();
	while ( shift < dataSize && red::IsWhiteSpace( buf[ shift ] ) )
	{
		++shift;
	}
	if ( shift < dataSize )
	{
		*this = RightString( dataSize - shift );
	}
	else
	{
		Clear();
	}

	return *this;
}

//////////////////////////////////////////////////////////////////////////
// todo: removes all white space from the both string sides
String& String::Trim()
{
	return TrimLeft().TrimRight();
}

//////////////////////////////////////////////////////////////////////////
// todo: removes all white space from the both string sides
String String::TrimCopy() const
{
	return String( *this ).TrimLeft().TrimRight();
}

//////////////////////////////////////////////////////////////////////////

String& String::Set( const char* str )
{
	const Uint32 newLen = str ? static_cast<Uint32>( red::Strlen( str ) ) : 0;
	Set( str, newLen );
	return *this;
}

String& String::Set( const char* str, const Uint32 newLen )
{
	// set new len
	if ( Resize( newLen ) )
	{
		// copy string data
		if ( str && newLen )
		{
			red::Memcpy( Data(), str, newLen );
		}

		// terminate when needed
		if ( newLen || _IsInternal() )
		{
			_Terminate( newLen );
		}
	}

	return *this;
}

String& String::Set( const String& str )
{
	// ultra fast internal string copier
	if ( _IsInternal() && str._IsInternal() )
	{
		f = str.f;
		return *this;
	}

	Set( str.AsChar(), str.Length() );
	return *this;
}

String& String::Set( const StringView& str )
{
	Set( str.Data(), str.Length() );
	return *this;

}


//////////////////////////////////////////////////////////////////////////

static void* Internal_StringAllocate( red::memory::Pool* pool, Uint32 size )
{
	if (pool == nullptr)
	{
		return RED_ALLOCATE_ALIGNED( RED_CONTAINER_STRING_DEFAULT_POOL_NAME, size, String::ALIGNMENT );
	}
	else 
	{
		return red::memory::AllocateAligned( *pool, size, String::ALIGNMENT );
	}
}

static void* Internal_StringReallocate( red::memory::Pool* pool, void* ptr, Uint32 size )
{
	if ( pool == nullptr )
	{
		return RED_REALLOCATE_ALIGNED( RED_CONTAINER_STRING_DEFAULT_POOL_NAME, ptr, size, String::ALIGNMENT );
	} 
	else 
	{
		return red::memory::ReallocateAligned( *pool, ptr, size, String::ALIGNMENT );
	} 
}

static void Internal_StringFree( red::memory::Pool* pool, void* ptr )
{
	if ( pool == nullptr )
	{
		RED_FREE( RED_CONTAINER_STRING_DEFAULT_POOL_NAME, ptr );
	} 
	else 
	{
		red::memory::Free( *pool, ptr );
	}
}

void String::_ResizeInternalBuffer(const Uint32 newCapacity)
{
	if (newCapacity >= INTERNAL_CAPACITY)
	{
		RED_FATAL_ASSERT( ( newCapacity & DYNAMIC_MAX_LENGTH_MASK ) == 0, "Cannot resize a string > 2^30-1 bytes" );

		// We are going from internal storage to dynamic storage
		red::memory::Pool* poolPtr = nullptr;
		if ( i.poolAddr != 0ULL )
		{
			poolPtr = reinterpret_cast<red::memory::Pool*>( &i.poolAddr );
		}

		const Uint32 toAllocate = newCapacity ? red::AlignUp(newCapacity + 1, ALIGNMENT) : 0;
		char* newBuf = static_cast<char*>( Internal_StringAllocate( poolPtr, toAllocate ) );
		RED_ASSERT( newBuf != nullptr, "Pool allocation / reallocation failed!");

		red::Memcpy( newBuf, i.buf, (i.length + 1) * sizeof(char) );

		e.buf = newBuf;
		e.capacity = toAllocate - 1;
		e.mode = SM_Dynamic;
		// Aliased to the same address, no change required
		//e.length = i.length;
		//e.poolAddr = i.poolAddr;

		RED_INCREMENT_DYNAMIC_COUNT();
		RED_INCREASE_DYNAMIC_MEMORY( toAllocate );
	}
	else
	{
		_Terminate(newCapacity ? newCapacity - 1: 0);
	}
}

void String::_ResizeDynamicBuffer(const Uint32 newCapacity)
{
	const Uint32 toAllocate = newCapacity ? red::AlignUp(newCapacity + 1, ALIGNMENT) : 0;
	const Uint32 toFree = e.capacity ? red::AlignUp(e.capacity + 1, ALIGNMENT) : 0;
	
	red::memory::Pool* poolPtr = nullptr;
	if ( e.poolAddr != 0ULL ) 
	{
		poolPtr = reinterpret_cast<red::memory::Pool*>( &e.poolAddr );
	}

	if ( toAllocate > INTERNAL_CAPACITY )
	{
		if ( toFree == 0 )
		{
			e.buf = static_cast<char*>( Internal_StringAllocate( poolPtr, toAllocate ) );
		}
		else if ( toAllocate != toFree )
		{
			e.buf = static_cast<char*>( Internal_StringReallocate( poolPtr, e.buf, toAllocate ) );
		}

		RED_ASSERT( e.buf != nullptr, "Pool allocation / reallocation failed!");

		e.capacity = toAllocate - 1;

		RED_INCREASE_DYNAMIC_MEMORY( static_cast<Int64>( toAllocate ) - toFree );
	}
	else //if ( toAllocate <= INTERNAL_CAPACITY )
	{
		i.mode = SM_Internal;

		if ( toFree > 0 )
		{
			char* oldBuf = e.buf;
			red::Memcpy( i.buf, oldBuf, toAllocate * sizeof(char) );
			Internal_StringFree( poolPtr, oldBuf );
		}

		RED_DECREASE_DYNAMIC_MEMORY( toFree );
		RED_DECREMENT_DYNAMIC_COUNT();
	}
}

//////////////////////////////////////////////////////////////////////////
// internal - resize buffer (change capacity)
void String::_ResizeBuffer(const Uint32 newCapacity)
{
	// resize external buffer
	if (_IsExternal())
	{
		return;
	}
	// resize internal buffer
	else if (_IsInternal())
	{
		_ResizeInternalBuffer(newCapacity);
	}
	// resize dynamic buffer
	else if (_IsDynamic())
	{
		RED_FATAL_ASSERT((newCapacity & DYNAMIC_MAX_LENGTH_MASK) == 0, "Cannot resize a string > 2^30-1 bytes");
		_ResizeDynamicBuffer(newCapacity);
	}
}

//////////////////////////////////////////////////////////////////////////

void String::RemoveWhiteSpaces()
{
	auto predicate = []( const char test ){ return test == ' ' || test == '\t' || test == '\n' || test == '\r'; }; // add more when needed
	Erase( std::remove_if( Begin(), End(), predicate ), End() );
}

void String::RemoveWhiteSpacesAndQuotes()
{
	auto predicate = []( const char test ){ return test == ' ' || test == '\t' || test == '\n' || test == '\r' || test == '\'' || test == '\"';  }; // add more when needed
	Erase( std::remove_if( Begin(), End(), predicate ), End() );
}

//////////////////////////////////////////////////////////////////////////
// todo:
Uint32 String::CountChars( char c ) const
{
	Uint32 ret = 0;
	for ( const_iterator i = Begin(); i < End(); ++i )
	{
		ret += (*i == c);
	}
	return ret;
}

//////////////////////////////////////////////////////////////////////////
// todo:
void String::SimpleHash( Uint32& hash ) const
{
	hash = SimpleHash( AsChar(), Length() );
}

//////////////////////////////////////////////////////////////////////////
// todo:
Uint32 String::SimpleHash( const char* text, const Uint32 length )
{
	//http://www.cogs.susx.ac.uk/courses/dats/notes/html/node114.html
	Uint32 hash = 0;
	for ( Uint32 i = 0; (i < length) && text[ i ]; ++i )
	{
		hash = text[ i ] + hash * 31;
	}

	return hash;
}

//////////////////////////////////////////////////////////////////////////
// todo:
Uint32 String::SimpleHash( const Char* text, const Uint32 length )
{
	Uint32 hash = 0;
	for ( Uint32 i = 0; (i < length) && text[ i ]; ++i )
	{
		hash = text[ i ] + hash * 31;
	}

	return hash;
}

RED_FORCE_INLINE void String::_Init(const EStringMode mode, const red::memory::Pool &pool) 
{
	// set init mode
	i.buf[0] = '\0';

	i.length = 0;
	i.mode = mode;
	i.poolAddr = *( reinterpret_cast<Uint64 *>(red::memory::AddressOf(pool)));
	
	RED_ASSERT(i.poolAddr != 0ULL, "poolAddress shouldn't be 0");

	RED_INCREMENT_STRING_COUNT();
}

RED_FORCE_INLINE void String::_Init() 
{
	// set init mode
	i.buf[0] = '\0';

	i.length = 0;
	i.poolAddr = 0ULL;
	i.mode = SM_Internal;

	RED_INCREMENT_STRING_COUNT();
}

//////////////////////////////////////////////////////////////////////////
// is string data stored in allocated dynamic buffer
RED_FORCE_INLINE const Bool String::_IsDynamic() const
{
	return (e.mode == SM_Dynamic);
}

//////////////////////////////////////////////////////////////////////////
// is string data stored internally in object without any external buffer
RED_FORCE_INLINE const Bool String::_IsInternal() const
{
	return e.mode == SM_Internal;
}

//////////////////////////////////////////////////////////////////////////
// is string buffer provided by user (const max capacity)
RED_FORCE_INLINE const Bool String::_IsExternal() const
{
	return e.mode == SM_External;
}

RED_FORCE_INLINE const Bool String::_IsValid() const
{
	RED_ASSERT((e.mode < SM_MAX), "Invalid Strig mode");
	return (e.mode < SM_MAX);
}

String::String(const red::memory::Pool &pool)
{
	_Init(SM_Internal, pool);
	RED_ASSERT(e.mode < SM_MAX, "Invalid String internal mode");
}

//////////////////////////////////////////////////////////////////////////
// ctor which reserves 'initCapacity' chars for future usage
String::String(const Uint32 initCapacity, const red::memory::Pool &pool)
{
	_Init(SM_Internal, pool);
	Reserve(initCapacity);
	RED_ASSERT(e.mode < SM_MAX, "Invalid String internal mode");
}

//////////////////////////////////////////////////////////////////////////
// ctor with on one 'fillChar' char
String::String(const char fillChar, const red::memory::Pool &pool)
{
	_Init(SM_Internal, pool);
	char txt[2] = { fillChar, 0 };
	Set(txt);
	RED_ASSERT(e.mode < SM_MAX, "Invalid String internal mode");
}

//////////////////////////////////////////////////////////////////////////
// ctor with c-style array of chars
String::String(const char* cstr, const red::memory::Pool &pool)
{
	_Init(SM_Internal, pool);
	Set(cstr);
	RED_ASSERT(e.mode < SM_MAX, "Invalid String internal mode");
}

//////////////////////////////////////////////////////////////////////////
// ctor with c-style array of chars which copy 'length' no. of chars (without null termination)
String::String(const char* cstr, const Uint32 length, const red::memory::Pool &pool)
{
	_Init(SM_Internal, pool);
	Set(cstr, length);
	RED_ASSERT(e.mode < SM_MAX, "Invalid String internal mode");
}

//////////////////////////////////////////////////////////////////////////
// copy ctor
String::String(const String& str, const red::memory::Pool &pool)
{
	_Init(SM_Internal, pool);
	Set(str);
	RED_ASSERT(e.mode < SM_MAX, "Invalid String internal mode");
}

//////////////////////////////////////////////////////////////////////////
// put termination character at specified offset 'at'
void String::_Terminate( const Uint32 at )
{
	char* ptr = AsChar();
	if ( ptr )
	{
		RED_ASSERT(at <= Capacity(), "Out of bound access.");
		ptr[ at ] = '\0';
	}

	RED_ASSERT(e.mode < SM_MAX, "Invalid String internal mode");
}


//////////////////////////////////////////////////////////////////////////
// def ctor
String::String()
{
	_Init();
	RED_ASSERT(e.mode < SM_MAX, "Invalid String internal mode");
}

//////////////////////////////////////////////////////////////////////////
// ctor which reserves 'initCapacity' chars for future usage
String::String( const Uint32 initCapacity )
{
	_Init();
	Reserve(initCapacity);
	RED_ASSERT(e.mode < SM_MAX, "Invalid String internal mode");
}

//////////////////////////////////////////////////////////////////////////
// ctor with on one 'fillChar' char
String::String( const char fillChar )
{
	_Init();
	char txt[2] = { fillChar, 0 };
	Set(txt);
	RED_ASSERT(e.mode < SM_MAX, "Invalid String internal mode");
}

//////////////////////////////////////////////////////////////////////////
// ctor with c-style array of chars
String::String( const char* cstr )
{
	_Init();
	Set( cstr );
	RED_ASSERT(e.mode < SM_MAX, "Invalid String internal mode");
}

//////////////////////////////////////////////////////////////////////////
// ctor with c-style array of chars which copy 'length' no. of chars
String::String( const char* cstr, const Uint32 length )
{
	_Init();
	Set( cstr, length );
	RED_ASSERT(e.mode < SM_MAX, "Invalid String internal mode");
}

//////////////////////////////////////////////////////////////////////////
// copy ctor
String::String( const String& str )
{
	if ( str.e.poolAddr == 0ULL )
	{
		_Init();
	} 
	else 
	{
		const red::memory::Pool &pool = *reinterpret_cast<const red::memory::Pool *>(&str.e.poolAddr);
		_Init(SM_Internal, pool);
	}

	Set( str );
	RED_ASSERT(e.mode < SM_MAX, "Invalid String internal mode");
}

//////////////////////////////////////////////////////////////////////////
// move ctor
String::String( String&& str )
{
	_Init();
	Swap( str );
	RED_ASSERT(e.mode < SM_MAX, "Invalid String internal mode");
}

//////////////////////////////////////////////////////////////////////////
// dtor
String::~String()
{
	// delete buffer when needed
	if ( !_IsExternal() )
	{
		RED_ASSERT(e.mode < SM_MAX, "Invalid String internal mode");
		Resize( 0 );
	}

	RED_DECREMENT_STRING_COUNT();
}

//////////////////////////////////////////////////////////////////////////
// assignment operator with c-style 'str' array
String& String::operator=( const char* str ) 
{
	return Set(str);
}

//////////////////////////////////////////////////////////////////////////
// copy assignment operator
String& String::operator=( const String& str )
{
	if ( this != &str )
	{
		Set( str );
	}
	return *this;
}

//////////////////////////////////////////////////////////////////////////
// move assignment operator
String& String::operator=( String&& str )
{
	String( std::move( str ) ).Swap( *this );
	return *this;
}

//////////////////////////////////////////////////////////////////////////

String& String::operator+=( char character )
{
	return Append( character );
}

String& String::operator+=( const char* cstr )
{
	if ( cstr && *cstr )
	{
		Append( cstr, static_cast<Uint32>( red::Strlen( cstr ) ) );
	}

	return *this;
}

String& String::operator+=( const String& str )
{
	return Append( str.AsChar(), str.Length() );
}

String& String::operator+=( const StringView& str )
{
	return Append( str.Data(), str.Length() );
}

//////////////////////////////////////////////////////////////////////////
// equal to c-style string operator
Bool String::operator==( const char* cstr ) const
{
	if ( !Data() && !cstr )
	{
		return true;
	}

	const Uint32 len = Length();
	if ( len == 0 && !cstr )
	{
		return true;
	}

	if ( len != red::Strlen( cstr ) )
	{
		return false;
	}

	return red::Strcmp( AsChar(), cstr, len ) == 0;
}

//////////////////////////////////////////////////////////////////////////
// equal to another string operator
Bool String::operator==( const String& str ) const
{
	if ( !Data() && !str.Data() )
	{
		return true;
	}

	const Uint32 len = Length();
	if ( len != str.Length() )
	{
		return false;
	}

	return red::Strcmp( AsChar(), str.AsChar(), len ) == 0;
}

//////////////////////////////////////////////////////////////////////////
// not equal to c-style string operator
Bool String::operator!=( const char* cstr ) const
{
	if ( !Data() && !cstr )
	{
		return false;
	}

	const Uint32 len = Length();
	if ( len == 0 && !cstr )
	{
		return false;
	}

	if ( len != red::Strlen( cstr ) )
	{
		return true;
	}

	return red::Strcmp( AsChar(), cstr, len ) != 0;
}

//////////////////////////////////////////////////////////////////////////
// not equal to another string operator
Bool String::operator!=( const String& str ) const
{
	if ( !Data() && !str.Data() )
	{
		return false;
	}

	const Uint32 len = Length();
	if ( len != str.Length() )
	{
		return true;
	}

	return red::Strcmp( AsChar(), str.AsChar(), len ) != 0;
}

//////////////////////////////////////////////////////////////////////////
//
const char& String::operator[]( Uint32 i ) const
{
	_BoundsCheck(i);
	return AsChar()[i];
}

//////////////////////////////////////////////////////////////////////////
//
char& String::operator[]( Uint32 i )
{
	_BoundsCheck(i);
	return AsChar()[i];
}

//////////////////////////////////////////////////////////////////////////
// equal to c-style string with no-case checking
const Bool String::EqualsNC( const char* cstr ) const
{
	if ( !Data() && !cstr )
	{
		return true;
	}

	const Uint32 len = Length();
	if ( len == 0 && !cstr )
	{
		return true;
	}

	if ( len != red::Strlen( cstr ) )
	{
		return false;
	}

	return red::StrcmpNC( AsChar(), cstr, len ) == 0;
}

//////////////////////////////////////////////////////////////////////////
// equal to another string with no-case checking
const Bool String::EqualsNC( const String& str ) const
{

	if ( !Data() && !str.Data() )
	{
		return true;
	}

	const Uint32 len = Length();
	if ( len != str.Length() )
	{
		return false;
	}

	return red::StrcmpNC( AsChar(), str.AsChar(), len ) == 0;
}

//////////////////////////////////////////////////////////////////////////
//
Bool String::Less( const char* buf1, size_t num1, const char* buf2, size_t num2 )
{
	while ( num1-- > 0 && num2-- > 0 )
	{
		if ( *buf1 < *buf2 )
		{
			return true;
		}
		else if ( *buf2 < *buf1 )
		{
			return false;
		}
		++buf1;
		++buf2;
	}

	return (num1 <= 0 && num2 > 0);
}

//// formatters
String String::Printf(STATIC_CHECK_PRINTF_MSC const char* format, ...)
{
	va_list arglist;
	va_start(arglist, format);
	char formattedBuf[4096];
	red::VSNPrintF(formattedBuf, RED_ARRAY_COUNT(formattedBuf), format, arglist);
	va_end(arglist);
	return formattedBuf;
}

String String::Printf(const red::memory::Pool &pool, STATIC_CHECK_PRINTF_MSC const char* format, ... )
{
	va_list arglist;
	va_start( arglist, format );
	char formattedBuf[ 4096 ];
	red::VSNPrintF( formattedBuf, RED_ARRAY_COUNT( formattedBuf ), format, arglist );
	va_end( arglist );
	return String(formattedBuf,pool);
}

String String::PrintfV(const red::memory::Pool &pool, const char* format, va_list arglist)
{
	char formattedBuf[4096];
	red::VSNPrintF(formattedBuf, RED_ARRAY_COUNT(formattedBuf), format, arglist);
	return String(formattedBuf, pool);
}

String String::PrintfV(const char* format, va_list arglist)
{
	char formattedBuf[4096];
	red::VSNPrintF(formattedBuf, RED_ARRAY_COUNT(formattedBuf), format, arglist);
	return formattedBuf;
}


//////////////////////////////////////////////////////////////////////////
// swap string objects
void String::Swap( String& str )
{
	std::swap(f.data0, str.f.data0);
	std::swap(f.data1, str.f.data1);
	std::swap(f.filler, str.f.filler);
	std::swap(f.strInfo, str.f.strInfo);
}

//////////////////////////////////////////////////////////////////////////
// check is string is empty
Bool String::Empty() const
{
	return Length() == 0;
}
//////////////////////////////////////////////////////////////////////////
// returns size of data
// no. of characters
Uint32 String::DataSize() const
{
	return Length() + 1;
}

//////////////////////////////////////////////////////////////////////////
// returns string capacity in bytes
Uint32 String::Capacity() const
{
	if ( _IsInternal() )
	{
		return String::INTERNAL_CAPACITY - 1;
	}
	return e.capacity;
}

///////////////////////////////////////////////////////
// returns string length in chars without trailing '/0' 
Uint32 String::Length() const
{
	if ( _IsInternal() )
	{
		RED_ASSERT( i.length < INTERNAL_CAPACITY,"String shouldn't exceed INTERNAL_CAPACITY.");
		return i.length;
	}

	RED_ASSERT((e.length & DYNAMIC_MAX_LENGTH_MASK) == 0 ,"Exceeded String dynamic length! (2^30-1 bytes)");
	return e.length;
}

///////////////////////////////////////////////////////////////
// internal set length (string length without null termination)
void String::_SetLength( const Uint32 newLen )
{
	// set new length
	if ( _IsInternal() )
	{
		i.length = newLen;
	}
	else
	{
		e.length = newLen;
	}
}

//////////////////////////////////////////////////////////////////////////
// get const c-style chars array
const char* String::AsChar() const
{
	if ( _IsInternal() )
	{
		return &i.buf[0];
	}
	return e.buf;
}

char* String::AsChar()
{
	if ( _IsInternal() )
	{
		return &i.buf[0];
	}

	return e.buf;
}

//////////////////////////////////////////////////////////////////////////
// returns const ptr to typeless raw buffer
const void* String::Data() const
{
	if ( _IsInternal() )
	{
		return &i.buf[0];
	}
	return e.buf;
}

void* String::Data()
{
	if ( _IsInternal() )
	{
		return &i.buf[0];
	}
	return e.buf;
}

//////////////////////////////////////////////////////////////////////////

Bool String::Reserve( const Uint32 newCapacity )
{
	if ( newCapacity > Capacity() )
	{
		_ResizeBuffer( newCapacity );
	}

	return newCapacity <= Capacity();
}

Bool String::Resize( const Uint32 newLen )
{
	if ( newLen )
	{
		if ( Reserve( newLen ) )
		{
			_SetLength( newLen );
			_Terminate( newLen );
			return true;
		}
	}
	else
	{
		_ResizeBuffer( 0 );
		_SetLength( 0 );
		_Terminate( 0 );
		return true;
	}

	// def result
	return false;
}


/////////////////////////////////////////////////////////////////////////
// todo: erase char at index <0, length)
const Bool String::Erase( iterator where )
{
	if ( where >= Begin() && where < End() )
	{
		red::Memmove( where, where + 1, (End() - where) );
		const Uint32 len = Length() - 1;
		_SetLength( len );
		_Terminate( len );

		// free buffer when empty
		if ( Length() == 0 )
		{
			_ResizeBuffer( 0 );
		}

		return true;
	}

	return false;
}

/////////////////////////////////////////////////////////////////////////
// todo: erase in range <first, last)
const Bool String::Erase( iterator first, iterator last )
{
	if ( first >= Begin() && last <= End() && last > first )
	{
		const Uint64 dif = last - first;
		red::Memmove( first, reinterpret_cast<void*>( first + dif ), (End() - first - dif + 1) );
		
		const Uint32 len = Length() - static_cast<Uint32>(dif);

		_SetLength( len );
		_Terminate( len );

		// free buffer when empty
		if ( Length() == 0 )
		{
			_ResizeBuffer( 0 );
		}

		return true;
	}

	return false;
}

// Erase in range
Bool String::Erase( Uint32 startIndex, Uint32 endIndex )
{
	return Erase( Begin() + startIndex, Begin() + endIndex );
}

/////////////////////////////////////////////////////////////////////////
// todo:
Bool String::Remove( const char& element )
{
	iterator i = std::find( Begin(), End(), element );
	if ( i != End() )
	{
		Erase( i );

		return true;
	}

	return false;
}

/////////////////////////////////////////////////////////////////////////
// todo:
Bool String::RemoveAt( const Uint32 index )
{
	const Uint32 prevLen = Length();
	if ( index < prevLen )
	{
		iterator beg = Begin() + index;
		red::Memmove( beg, beg + 1, (prevLen - index) );
		
		const Uint32 len = prevLen - 1;
		_SetLength( len );
		_Terminate( len );
		
		// free buffer when empty
		if ( Length() == 0 )
		{
			_ResizeBuffer( 0 );
		}

		return true;
	}

	return false;
}

//////////////////////////////////////////////////////////////////////////

bool String::Insert( const Uint32 index, const char* subString, const Uint32 subStringLen )
{
	const Uint32 prevLen = Length();
	if ( index <= prevLen && subString && subStringLen )
	{
		if ( Resize( prevLen + subStringLen ) )
		{
			// move right side
			if ( index < prevLen )
			{
				red::Memmove( AsChar() + index + subStringLen, AsChar() + index, (prevLen - index) );
			}

			// insert
			red::Memcpy( AsChar() + index, subString, subStringLen );

			// Call to Resize() puts in the null terminator
			return true;
		}
	}

	return false;
}

Bool String::Insert( const Uint32 index, const char newChar )
{
	return Insert( index, &newChar, 1 );
}

Bool String::Insert( const Uint32 index, const char* subCString )
{
	const Uint32 subCStringLen = static_cast<Uint32>( red::Strlen( subCString ) );
	return Insert( index, subCString, subCStringLen );
}

Bool String::Insert( const Uint32 index, const String& subString )
{
	return Insert( index, subString.AsChar(), subString.Length() );
}

bool String::Insert( const Uint32 index, const StringView& subString )
{
	return Insert( index, subString.Data(), subString.Length() );
}

//////////////////////////////////////////////////////////////////////////

String String::StringBefore( const String& separator ) const
{
	return _StringBefore( separator, false );
}

//////////////////////////////////////////////////////////////////////////
//
String String::StringAfter( const String& separator ) const
{
	return _StringAfter( separator, false );
}

//////////////////////////////////////////////////////////////////////////
//
String String::StringBeforeFromRight( const String& separator ) const
{
	return _StringBefore( separator, true );
}

//////////////////////////////////////////////////////////////////////////
//
String String::StringAfterFromRight( const String& separator ) const
{
	return _StringAfter( separator, true );
}

//////////////////////////////////////////////////////////////////////////
//
const Bool String::Split( const String& separator, String* leftPart, String* rightPart ) const
{
	return _Split( separator, leftPart, rightPart, false );
}

//////////////////////////////////////////////////////////////////////////
//
const Bool String::Split( const red::DynArray< String >& separators, String* leftPart, String* rightPart ) const
{
	return _Split( separators, leftPart, rightPart, false );
}

//////////////////////////////////////////////////////////////////////////
//
const Bool String::SplitFromRight( const String& separator, String* leftPart, String* rightPart ) const
{
	return _Split( separator, leftPart, rightPart, true );
}

//////////////////////////////////////////////////////////////////////////
//
const Bool String::SplitFromRight( const red::DynArray< String >& separators, String* leftPart, String* rightPart ) const
{
	return _Split( separators, leftPart, rightPart, true );
}

//////////////////////////////////////////////////////////////////////////
// check is 'wantedChar' exists and put result into 'foundAtIndex' or return false
const Bool String::Contains( const char wanterChar, const Uint32 startIndex ) const
{
	Uint32 result;
	return IndexOf( wanterChar, result, startIndex );
}

//////////////////////////////////////////////////////////////////////////
// check is 'wantedCString' exists and put result into 'foundAtIndex' or return false
const Bool String::Contains( const char* wantedCString, const Uint32 startIndex ) const
{
	Uint32 result;
	return IndexOf( wantedCString, result, startIndex );
}

//////////////////////////////////////////////////////////////////////////
// check is 'wantedString' exists and put result into 'foundAtIndex' or return false
const Bool String::Contains( const String& wantedString, const Uint32 startIndex ) const
{
	Uint32 result;
	return IndexOf( wantedString.AsChar(), result, startIndex );
}

//////////////////////////////////////////////////////////////////////////
// check is 'wantedChar' (no case sensitive) exists and put result into 'foundAtIndex' or return false
const Bool String::ContainsNoCase( const char wantedChar, const Uint32 startIndex ) const
{
	Uint32 result;
	return IndexOfNoCase( wantedChar, result, startIndex );
}

//////////////////////////////////////////////////////////////////////////
// check is 'wantedCString' (no case sensitive) exists and put result into 'foundAtIndex' or return false
const Bool String::ContainsNoCase( const char* wantedCString, const Uint32 startIndex ) const
{
	Uint32 result;
	return IndexOfNoCase( wantedCString, result, startIndex );
}

//////////////////////////////////////////////////////////////////////////
// check is 'wantedString' (no case sensitive) exists and put result into 'foundAtIndex' or return false
const Bool String::ContainsNoCase( const String& wantedString, const Uint32 startIndex ) const
{
	Uint32 result;
	return IndexOfNoCase( wantedString.AsChar(), result, startIndex );
}

//////////////////////////////////////////////////////////////////////////
// check from right is 'wantedChar' exists and put result into 'foundAtIndex' or return false
const Bool String::ContainsFromRight( const char wantedChar, const Uint32 startIndex ) const
{
	Uint32 result;
	return IndexOfLast( wantedChar, result, startIndex );
}

//////////////////////////////////////////////////////////////////////////
// check from right is 'wantedCString' exists and put result into 'foundAtIndex' or return false
const Bool String::ContainsFromRight( const char* wantedCString, const Uint32 startIndex ) const
{
	Uint32 result;
	return IndexOfLast( wantedCString, result, startIndex );
}

//////////////////////////////////////////////////////////////////////////
// check from right is 'wantedString' exists and put result into 'foundAtIndex' or return false
const Bool String::ContainsFromRight( const String& wantedString, const Uint32 startIndex ) const
{
	Uint32 result;
	return IndexOfLast( wantedString.AsChar(), result, startIndex );
}

//////////////////////////////////////////////////////////////////////////
// check from right is 'wantedChar' (no case sensitive) exists and put result into 'foundAtIndex' or return false
const Bool String::ContainsFromRightNoCase( const char wantedChar, const Uint32 startIndex ) const
{
	Uint32 result;
	return IndexOfLastNoCase( wantedChar, result, startIndex );
}

//////////////////////////////////////////////////////////////////////////
// check from right is 'wantedString' (no case sensitive) exists and put result into 'foundAtIndex' or return false
const Bool String::ContainsFromRightNoCase( const char* wantedCString, const Uint32 startIndex ) const
{
	Uint32 result;
	return IndexOfLastNoCase( wantedCString, result, startIndex );
}

//////////////////////////////////////////////////////////////////////////
// check from right is 'wantedString' (no case sensitive) exists and put result into 'foundAtIndex' or return false
const Bool String::ContainsFromRightNoCase( const String& wantedString, const Uint32 startIndex ) const
{
	Uint32 result;
	return IndexOfLastNoCase( wantedString.AsChar(), result, startIndex );
}

//////////////////////////////////////////////////////////////////////////
// search for 'wantedChar' from 'startIndex' and put result into 'foundAtIndex' or return false
const Bool String::IndexOf( const char wantedChar, Uint32& foundAtIndex, const Uint32 startIndex ) const
{
	return _IndexOf( wantedChar, foundAtIndex, startIndex, false );
}

//////////////////////////////////////////////////////////////////////////
// search for 'wantedCString' from 'startIndex' and put result into 'foundAtIndex' or return false
const Bool String::IndexOf( const char* wantedCString, Uint32& foundAtIndex, const Uint32 startIndex ) const
{
	return _IndexOf( wantedCString, foundAtIndex, startIndex, false );
}

//////////////////////////////////////////////////////////////////////////
// search for 'wantedString' and put result into 'foundAtIndex' or return false
const Bool String::IndexOf( const String& wantedString, Uint32& foundAtIndex, const Uint32 startIndex ) const
{
	return _IndexOf( wantedString.AsChar(), foundAtIndex, startIndex, false );
}

//////////////////////////////////////////////////////////////////////////
// search for 'wantedChar' (no case sensitive) from 'startIndex' and put result into 'foundAtIndex' or return false
const Bool String::IndexOfNoCase( const char wantedChar, Uint32& foundAtIndex, const Uint32 startIndex ) const
{
	return _IndexOf( wantedChar, foundAtIndex, startIndex, true );
}

//////////////////////////////////////////////////////////////////////////
// search for 'wantedCString' (no case sensitive) from 'startIndex' and put result into 'foundAtIndex' or return false
const Bool String::IndexOfNoCase( const char* wantedCString, Uint32& foundAtIndex, const Uint32 startIndex ) const
{
	return _IndexOf( wantedCString, foundAtIndex, startIndex, true );
}

//////////////////////////////////////////////////////////////////////////
// search for 'wantedString' (no case sensitive) and put result into 'foundAtIndex' or return false
const Bool String::IndexOfNoCase( const String& wantedString, Uint32& foundAtIndex, const Uint32 startIndex ) const
{
	return _IndexOf( wantedString.AsChar(), foundAtIndex, startIndex, true );
}

//////////////////////////////////////////////////////////////////////////
// search for 'wantedChar' from 'startIndex' backwards and put result into 'foundAtIndex' or return false
const Bool String::IndexOfLast( const char wantedChar, Uint32& foundAtIndex, const Uint32 startIndex ) const
{
	return _IndexOfLast( wantedChar, foundAtIndex, startIndex, false );
}

//////////////////////////////////////////////////////////////////////////
// search for 'wantedCString' from 'startIndex' backwards and put result into 'foundAtIndex' or return false
const Bool String::IndexOfLast( const char* wantedCString, Uint32& foundAtIndex, const Uint32 startIndex ) const
{
	return _IndexOfLast( wantedCString, foundAtIndex, startIndex, false );
}

//////////////////////////////////////////////////////////////////////////
// search for 'wantedString' backwards and put result into 'foundAtIndex' or return false
const Bool String::IndexOfLast( const String& wantedString, Uint32& foundAtIndex, const Uint32 startIndex ) const
{
	return _IndexOfLast( wantedString.AsChar(), foundAtIndex, startIndex, false );
}

//////////////////////////////////////////////////////////////////////////
// search for 'wantedChar' from 'startIndex' backwards and put result into 'foundAtIndex' or return false
const Bool String::IndexOfLastNoCase( const char wantedChar, Uint32& foundAtIndex, const Uint32 startIndex ) const
{
	return _IndexOfLast( wantedChar, foundAtIndex, startIndex, true );
}

//////////////////////////////////////////////////////////////////////////
// search for 'wantedCString' from 'startIndex' backwards and put result into 'foundAtIndex' or return false
const Bool String::IndexOfLastNoCase( const char* wantedCString, Uint32& foundAtIndex, const Uint32 startIndex ) const
{
	return _IndexOfLast( wantedCString, foundAtIndex, startIndex, true );
}

//////////////////////////////////////////////////////////////////////////
// search for 'wantedString' backwards and put result into 'foundAtIndex' or return false
const Bool String::IndexOfLastNoCase( const String& wantedString, Uint32& foundAtIndex, const Uint32 startIndex ) const
{
	return _IndexOfLast( wantedString.AsChar(), foundAtIndex, startIndex, true );
}

//////////////////////////////////////////////////////////////////////////
// todo:
String red::operator+( const String& str1, const String& str2 )
{
	String result;
	result.Reserve( str1.Length() + str2.Length() );
	result = str1;
	result += str2;

	return result;
}

//////////////////////////////////////////////////////////////////////////
// less than operator
Bool red::operator<( const String& lhs, const String& rhs )
{

	if ( lhs.Data() && rhs.Data() )
	{
		return red::Strcmp( lhs.AsChar(), rhs.AsChar() ) < 0;
	}

	if ( !lhs.Data() && rhs.Data() )
	{
		return true;
	}

	return false;
}

namespace red
{
const Bool StringDebugger::IsDynamic( const String& str )
{
	return str._IsDynamic();
}

const Bool StringDebugger::IsInternal( const String& str )
{
	return str._IsInternal();
}

const Bool StringDebugger::IsExternal( const String& str )
{
	return str._IsExternal();
}

const Bool StringDebugger::IsValid(const String& str)
{
	return str._IsValid();
}

const Bool StringDebugger::IsAscii( const String& str )
{
	const Uint32 len = str.Length();
	for ( Uint32 i = 0; i < len; ++i )
	{
		if ( str[i] > 127 )
		{
			return false;
		}
	}
	return true;
}

String::iterator begin( String& string ) { return string.Begin(); }
String::iterator end( String& string ) { return string.End(); }
String::const_iterator begin( const String& string ) { return string.Begin(); }
String::const_iterator end( const String& string ) { return string.End(); }
String::const_iterator cbegin( const String& string ) { return string.Begin(); }
}

namespace StringHelpers
{
	Int32 StrchrR( const red::String& str, char ch )
	{
		Uint32 index = 0;
		if ( !str.IndexOfLast( ch, index ) )
			return -1;

		return (Int32)index;
	}

	Bool WildcardMatch( const char* str, const char* match )
	{
		while (*match)
		{
			if (!*str)
				return *match == '*' && !*(match + 1);

			if (*match == '*')
				return WildcardMatch(str, match + 1) || WildcardMatch(str + 1, match);

			if ( !( *match == '?' || toupper(*str) == toupper(*match) ) )
				return false;

			++str;
			++match;
		}
		return !*str;
	}
}
