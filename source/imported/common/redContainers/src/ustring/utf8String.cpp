#include "build.h"

#include "ustring/utf8String.h"
#include "ustring/utf16String.h"

namespace
{
	static_assert( sizeof(UniChar) == sizeof(red::CodeUnit16), "Size of Char not match size of red::text::CodeUnit16" );
	RED_INLINE const red::CodeUnit16* CharToCU16( const UniChar* str ) { return reinterpret_cast< const red::CodeUnit16* >( str ); }
	//RED_INLINE const UniChar* CU16ToChar( const red::CodeUnit16* str ) { return reinterpret_cast< const UniChar* >( str ); }
}
// =================================================================================================
namespace red {
// =================================================================================================

/**
Ctor.

\param src UTF-16 string.

See docs for Utf8String::Utf8String(const CodeUnit16*, Uint32) for more info on size of UTF-8 string
vs size of UTF-16 string.
*/
Utf8String::Utf8String(const Utf16String& src)
: strSize(0),
  capacity(0),
  str(0)
{
    if(RED_LIKELY(!src.Empty())) {
        AllocateStorage(src.Length() * 3); // we need at most 3x more UTF-8 code units
        Utf16ToUtf8(str, capacity, &strSize, src.GetPtr(), src.Length(), UnicodeChars::replacementCharacter, 0);
        str[strSize] = 0; // null terminate
    }
}

// =================================================================================================

/**
Ctor.

\param src Null (U+0000) terminated UTF-16 string. If 0 then empty string will be produced.

See docs for Utf8String::Utf8String(const CodeUnit16*, Uint32) for more info on size of UTF-8 string
vs size of UTF-16 string.
*/
Utf8String::Utf8String(const UniChar* src)
: strSize(0),
  capacity(0),
  str(0)
{
    if(RED_LIKELY(src != 0)) {
        Uint32 srcNumCodeUnits = (Uint32)red::Strlen(src);
        if(RED_LIKELY(srcNumCodeUnits > 0)) {
            AllocateStorage(srcNumCodeUnits * 3); // we need at most 3x more UTF-8 code units
            Utf16ToUtf8(str, capacity, &strSize, CharToCU16( src ), srcNumCodeUnits, UnicodeChars::replacementCharacter, 0);
            str[strSize] = 0; // null terminate
        }
    }
}

// =================================================================================================

/**
Ctor.

\param src UTF-16 string. May contain null (U+0000) characters. If 0 then empty string will be produced.

\param numCodeUnits Number of code units in src. If 0 then empty string will be produced.

If UTF-16 string has x 16-bit code units then UTF-8 string will have at most 3x 8-bit code units.
In terms of bytes this means that UTF-8 will have at most 1.5x more bytes than UTF-16 string.

For characters in BMP we need 1 UTF-16 code unit and:
  * 1 UTF-8 code unit for the first 128 characters (US-ASCII)
  * 2 UTF-8 code units for the next 1920 characters (this includes Latin letters with diacritics and
    characters from the Greek, Cyrillic, Coptic, Armenian, Hebrew, Arabic, Syriac and Tana alphabets
  * 3 UTF-8 code units for the rest of BMP (which contains virtually all characters in common use)

For characters in supplementary planes we need 2 UTF-16 code units and 4 UTF-8 code units. Here we
have less common CJK characters and various historic scripts and mathematical symbols.
*/
Utf8String::Utf8String(const UniChar* src, Uint32 numCodeUnits)
	: strSize(0),
	capacity(0),
	str(0)
{
	if(RED_LIKELY(src && numCodeUnits > 0)) {
		AllocateStorage(numCodeUnits * 3); // we need at most 3x more UTF-8 code units
		Utf16ToUtf8(str, capacity, &strSize, CharToCU16( src ), numCodeUnits, UnicodeChars::replacementCharacter, 0);
		str[strSize] = 0; // null terminate
	}
}

// =================================================================================================

/**
Assignment operator.

If rhs is empty string then Clear() will be called for this Utf8String, i.e. storage will not
be deallocated.
*/
Utf8String& Utf8String::operator=(const Utf8String& rhs)
{
    // Self assignment is properly handled by the implementation.

    // Assigning non-empty string.
    if(RED_LIKELY(rhs.Length() > 0)) {
        // if capacity of this string is big enough then just copy rhs
        if(Capacity() >= rhs.Length()) {
            // Using memmove makes self assignment ok.
            std::memmove(str, rhs.str, rhs.Length());
        }
        // capacity of this string is too small
        else {
            // reallocate storage
            DelStorage();
            AllocateStorage(rhs.Length());
            // copy rhs contents
            std::memcpy(str, rhs.str, rhs.Length());
        }

        SetSize(rhs.Length());
    }
    // Assigning empty string.
    else
        Clear();

    return *this;
}

// =================================================================================================

/**
*/
Utf8String& Utf8String::operator+=(const Utf8String& rhs)
{
	return Append( rhs );
}

// =================================================================================================

/**
*/
Utf8String Utf8String::operator+(const Utf8String& rhs) const
{
	Utf8String result = *this;
	result += rhs;
	return result;
}

// =================================================================================================

/**
Assigns UTF-16 string.

\param src Null (U+0000) terminated UTF-16 string. If 0 then empty string will be produced.

See docs for Utf8String::Utf8String(const CodeUnit16*, Uint32) for more info on size of UTF-8 string
vs size of UTF-16 string.
*/
void Utf8String::FromUtf16(const CodeUnit16* src)
{
    if(RED_LIKELY(src != 0))
        FromUtf16(src, UStrLen(src));
}

// =================================================================================================

/**
Assigns UTF-16 string.

\param src UTF-16 string. May contain null (U+0000) characters. If 0 then empty string will be produced.

\param numCodeUnits Number of UTF-16 code units in src string. If 0 then empty string will be produced.

See docs for Utf8String::Utf8String(const CodeUnit16*, Uint32) for more info on size of UTF-8 string
vs size of UTF-16 string.
*/
void Utf8String::FromUtf16(const CodeUnit16* src, Uint32 numCodeUnits)
{
    if(RED_LIKELY(src && numCodeUnits > 0)) {
        // Allocate more space if necessary (we need at most 3x more UTF-8 code units - for more
        // info see Utf8String::Utf8String(const CodeUnit16*, Uint32) docs).
        if(capacity < numCodeUnits * 3) {
            DelStorage();
            AllocateStorage(numCodeUnits * 3);
        }

        Utf16ToUtf8(str, capacity, &strSize, src, numCodeUnits, UnicodeChars::replacementCharacter, 0);
        str[strSize] = 0; // null terminate
    }
    else
        Clear();
}

// =================================================================================================

/**
Assigns UTF-16 string.

\param src UTF-16 string.
*/
void Utf8String::FromUtf16(const Utf16String& src)
{
    FromUtf16(src.GetPtr(), src.Length());
}

// =================================================================================================

/**
*/
Utf8String& Utf8String::Append(const Utf8String& rhs)
{
	const Uint32 oldStrSize = Length();
	const Uint32 otherStrSize = rhs.Length();
	const Uint32 newStrSize = oldStrSize + otherStrSize;
	Reserve( newStrSize );
	std::memcpy(str + oldStrSize, rhs.str, otherStrSize);
	SetSize( newStrSize );
	return *this;
}

// =================================================================================================
} // namespace red
// =================================================================================================
