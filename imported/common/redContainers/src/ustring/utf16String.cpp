#include "build.h"

#include "ustring/utf16String.h"
#include "ustring/utf8String.h"
#include <algorithm>

#define IS_WHITESPACE(c) ((c)==' ' || (c)=='\t' || (c)=='\r' || (c)=='\n')

namespace
{
	static_assert( sizeof(UniChar) == sizeof(red::CodeUnit16), "Size of Char not match size of red::text::CodeUnit16" );
	 const red::CodeUnit16* CharToCU16( const UniChar* str ) { return reinterpret_cast< const red::CodeUnit16* >( str ); }
	 const UniChar* CU16ToChar( const red::CodeUnit16* str ) { return reinterpret_cast< const UniChar* >( str ); }
}

// =================================================================================================
namespace red {
// =================================================================================================

/**
Ctor.

\param src UTF-8 string.

See docs for Utf16String::Utf16String(const CodeUnit8*, Uint32) for more info on size of UTF-16
string vs size of UTF-8 string.
*/
Utf16String::Utf16String(const Utf8String& src)
: strSize(0),
  capacity(0),
  str(0)
{
    if(RED_LIKELY(!src.Empty())) {
        AllocateStorage(src.Length()); // we need at most the same number of UTF-16 code units
        Utf8ToUtf16(str, capacity, &strSize, src.GetPtr(), src.Length(), UnicodeChars::replacementCharacter, 0);
        str[strSize] = 0; // null terminate
    }
}

// =================================================================================================

/**
Ctor.


\param src Null (U+0000) terminated UTF-16 string. If 0 then empty string will be produced.
*/
Utf16String::Utf16String( const UniChar* src )
	: strSize(0),
	capacity(0),
	str(0)
{
	if(RED_LIKELY(src != 0)) {
		strSize = UStrLen(CharToCU16(src));

		if(RED_LIKELY(strSize > 0)) {
			AllocateStorage(strSize);
			std::memcpy(str, src, strSize * sizeof(CodeUnit16));
			str[strSize] = 0; // null terminate
		}
	}
}

Utf16String::Utf16String(const UniChar& src)
	: strSize(0),
	capacity(0),
	str(0)
{
	if (RED_LIKELY(src != 0)) {
		strSize = UStrLen(CharToCU16(&src));

		if (RED_LIKELY(strSize > 0)) {
			AllocateStorage(strSize);
			std::memcpy(str, &src, strSize * sizeof(CodeUnit16));
			str[strSize] = 0; // null terminate
		}
	}
}

// =================================================================================================

/**
Assignment operator.

If rhs is empty string then Clear() will be called for this Utf16String, i.e. storage will not
be deallocated.
*/
Utf16String& Utf16String::operator=(const Utf16String& rhs)
{
    // Self assignment is properly handled by the implementation.

    // Assigning non-empty string.
    if(RED_LIKELY(rhs.Length() > 0)) {
        // if capacity of this string is big enough then just copy rhs
        if(Capacity() >= rhs.Length()) {
            // Using memmove makes self assignment ok.
            std::memmove(str, rhs.str, rhs.Length() * sizeof(CodeUnit16));
        }
        // capacity of this string is too small
        else {
            // reallocate storage
            DelStorage();
            AllocateStorage(rhs.Length());
            // copy rhs contents
            std::memcpy(str, rhs.str, rhs.Length() * sizeof(CodeUnit16));
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
Assigns UTF-8 string.

\param src Null terminated UTF-8 string. If 0 then empty string will be produced.

See docs for Utf16String::Utf16String(const CodeUnit8*, Uint32) for more info on size of UTF-16
string vs size of UTF-8 string.
*/
void Utf16String::FromUtf8(const CodeUnit8* src)
{
    if(RED_LIKELY(src != 0))
        FromUtf8(src, (Uint32)red::Strlen(reinterpret_cast<const char*>(src)));
}

// =================================================================================================

/**
Assign UTF-8 string.

\param src UTF-8 string. May contain null characters. If 0 then empty string will be produced.

\param numCodeUnits Number of UTF-8 code units in src. If 0 then empty string will be produced.

See docs for Utf16String::Utf16String(const CodeUnit8*, Uint32) for more info on size of UTF-16
string vs size of UTF-8 string.
*/
void Utf16String::FromUtf8(const CodeUnit8* src, Uint32 numCodeUnits)
{
    if(RED_LIKELY(src && numCodeUnits > 0)) {
        // Allocate more space if necessary (we need at most the same number of UTF-16 code
        // units - for more info see Utf16String::Utf16String(const CodeUnit8*, Uint32) docs).
        if(capacity < numCodeUnits) {
            DelStorage();
            AllocateStorage(numCodeUnits);
        }

        Utf8ToUtf16(str, capacity, &strSize, src, numCodeUnits, UnicodeChars::replacementCharacter, 0);
        str[strSize] = 0; // null terminate
    }
    else
        Clear();
}

// =================================================================================================

/**
Assigns UTF-8 string.

\param src UTF-8 string.

See docs for Utf16String::Utf16String(const CodeUnit8*, Uint32) for more info on size of UTF-16
string vs size of UTF-8 string.
*/
void Utf16String::FromUtf8(const Utf8String& src)
{
    FromUtf8(src.GetPtr(), src.Length());
}

// =================================================================================================

/**
Returns code unit offset corresponding to code point offset.

\return Code unit offset corresponding to code point offset or -1 if code point offset is out of range.
*/
Uint32 Utf16String::GetCuOffset(Uint32 cpOffset)
{
    Uint32 cuOff = 0;
    Uint32 cpOff = 0;

    for( ; cuOff < Length() && cpOff != cpOffset; ) {
        ItCP(cuOff);
        ++cpOff;
    }

    if(cuOff < Length()) // this implies that cpOff == cpOffset stopped the loop
        return cuOff;

    // code point offset out of range
    return static_cast<Uint32>(-1);
}

// =================================================================================================

/**
Inserts one code point into this string at specified code point offset.

\param cpOffset Code point offset at which to insert ins. If code point offset is -1 or is out of
range then the function will append specified code point to the end of string.

\param ins Code point to be inserted. Must be Unicode code point (U+0000..U+10ffff), otherwise result
is undefined since the function doesn't check this.

\return True - insertion succeeded, false - cpOffset is out of range.

It's ok to call Insert() for empty Utf16String.
*/
Bool Utf16String::Insert(Uint32 cpOffset, CodePoint ins)
{
    Uint32 cuOff = -1;

    if(cpOffset != -1)
        cuOff = GetCuOffset(cpOffset);

    if(cuOff == -1) {
        Append(ins);
        return true;
    }

    Uint32 cuCount = GetNumCodeUnits16(ins);

    // make place for ins
    Reserve(Length() + cuCount);
    std::memmove(GetPtr() + cuOff + cuCount, GetPtr() + cuOff, (Length() - cuOff) * sizeof(CodeUnit16));

    // insert ins
    if(CodePointIsBMP(ins))
        str[cuOff] = static_cast<CodeUnit16>(ins);
    else if(CodePointIsSupplementary(ins)) {
        str[cuOff] = GetLeadSurrogate(ins);
        str[cuOff + 1] = GetTrailSurrogate(ins);
    }

    SetSize(Length() + cuCount);

    return true;
}

// =================================================================================================

/**
Replaces part of this string with another string.

It's ok to call Replace() for empty Utf16String.

Preconditions:
1. remCuOff + remSize <= Length() (checked only in debug)
2. rep != 0 || (rep == 0 && repSize == 0) (checked only in debug)

Loose notes:
1. When replacing whole string with empty string the result will be empty string with the same
   capacity as before Replace(). Note this is different from non-member Replace() which returns
   empty string with Capacity() == 0 in such case.

\param remCuOff Starting CodeUnit16 offset of part that is to be replaced, one-past-last CodeUnit16 offset is ok.
\param remSize Size (in code units) of part that is to be replaced, may be 0.
\param rep Pointer to replacement string. May be 0 only if repSize is also 0.
\param repSize Size (in code units) of replacement string, may be 0.
*/
void Utf16String::Replace(Uint32 remCuOff, Uint32 remSize, const CodeUnit16* rep, Uint32 repSize)
{
    RED_ASSERT(remCuOff + remSize <= Length());
    RED_ASSERT(rep != 0 || (rep == 0 && repSize == 0));

    Uint32 newSize = Length() - remSize + repSize;

    if(RED_LIKELY(newSize > 0)) {
        // Capacity is sufficient to perform replacement without reallocation.
        if(newSize <= Capacity()) {
            // make place for rep by moving part of original string
            std::memmove(GetPtr() + remCuOff + repSize, GetPtr() + remCuOff + remSize, (Length() - remCuOff - remSize) * sizeof(CodeUnit16));
            // copy rep
            std::memcpy(GetPtr() + remCuOff, rep, repSize * sizeof(CodeUnit16));

            // set size and null terminate
            strSize = newSize;
            GetPtr()[strSize] = 0;
        }
        // Capacity is to small, reallocation is needed.
        else {
            Utf16String r(red::utf16::Replace(*this, remCuOff, remSize, rep, repSize));
            r.Swap(*this);
        }
    }
    else
        Clear();
}

// =================================================================================================

/**
 Removes all white space from the both string sides

 It's ok to call Trim() for empty Utf16String.
 */

void Utf16String::Trim()
{
	TrimLeft();
	TrimRight();
}

// =================================================================================================

/**
Removes all white space from the string beginning

It's ok to call TrimLeft() for empty Utf16String.
*/

void Utf16String::TrimLeft()
{
	Uint32 cuIndex = 0;
	Uint32 cuNonWhiteSpaceIndex = 0;

	CodeUnit16 c = 0;
	for( ; cuIndex < Length(); ) {
		c = str[cuIndex];		
		if( IS_WHITESPACE( c ) == false )
		{
			cuNonWhiteSpaceIndex = cuIndex;
			break;
		}
		++cuIndex;		
	}

	if( cuNonWhiteSpaceIndex != 0 )
	{
		Replace( 0, cuNonWhiteSpaceIndex, nullptr, 0 );
	}
}

// =================================================================================================

/**
Removes all white space from the string ending

It's ok to call TrimRight() for empty Utf16String.
*/

void Utf16String::TrimRight()
{
	Uint32 size = Length();
	if( size == 0 )
		return;

	Uint32 cuIndex = size;
	Uint32 cuNonWhiteSpaceIndex = size;

	CodeUnit16 c = 0;
	do {
		--cuIndex;
		c = str[cuIndex];		
		if( IS_WHITESPACE( c ) == false )
		{
			cuNonWhiteSpaceIndex = cuIndex;
			break;
		}				
	} while ( cuIndex > 0 );

	if( cuNonWhiteSpaceIndex != size )
	{
		Replace( cuNonWhiteSpaceIndex + 1, size - (cuNonWhiteSpaceIndex + 1), nullptr, 0 );
	}
}

// =================================================================================================

/**
	//http://www.cogs.susx.ac.uk/courses/dats/notes/html/node114.html

	\return simple hash
*/
Uint32 Utf16String::CalcHash() const
{
	Uint32 hash = 0;
	Uint32 length = strSize;
	for ( Uint32 i = 0; (i < length) && str[ i ]; ++i )
	{
		hash = str[ i ] + hash * 31;
	}

	return hash;
}

// =================================================================================================

/**
String encoding method - name changed to mislead crackers

\param crc is hash used to encode string.
\return trimmed version of str

It's ok to call Validate() for empty Utf16String.
*/
void Utf16String::Validate( Uint16 crc )
{
	Uint32 size = Length();
	if( size == 0 )
		return;

	Uint32 cuIndex = 0;
	for( ; cuIndex < Length(); )
 	{
 		str[cuIndex] ^= crc * (Uint16) size;
 		crc = crc << 1 | crc >> 15;
 		++cuIndex;
 	}
}

// =================================================================================================

/**
Return RedEngine specific string raw pointer
*/
const UniChar* Utf16String::AsChar() const
{
	return CU16ToChar( str );
}

// =================================================================================================

/**
Performs only bitwise comparison

/return Int8 1 for more, 0 for equal, -1 for less
*/

Int8 Utf16String::Compare( const Int32 start, const Int32 length, const CodeUnit16* srcChars, Int32 srcStart, Int32 srcLength ) const
{
	if( srcChars == NULL ) {
		// treat const UChar *srcChars==NULL as an empty string
		return length == 0 ? 0 : 1;
	}

	// get the correct pointer
	const CodeUnit16 *chars = str;

	chars += start;
	srcChars += srcStart;

	Int32 minLength;
	Int8 lengthResult;

	// get the srcLength if necessary
	if(srcLength < 0) {
		srcLength = UStrLen( srcChars + srcStart );
	}

	// are we comparing different lengths?
	if(length != srcLength) {
		if(length < srcLength) {
			minLength = length;
			lengthResult = -1;
		} else {
			minLength = srcLength;
			lengthResult = 1;
		}
	} else {
		minLength = length;
		lengthResult = 0;
	}

	/*
	* we need to take care not to truncate the result -
	* one way to do this is to right-shift the value to
	* move the sign bit into the lower 8 bits and making sure that this
	* does not become 0 itself
	*/

	if(minLength > 0 && chars != srcChars) {
		Int32 result;

		// little-endian: compare CodeUnit16 units
		do {
			result = ((Int32)*(chars++) - (Int32)*(srcChars++));
			if(result != 0) {
				return (result >> 15 | 1);
			}
		} while(--minLength > 0);
	}
	return lengthResult;
}


void Utf16String::Remove(Uint32 remStart, Uint32 remEnd )
{
	RED_ASSERT( remStart <= strSize, "Cannot remove characters past the end of the string" );
	RED_ASSERT( remEnd <= strSize, "Cannot remove characters past the end of the string" );
	RED_ASSERT( remStart <= remEnd, "Wrong order of indexes" );

	Uint32 destIndex = remStart;
	Uint32 srcIndex = remEnd;
	while ( srcIndex <= strSize ) // copies the null terminator as well
	{
		str[ destIndex ] = str[ srcIndex ];
		++destIndex;
		++srcIndex;
	}

	strSize = destIndex - 1; // omit null terminator
}

// =================================================================================================
// implementations of member functions
// =================================================================================================

/**
Ctor - creates Utf16String without initializing string's contents.

\param minCapacity Min capacity of string, in code units. If 0 then no storage will be allocated.
*/
 Utf16String::Utf16String(Uint32 minCapacity /* = 0 */)
: strSize(0),
  capacity(0),
  str(0)
{
    if(minCapacity > 0) {
        AllocateStorage(minCapacity);
        str[0] = 0; // null terminate
    }
}

// =================================================================================================

/**
Ctor.

Stick to US-ASCII when using char literals.
Of course, string literals can also be used as src argument. However, there is something you have to
be aware of - the compiler's and the runtime character set's encodings are not specified by the C/C++
standard. Therefore, to be on the safe side, you should restrict yourself to using US-ASCII string
literals only.

Watch for mislabeled Windows-1252 strings!
Very commonly strings that are said to be ISO 8859-1 or Latin-1 are really Windows-1252. Windows-1252 is very
similar to ISO 8859-1 - it differs only in [128, 159] range. This excerpt from Windows-1252 article on Wikipedia
provides more info:
"It is very common to mislabel Windows-1252 text with the charset label ISO-8859-1. A common result was that all
the quotes and apostrophes (produced by "smart quotes" in Microsoft software) were replaced with question marks
or boxes on non-Windows operating systems, making text difficult to read. Most modern web browsers and e-mail
clients treat the MIME charset ISO-8859-1 as Windows-1252 in order to accommodate such mislabeling. This is now
standard behavior in the draft HTML 5 specification, which requires that documents advertised as ISO-8859-1
actually be parsed with the Windows-1252 encoding."

\param src Null terminated US-ASCII or ISO 8859-1 (aka Latin-1) string. If 0 then empty string will be produced.
*/
 Utf16String::Utf16String(const char* src)
: strSize(0),
  capacity(0),
  str(0)
{
    if(RED_LIKELY(src != 0)) {
        strSize = (Uint32)red::Strlen(src);

        if(RED_LIKELY(strSize > 0)) {
            AllocateStorage(strSize);
            for(unsigned int i = 0; i < strSize; ++i)
                str[i] = src[i];
            str[strSize] = 0; // null terminate
        }
    }
}

// =================================================================================================

/**
Ctor.

See Utf16String::Utf16String(const char*) docs for info on using character literals and warning about
mislabeled Windows-1252 strings.

\param src US-ASCII or ISO 8859-1 (aka Latin-1) string. If 0 then empty string will be produced.
May contain null characters.

\param numBytes Number of bytes in src. If 0 then empty string will be produced.
*/
 Utf16String::Utf16String(const char* src, Uint32 numBytes)
: strSize(0),
  capacity(0),
  str(0)
{
    if(RED_LIKELY(src && numBytes > 0)) {
        AllocateStorage(numBytes);
        for(unsigned int i = 0; i < numBytes; ++i)
            str[i] = src[i];
        strSize = numBytes;
        str[strSize] = 0; // null terminate
    }
}

// =================================================================================================

/**
Ctor.

\param src UTF-16 string. May contain nulls (U+0000). If 0 then empty string will be produced.

\param numCodeUnits Number of code units in src. If 0 then empty string will be produced.
*/
 Utf16String::Utf16String( const UniChar* src, Uint32 numCodeUnits )
	: strSize(0),
	capacity(0),
	str(0)
{
	if(RED_LIKELY(src && numCodeUnits > 0)) {
		AllocateStorage(numCodeUnits);
		std::memcpy(str, src, numCodeUnits * sizeof(CodeUnit16));
		strSize = numCodeUnits;
		str[strSize] = 0; // null terminate
	}
}

// =================================================================================================

/**
Ctor.

\param src Null (U+0000) terminated UTF-16 string. If 0 then empty string will be produced.
*/
 Utf16String::Utf16String(const CodeUnit16* src)
: strSize(0),
  capacity(0),
  str(0)
{
    if(RED_LIKELY(src != 0)) {
        strSize = UStrLen(src);

        if(RED_LIKELY(strSize > 0)) {
            AllocateStorage(strSize);
            std::memcpy(str, src, strSize * sizeof(CodeUnit16));
            str[strSize] = 0; // null terminate
        }
    }
}

// =================================================================================================

/**
Ctor.

\param src UTF-16 string. May contain nulls (U+0000). If 0 then empty string will be produced.

\param numCodeUnits Number of code units in src. If 0 then empty string will be produced.
*/
 Utf16String::Utf16String(const CodeUnit16* src, Uint32 numCodeUnits)
: strSize(0),
  capacity(0),
  str(0)
{
    if(RED_LIKELY(src && numCodeUnits > 0)) {
        AllocateStorage(numCodeUnits);
        std::memcpy(str, src, numCodeUnits * sizeof(CodeUnit16));
        strSize = numCodeUnits;
        str[strSize] = 0; // null terminate
    }
}

// =================================================================================================

/**
Ctor.

\param src Null terminated UTF-8 string. If 0 then empty string will be produced.

See docs for Utf16String::Utf16String(const CodeUnit8*, Uint32) for more info on size of UTF-16
string vs size of UTF-8 string.
*/
 Utf16String::Utf16String(const CodeUnit8* src)
: strSize(0),
  capacity(0),
  str(0)
{
    if(RED_LIKELY(src != 0)) {
        Uint32 srcNumCodeUnits = (Uint32)red::Strlen(reinterpret_cast<const char*>(src));
        if(RED_LIKELY(srcNumCodeUnits > 0)) {
            AllocateStorage(srcNumCodeUnits); // we need at most the same number of UTF-16 code units
            Utf8ToUtf16(str, capacity, &strSize, src, srcNumCodeUnits, UnicodeChars::replacementCharacter, 0);
            str[strSize] = 0; // null terminate
        }
    }
}

// =================================================================================================

/**
Ctor.

\param src UTF-8 string. May contain null characters. If 0 then empty string will be produced.

\param numCodeUnits Number of UTF-8 code units in src. If 0 then empty string will be produced.

UTF-16 string has at most the same number of code units as UTF-8 string. In terms of bytes this
means that UTF-16 string will have at most 2x more bytes than UTF-8 string.

For characters in BMP we need 1 UTF-16 code unit and:
  * 1 UTF-8 code unit for the first 128 characters (US-ASCII)
  * 2 UTF-8 code units for the next 1920 characters (this includes Latin letters with diacritics and
    characters from the Greek, Cyrillic, Coptic, Armenian, Hebrew, Arabic, Syriac and Tana alphabets
  * 3 UTF-8 code units for the rest of BMP (which contains virtually all characters in common use)

For characters in supplementary planes we need 2 UTF-16 code units and 4 UTF-8 code units. Here we
have less common CJK characters and various historic scripts and mathematical symbols.
*/
 Utf16String::Utf16String(const CodeUnit8* src, Uint32 numCodeUnits)
: strSize(0),
  capacity(0),
  str(0)
{
    if(RED_LIKELY(src && numCodeUnits > 0)) {
        AllocateStorage(numCodeUnits); // we need at most the same number of UTF-16 code units
        Utf8ToUtf16(str, capacity, &strSize, src, numCodeUnits, UnicodeChars::replacementCharacter, 0);
        str[strSize] = 0; // null terminate
    }
}

// =================================================================================================

/**
Cctor - creates string copy.

Note that copy may have different (smaller/bigger) capacity than original string - this depends on
different factors taken into account by storage allocator.

\param other Utf16String that is to be copied.
*/
 Utf16String::Utf16String(const Utf16String& other)
: strSize(other.strSize),
  capacity(0),
  str(0)
{
    // Creating from non-empty string.
    if(RED_LIKELY(strSize > 0)) {
        AllocateStorage(strSize);
        std::memcpy(str, other.str, other.strSize * sizeof(CodeUnit16));
        str[strSize] = 0; // null terminate
    }
}

// =================================================================================================

/**
Move constructor
*/

 Utf16String::Utf16String( Utf16String&& src )
	: strSize( src.strSize )
	, capacity( src.capacity )
	, str( src.str )
{
	src.strSize = 0;
	src.capacity = 0;
	src.str = nullptr;
}

// =================================================================================================

/**
Dtor.
*/
 Utf16String::~Utf16String()
{
    DelStorage();
}


// =================================================================================================

/**
Move constructor
*/
 Utf16String& Utf16String::operator=( Utf16String&& rhs )
{
	Utf16String( std::move( rhs ) ).Swap( *this );
	return *this;
}


// =================================================================================================

/**
Returns reference to 16-bit code unit at specified index.

\param i Index of code unit that is to be returned. It has to satisfy condition: i < Length().
Otherwise, behavior is undefined since operator[] doesn't check this condition (except for
assertion in debug builds).
*/
 CodeUnit16& Utf16String::operator[](Uint32 i)
{
    RED_ASSERT(i < Length());
    return str[i];
}

// =================================================================================================

/**
Returns const reference to 16-bit code unit at specified index.

\param i Index of code unit that is to be returned. It has to satisfy condition: i < Length().
Otherwise, behavior is undefined since operator[] doesn't check this condition (except for
assertion in debug builds).
*/
 const CodeUnit16& Utf16String::operator[](Uint32 i) const
{
    RED_ASSERT(i < Length());
    return str[i];
}

// =================================================================================================

/**
Gets code point at specified offset.

\param i Offset in string. Must satisfy condition: i < Length(). Otherwise behavior is undefined
as GetCP() doesn't check this condition. Offset may point to code unit that is lead or trail
surrogate - in this case, the other surrogate will be read and supplementary code point will
be returned. If it points to unpaired surrogate then that surrogate is returned as code point.

\return Code point at specified offset.
*/
 CodePoint Utf16String::GetCP(Uint32 i) const
{
    return GetCodePointAtSafer(GetPtr(), 0, i, Length());
}

// =================================================================================================

/**
Iterates over code points in forward direction.

\param i (inout) Offset in string. Must satisfy condition: i < Length(). Otherwise behavior is
undefined as ItCP() doesn't check this condition. If offset points to code unit that is lead
surrogate, then the function will also read trail surrogate and return supplementary code point.
If it points to trail surrogate or unpaired lead surrogate then that surrogate will be returned
as code point.

\return Code point at i before incrementation.

Code point at specified offset is returned and then the offset is advanced to next code point.

Example - iterating over all code points in string:

    for(Uint32 i = 0; i < someString.Length(); ) {
        CodePoint cp = someString.ItCP(i);
        // ...
    }
*/
 CodePoint Utf16String::ItCP(Uint32& i) const
{
    return GetCodePointAtAndAdvanceSafer(GetPtr(), i, Length());
}

// =================================================================================================

/**
Iterates over code points in backward direction.

\param i (inout) String offset. Must satisfy condition: i > 0 && i <= Length(). Also, i must point
to code point boundary. Otherwise behavior is undefined as RevItCP() doesn't check this conditions.
If i points behind code unit that is a trail surrogate, the function will also read lead surrogate
and return supplementary code point. If i points behind unpaired trail or lead surrogate then that
surrogate will be returned as code point.

\return Code point at i after it was decremented.

First the offset is moved backwards to previous code point and then this code points is returned.
This means that offset should be set to one-past-last code unit (use Length() for this) if we
want to iterate over all code points in string.

Example - iterating over all code point in string in backward direction:

    for(Uint32 i = somestring.Length(); i > 0; ) {
        CodePoint cp = someString.RevItCP(i);
        // ...
    }
*/
 CodePoint Utf16String::RevItCP(Uint32& i) const
{
    return BackAdvanceAndGetCodePointAtSafer(GetPtr(), 0, i);
}

// =================================================================================================

/**
Swaps this string with another.

Trying to Swap() Utf16String with itself does nothing.
*/
 void Utf16String::Swap(Utf16String& other)
{
    if(RED_LIKELY(this != &other)) {
        unsigned char temp[sizeof(Utf16String)];
        std::memcpy(temp, (const void *)this, sizeof(Utf16String));
        std::memcpy((void *)this, (const void *)&other, sizeof(Utf16String));
        std::memcpy((void *)&other, temp, sizeof(Utf16String));
    }
}

// =================================================================================================

/**
Returns whether string is empty.

String is empty if its size is 0. Note that this doesn't necessarily means that Capacity() == 0.
*/
 Bool Utf16String::Empty() const
{
    return Length() == 0;
}

// =================================================================================================

/**
Returns size of string in code units.

\return Size of string in code units, for empty string it's 0.
*/
 Uint32 Utf16String::Length() const
{
    return strSize;
}

// =================================================================================================

/**
Returns capacity of string in code units.

Capacity is the maximum number of code units that string may grow to without reallocating internal buffer.

Note that the size of internal buffer is (capacity + 1) to accommodate for terminating null char.
The only exception is when capacity is 0 - in that case size of internal buffer is also 0.

\return Capacity of string in code units, it doesn't have to be 0 for empty strings.
*/
 Uint32 Utf16String::Capacity() const
{
    return capacity;
}

// =================================================================================================

/**
Returns pointer to internal buffer with string contents.

The function returns pointer to internal buffer. For empty strings it may be 0 (no storage allocated)
or it may be != 0 (storage was allocated).

The pointer is valid until next operation that possibly modifies the string.

\return Pointer to string storage, it doesn't have to be 0 for empty strings (storage may be allocated).
*/
 const CodeUnit16* Utf16String::GetPtr() const
{
    return str;
}

// =================================================================================================

/**
Returns pointer to internal buffer with string contents.

The function returns pointer to internal buffer. For empty strings it may be 0 (no storage allocated)
or it may be != 0 (storage was allocated).

You can use this pointer to modify string's contents but only in offset range [0, Length()).

The pointer is valid until next operation that possibly modifies the string (of course, modifying the
the string via pointer returned by GetPtr() does not invalidate the pointer).

\return Pointer to string storage, it doesn't have to be 0 for empty strings (storage may be allocated).
*/
 CodeUnit16* Utf16String::GetPtr()
{
    return str;
}

// =================================================================================================

/**
Resizes the string.

If newSize < Length() then string contents is reduced to newSize chars.
If newSize > Length() then string contents is expanded - new chars contain garbage.
If newSize == Length() then nothing happens.

If newSize <= Capacity() then no storage is reallocated, otherwise storage reallocation occurs.

\param newSize New size, in code units.
*/
 void Utf16String::Resize(Uint32 newSize)
{
    // No storage reallocation needed.
    if(newSize <= Capacity()) {
        strSize = newSize;
        // Null terminate string buffer.
        if(Capacity() > 0)
            str[strSize] = 0;
    }
    // Storage reallocation needed.
    else {
        Utf16String s(newSize);                                       // newSize as min capacity
        std::memcpy(s.str, GetPtr(), Length() * sizeof(CodeUnit16)); // copy old contents
        s.strSize = newSize;                                          // set requested size
        s.str[newSize] = 0;                                           // null terminate

        s.Swap(*this);
    }
}

// =================================================================================================

/**
Sets the size of the string.

Often we want to change the size of the string and we know that the new size is less than or equal
to capacity of the string. In this case it's more efficient to use SetSize() than Resize().

Preconditions:
1. newSize <= Capacity() (checked only in debug).

\param newSize New size for the string, in code units. Must be <= Capacity() otherwise behavior
is undefined since SetSize() doesn't check this condition (except for assertion in debug build).
*/
 void Utf16String::SetSize(Uint32 newSize)
{
    RED_ASSERT(newSize <= Capacity());

    strSize = newSize;

    // Null terminate string buffer.
    if(Capacity() > 0)
        str[strSize] = 0;
}

// =================================================================================================

/**
Makes string empty but does not deallocate storage.

After calling Clear(), the string behaves like empty string but storage is not deallocated. This
makes it possible to avoid deallocation + allocation when string is later used (of course, if string
capacity won't be enough, deallocation + allocation will have to be performed).
*/
 void Utf16String::Clear()
{
    strSize = 0;

    // Null terminate string buffer.
    if(Capacity() > 0)
        str[0] = 0;
}

// =================================================================================================

/**
Specifies min capacity of the string.

If current capacity is smaller than minCapacity then storage reallocation occurs. If current
capacity is equal or greater than minCapacity then nothing happens, i.e. storage is not reallocated.
In other words, Reserve() may increase capacity but it will never decrease it.
*/
 void Utf16String::Reserve(Uint32 minCapacity)
{
    if(Capacity() < minCapacity) {
        Utf16String s(minCapacity);
        std::memcpy(s.str, GetPtr(), Length() * sizeof(CodeUnit16));    // copy old contents
        s.strSize = Length();
        s.str[Length()] = 0;                                            // null terminate

        s.Swap(*this);
    }
}

// =================================================================================================

/**
Makes string empty and deletes its storage.
*/
 void Utf16String::DelStorage()
{
	RED_FREE( red::PoolEngine, str );
    strSize = 0;
    capacity = 0;
    str = nullptr;
}

// =================================================================================================

/**
Replaces part of this string with another string.

It's ok to call Replace() for empty Utf16String.

Preconditions:
1. remCuOff + remSize <= Length() (checked only in debug)

Loose notes:
1. When replacing whole string with empty string the result will be empty string with the same
   capacity as before Replace(). Note this is different from non-member Replace() which returns
   empty string with Capacity() == 0 in such case.

\param remCuOff Starting CodeUnit16 offset of part that is to be replaced, one-past-last CodeUnit16 offset is ok.
\param remSize Size (in code units) of part that is to be replaced, may be 0.
\param rep Replacement string, may be empty.
*/
 void Utf16String::Replace(Uint32 remCuOff, Uint32 remSize, const Utf16String& rep)
{
    RED_ASSERT(remCuOff + remSize <= Length());

    Replace(remCuOff, remSize, rep.GetPtr(), rep.Length());
}

// =================================================================================================

/**
Replaces part of this string with another string.

It's ok to call Replace() for empty Utf16String.

Preconditions:
1. remCuOff + remSize <= Length() (checked only in debug)
2. repCuOff <= rep.Length() (checked only in debug)

Loose notes:
1. When replacing whole string with empty string the result will be empty string with the same
   capacity as before Replace(). Note this is different from non-member Replace() which returns
   empty string with Capacity() == 0 in such case.

\param remCuOff Starting CodeUnit16 offset of part that is to be replaced, one-past-last CodeUnit16 offset is ok.
\param remSize Size (in code units) of part that is to be replaced, may be 0.
\param rep Replacement string, may be empty.
\param repCuOff Starting CodeUnit16 offset of the part of rep that is to be used, one-past-last CodeUnit16 offset is ok.
*/
 void Utf16String::Replace(Uint32 remCuOff, Uint32 remSize, const Utf16String& rep, Uint32 repCuOff)
{
    RED_ASSERT(remCuOff + remSize <= Length());
    RED_ASSERT(repCuOff <= rep.Length());

    Replace(remCuOff, remSize, rep.GetPtr() + repCuOff, rep.Length() - repCuOff);
}

// =================================================================================================

/**
Replaces part of this string with another string.

It's ok to call Replace() for empty Utf16String.

Preconditions:
1. remCuOff + remSize <= Length() (checked only in debug)
2. repCuOff + repSize <= rep.Length() (checked only in debug)

Loose notes:
1. When replacing whole string with empty string the result will be empty string with the same
   capacity as before Replace(). Note this is different from non-member Replace() which returns
   empty string with Capacity() == 0 in such case.

\param remCuOff Starting CodeUnit16 offset of part that is to be replaced, one-past-last Codenit16 offset is ok.
\param remSize Size (in code units) of part that is to be replaced, may be 0.
\param rep Replacement string, may be empty.
\param repCuOff Starting CodeUnit16 offset of the part of rep that is to be used, one-past-last CodeUnit16 offset is ok.
\param repSize Size (in code units) of part of rep that is to be used, may be 0.
*/
 void Utf16String::Replace(Uint32 remCuOff, Uint32 remSize, const Utf16String& rep, Uint32 repCuOff, Uint32 repSize)
{
    RED_ASSERT(remCuOff + remSize <= Length());
    RED_ASSERT(repCuOff + repSize <= rep.Length()); // note this allows repCuOff == rep.Length() && repSize == 0

    Replace(remCuOff, remSize, rep.GetPtr() + repCuOff, repSize);
}

// =================================================================================================

/**
Replaces part of this string with another string.

It's ok to call Replace() for empty Utf16String.

Preconditions:
1. remCuOff + remSize <= Length() (checked only in debug)
2. repNullTerminatedString != 0 (checked only in debug)
3. repNullTerminatedString is null terminated (not checked)

Loose notes:
1. When replacing whole string with empty string the result will be empty string with the same
   capacity as before Replace(). Note this is different from non-member Replace() which returns
   empty string with Capacity() == 0 in such case.

\param remCuOff Starting CodeUnit16 offset of part that is to be replaced, one-past-last CodeUnit16 offset is ok.
\param remSize Size (in code units) of part that is to be replaced, may be 0.
\param repNullTerminatedString Pointer to null terminated replacement string, may be empty, must not be 0.
*/
 void Utf16String::Replace(Uint32 remCuOff, Uint32 remSize, const CodeUnit16* repNullTerminatedString)
{
    RED_ASSERT(remCuOff + remSize <= Length());
    RED_ASSERT(repNullTerminatedString);

    Replace(remCuOff, remSize, repNullTerminatedString, UStrLen(repNullTerminatedString));
}

// =================================================================================================

/**
Allocates storage for specified number of code units.

Preconditions:
1. no storage is allocated (checked only in debug)

The function may allocate more storage than requested.
*/
 void Utf16String::AllocateStorage(Uint32 numCodeUnits)
{
    // assert no storage is allocated
    RED_ASSERT(!str);

    // Allocate storage. We ask for numCodeUnits + 1, because we need space for null. This is also
    // why we decrement capacity by one CodeUnit16 after allocation.
	str = static_cast<CodeUnit16*>( RED_ALLOCATE( red::PoolEngine, sizeof( UniChar ) * ( numCodeUnits + 1 ) ) );
	red::Memzero( str, sizeof( UniChar ) * ( numCodeUnits + 1 ) );
	capacity = numCodeUnits;
}

// =================================================================================================

/**
Appends code point to the end of string.

\param cp Code point to append. Must be Unicode code point (U+0000..U+10ffff), otherwise result
is undefined since the function doesn't check this.

\return Reference to this string.
*/
 Utf16String& Utf16String::Append(CodePoint ins)
{
    Uint32 cuCount = GetNumCodeUnits16(ins);
    Reserve(Length() + cuCount);

    if(CodePointIsBMP(ins))
            str[Length()] = static_cast<CodeUnit16>(ins);
    else if(CodePointIsSupplementary(ins)) {
        str[Length()] = GetLeadSurrogate(ins);
        str[Length() + 1] = GetTrailSurrogate(ins);
    }

    SetSize(Length() + cuCount);

    return *this;
}

// =================================================================================================

/**
Appends Utf16String to the end of string.
THIS FUNCTION NOT GUARANTEE CORRECT FINAL Utf16String !!!!
THIS IS ONLY RAW BUFFER CONCATENATION

\param other Utf16String to append.
\param otherOffset The offset from the other string to start appending

\return Reference to this string.
*/
 Utf16String& Utf16String::Append(const Utf16String& other, Uint32 otherOffset)
{
	if ( otherOffset < other.Length() )
	{
		const Uint32 oldStrSize = Length();
		const Uint32 otherStrSize = other.Length() - otherOffset;
		const Uint32 newStrSize = oldStrSize + otherStrSize;

		Reserve( newStrSize );

		std::memcpy(str + oldStrSize, other.str + otherOffset, otherStrSize * sizeof(CodeUnit16));

		SetSize( newStrSize );
	}

	return *this;
}


// =================================================================================================
// implementation - non-member functions
// =================================================================================================

namespace utf16
{

/**
 Removes all white space from the both string sides.

\param searchInStr String to be searched in, may be empty.
\return trimmed version of str
*/

Utf16String Trim( const Utf16String& str )
{
	Utf16String ret( str );
	ret.Trim();
	return ret;
}

// =================================================================================================

/**
Searches the string in forward direction for the first CodeUnit16 that matches any of the characters specified in its string (key).

Preconditions:
1. keyStart != 0, keyEnd != 0, keyEnd > keyStart (checked only in debug).

\param searchInStr String to be searched in, may be empty.
\param keyStart Pointer to first CodeUnit16 in key.
\param keyEnd Pointer to one-past-last CodeUnit16 in key.
\param fromCuOffset CodeUnit16 offset at which search is to begin, may be >= searchInStr.Length().
\return CodeUnit16 offset at which key was found, Utf16String::npos if key wasn't found.
*/
Uint32 Find(const Utf16String& searchInStr, const CodeUnit16* keyStart, const CodeUnit16* keyEnd, Uint32 fromCuOffset /* = 0 */)
{
    if(RED_LIKELY(fromCuOffset < searchInStr.Length())) {
        // Assert key is valid.
        RED_ASSERT(keyStart && keyEnd && keyEnd > keyStart);

        const CodeUnit16* findRangeEnd = searchInStr.GetPtr() + searchInStr.Length();
        const CodeUnit16* foundPtr = std::search(searchInStr.GetPtr() + fromCuOffset, findRangeEnd, keyStart, keyEnd);

        if(foundPtr < findRangeEnd)
            // we have a match
            return (Uint32)(foundPtr - searchInStr.GetPtr());
    }

    // no match
    return Utf16String::npos;
}

// =================================================================================================

/**
Searches string in forward direction for the first occurrence of one of CodeUnit16 in string (key).

Preconditions:
1. keyStart != 0, keyEnd != 0, keyEnd > keyStart (checked only in debug).

\param searchInStr String to be searched in, may be empty.
\param keyStart Pointer to first CodeUnit16 in key.
\param keyEnd Pointer to one-past-last CodeUnit16 in key.
\param fromCuOffset CodeUnit16 offset at which search is to begin, may be >= searchInStr.Length().
\return CodeUnit16 offset at which key was found, Utf16String::npos if key wasn't found.
*/
Uint32 FindFirstOf(const Utf16String& searchInStr, const CodeUnit16* keyStart, const CodeUnit16* keyEnd, Uint32 fromCuOffset )
{
	if(RED_LIKELY(fromCuOffset < searchInStr.Length())) {
		// Assert key is valid.
		RED_ASSERT(keyStart && keyEnd && keyEnd > keyStart);

		const CodeUnit16* findRangeEnd = searchInStr.GetPtr() + searchInStr.Length();
		const CodeUnit16* foundPtr = std::find_first_of(searchInStr.GetPtr() + fromCuOffset, findRangeEnd, keyStart, keyEnd);

		if(foundPtr < findRangeEnd)
			// we have a match
				return (Uint32)(foundPtr - searchInStr.GetPtr());
	}

	// no match
	return Utf16String::npos;
}

// =================================================================================================

/**
Searches string in forward direction for the first occurrence of CodeUnit16.

\param searchInStr String to be searched in, may be empty.
\param cu CodeUnit16 to be located.
\param fromOffset CodeUnit16 offset at which search is to begin, may be >= searchInStr.Length().
\return CodeUnit16 offset at which cu was found, Utf16String::npos if cu wasn't found.
*/
Uint32 Find(const Utf16String& searchInStr, CodeUnit16 cu, Uint32 fromCuOffset /* = 0 */)
{
    if(RED_LIKELY(fromCuOffset < searchInStr.Length())) {
        Uint32 off = fromCuOffset;
        Uint32 offLimit = searchInStr.Length();
        const CodeUnit16* p = searchInStr.GetPtr() + fromCuOffset;

        while(off < offLimit && cu != *p) {
            ++off;
            ++p;
        }

        if(off < offLimit)
            // we have a match
            return off;
    }

    // no match
    return Utf16String::npos;
}

// =================================================================================================

/**
Searches string in forward direction for the first occurrence of CodePoint.

\param searchInStr String to be searched in, may be empty.
\param cp CodePoint to be located.
\param fromCuOffset CodeUnit16 offset at which search is to begin, may be >= searchInStr.Length().
\return CodeUnit16 offset at which cp was found, Utf16String::npos if cp wasn't found.
*/
Uint32 Find(const Utf16String& searchInStr, CodePoint cp, Uint32 fromCuOffset /* = 0 */)
{
    Uint32 ret = 0;
    Uint32 offLimit = searchInStr.Length();
    for(Uint32 off = fromCuOffset; off < offLimit; ) {
        ret = off;
        if(cp == searchInStr.ItCP(off))
            // we have a match
            return ret;
    }

    // no match
    return Utf16String::npos;
}

// =================================================================================================

/**
Searches string in backward direction for the first occurrence of another string (key).

Preconditions:
1. keyStart != 0, keyEnd != 0, keyEnd > keyStart (checked only in debug).

\param searchInStr String to be searched in, may be empty.
\param keyStart Pointer to first CodeUnit16 in key.
\param keyEnd Pointer to one-past-last CodeUnit16 in key.
\param fromCuOffset CodeUnit16 offset (from beginning of the string) at which search is to begin, may be >= searchInStr.Length().
\return CodeUnit16 offset (from beginning of the string) at which key was found, Utf16String::npos if key wasn't found.
*/
Uint32 RevFind(const Utf16String& searchInStr, const CodeUnit16* keyStart, const CodeUnit16* keyEnd, Uint32 fromCuOffset)
{
    if(RED_LIKELY(fromCuOffset < searchInStr.Length())) {
        // Assert key is valid.
        RED_ASSERT(keyStart && keyEnd && keyEnd > keyStart);

        const CodeUnit16* findRangeEnd = searchInStr.GetPtr() + fromCuOffset + 1;
        const CodeUnit16* foundPtr = std::find_end(searchInStr.GetPtr(), findRangeEnd, keyStart, keyEnd);

        if(foundPtr < findRangeEnd)
            // we have a match
            return (Uint32)(foundPtr - searchInStr.GetPtr());
    }

    // no match
    return Utf16String::npos;
}

/**
Searches string in backward direction for the last character that matches any of the characters specified in string (key).

Preconditions:
1. keyStart != 0, keyEnd != 0, keyEnd > keyStart (checked only in debug).

\param searchInStr String to be searched in, may be empty.
\param keyStart Pointer to first CodeUnit16 in key.
\param keyEnd Pointer to one-past-last CodeUnit16 in key.
\param fromCuOffset CodeUnit16 offset (from beginning of the string) at which search is to begin, may be >= searchInStr.Length().
\return CodeUnit16 offset (from beginning of the string) at which key was found, Utf16String::npos if key wasn't found.
*/
Uint32 RevFindFirstOf(const Utf16String& searchInStr, const CodeUnit16* keyStart, const CodeUnit16* keyEnd, Uint32 fromCuOffset)
{
	if(RED_LIKELY(fromCuOffset < searchInStr.Length())) {
		// Assert key is valid.
		RED_ASSERT(keyStart && keyEnd && keyEnd > keyStart);

		for(Uint32 i = 0, k = fromCuOffset; i <= fromCuOffset; ++i, --k) {
			for(const CodeUnit16* keyPtr = keyStart;  keyPtr != keyEnd; ++keyPtr) {
				const CodeUnit16 cu = *keyPtr;
				if(searchInStr[k] == cu)
					// we have a match
						return k;
			}
		}
	}

	// no match
	return Utf16String::npos;
}

// =================================================================================================

/**
Searches string in backward direction for the first occurrence of CodeUnit16.

\param searchInStr String to be searched in, may be empty.
\param cu CodeUnit16 to be located.
\param fromCuOffset CodeUnit16 offset (from beginning of the string) at which search is to begin, may be >= searchInStr.Length().
\return CodeUnit16 offset (from beginning of the string) at which cu was found, Utf16String::npos if cu wasn't found.
*/
Uint32 RevFind(const Utf16String& searchInStr, CodeUnit16 cu, Uint32 fromCuOffset)
{
    if(RED_LIKELY(fromCuOffset < searchInStr.Length())) {
        for(Uint32 i = 0, k = fromCuOffset; i <= fromCuOffset; ++i, --k) {
            if(searchInStr[k] == cu)
                // we have a match
                return k;
        }
    }

    // no match
    return Utf16String::npos;
}

// =================================================================================================

/**
Searches string in backward direction for the first occurrence of CodePoint.

\param searchInStr String to be searched in, may be empty.
\param cp CodePoint to be located.
\param fromCuOffset CodeUnit16 offset at which search is to begin, may be >= searchInStr.Length().
\return CodeUnit16 offset at which cp was found, Utf16String::npos if cp wasn't found.
*/
Uint32 RevFind(const Utf16String& searchInStr, CodePoint cp, Uint32 fromCuOffset)
{
    if(RED_LIKELY(fromCuOffset < searchInStr.Length())) {

        // due to semantics of RevItCP() we need to advance the offset first
        Uint32 off = fromCuOffset;
        searchInStr.ItCP(off);

        for( ; off > 0; ) {
            if(cp == searchInStr.RevItCP(off))
                // we have a match
                return off;
        }
    }

    // no match
    return Utf16String::npos;
}

// =================================================================================================

/**
Returns Utf16String formed by replacing part of one string with another string.

Preconditions:
1. remCuOff + remSize <= s.Length() (checked only in debug)
2. rep != 0 || (rep == 0 && repSize == 0) (checked only in debug)

Loose notes:
1. When replacing whole string with empty string, empty string with Capacity() == 0 is returned.
   Note this is different from member Replace() which returns empty string with the same capacity
   as before Replace() in such case.

\param s String which part is to be replaced, may be empty.
\param remCuOff Starting CodeUnit16 offset of the part of s that is to be replaced, one-past-last CodeUnit16 offset is ok.
\param remSize Size (in code units) of part in s that is to be replaced, may be 0.
\param rep Pointer to string that is to be inserted into s. May be 0 only if repSize is also 0.
\param repSize Size (in code units) of rep, may be 0.
*/
Utf16String Replace(const Utf16String& s, Uint32 remCuOff, Uint32 remSize, const CodeUnit16* rep, Uint32 repSize)
{
    RED_ASSERT(remCuOff + remSize <= s.Length());
    RED_ASSERT(rep != 0 || (rep == 0 && repSize == 0));

    Uint32 retSize = s.Length() - remSize + repSize;
    Utf16String ret(retSize);

    if(RED_LIKELY(retSize > 0)) {   // if retSize == 0 then there's nothing to do
        // copy parts of s that are not replaced, leave space for rep
        std::memcpy(ret.GetPtr(), s.GetPtr(), remCuOff * sizeof(CodeUnit16));
        std::memcpy(ret.GetPtr() + remCuOff + repSize, s.GetPtr() + remCuOff + remSize, (s.Length() - remCuOff - remSize) * sizeof(CodeUnit16));

        // copy rep
        std::memcpy(ret.GetPtr() + remCuOff, rep, repSize * sizeof(CodeUnit16));

        // set size and null terminate
        ret.SetSize(retSize);
    }

    return ret;
}

// =================================================================================================
} // namespace utf16
// =================================================================================================

// =================================================================================================
} // namespace red
// =================================================================================================
