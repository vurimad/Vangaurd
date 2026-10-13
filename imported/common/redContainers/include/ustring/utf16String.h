#pragma once

#include "../redContainersApi.h"

// =================================================================================================
namespace red {
// =================================================================================================

// forward declaration
class Utf8String;

/**
UTF-16 string.

Size of code unit is 16 bytes.

Sizes, lengths, offsets etc. are expressed in code units (not bytes!) unless explicitly stated otherwise.
*/
class RED_CONTAINERS_API Utf16String
{
public:
    explicit Utf16String(Uint32 minCapacity = 0);
    explicit Utf16String(const char* src);
    Utf16String(const char* src, Uint32 numBytes);
	explicit Utf16String(const UniChar& src);
	explicit Utf16String(const UniChar* src);
	Utf16String(const UniChar* src, Uint32 numCodeUnits);
    explicit Utf16String(const CodeUnit16* src);
    Utf16String(const CodeUnit16* src, Uint32 numCodeUnits);
    explicit Utf16String(const CodeUnit8* src);
    Utf16String(const CodeUnit8* src, Uint32 numCodeUnits);
    explicit Utf16String(const Utf8String& src);
    Utf16String(const Utf16String& other);
	Utf16String(Utf16String&& src);
    ~Utf16String();

    Utf16String& operator=(const Utf16String& rhs);
	Utf16String& operator=( Utf16String&& rhs );

    CodeUnit16& operator[](Uint32 i);
    const CodeUnit16& operator[](Uint32 i) const;

    CodePoint GetCP(Uint32 i) const;
    CodePoint ItCP(Uint32& i) const;
    CodePoint RevItCP(Uint32& i) const;

    Uint32 GetCuOffset(Uint32 cpOffset);

    void Swap(Utf16String& other);

    Bool Empty() const;

    Uint32 Length() const;
    Uint32 Capacity() const;

    CodeUnit16* GetPtr();
    const CodeUnit16* GetPtr() const;

	const UniChar* AsChar() const;

    void Resize(Uint32 newSize);
    void SetSize(Uint32 newSize);
    void Clear();

    void Reserve(Uint32 minCapacity);
    void DelStorage();

	void Remove(Uint32 remstart, Uint32 remEnd );

    void Replace(Uint32 remCuOff, Uint32 remSize, const Utf16String& rep);
    void Replace(Uint32 remCuOff, Uint32 remSize, const Utf16String& rep, Uint32 repCuOff);
    void Replace(Uint32 remCuOff, Uint32 remSize, const Utf16String& rep, Uint32 repCuOff, Uint32 repSize);
    void Replace(Uint32 remCuOff, Uint32 remSize, const CodeUnit16* repNullTerminatedString);
    void Replace(Uint32 remCuOff, Uint32 remSize, const CodeUnit16* rep, Uint32 repSize);

	void Trim();
	void TrimLeft();
	void TrimRight();
		
    void FromUtf8(const CodeUnit8* src);
    void FromUtf8(const CodeUnit8* src, Uint32 numCodeUnits);
    void FromUtf8(const Utf8String& src);

    Utf16String& Append(CodePoint ins);
	Utf16String& Append(const Utf16String& other, Uint32 otherOffset = 0);

    Bool Insert(Uint32 cpOffset, CodePoint ins);

    static const Uint32 npos = static_cast<Uint32>(-1); // bad position

	Uint32 CalcHash() const;
	void Validate( Uint16 crc );

	Int8 Compare( const Int32 start, const Int32 length, const CodeUnit16* srcChars, Int32 srcStart, Int32 srcLength) const;


private:
    void AllocateStorage(Uint32 numCodeUnits);

    Uint32 strSize;     ///< String size in code units. For empty string it is 0.
    Uint32 capacity;    ///< String capacity in code units. It doesn't have to be 0 for empty strings.
    CodeUnit16* str;    ///< Pointer to string storage. It doesn't have to be 0 for empty strings.
};

// =================================================================================================
// Utf16String non-member interface
// =================================================================================================

namespace utf16
{

// trim string from white spaces
RED_CONTAINERS_API Utf16String Trim( const Utf16String& str );

// Wrapper around Utf16String::Swap() for algorithms that use non-member swap() (e.g. std algorithms).
void swap(Utf16String& a, Utf16String& b);

// substring functions
Utf16String SubStr(const Utf16String& s, Uint32 fromCuOffset, Uint32 size);
Utf16String SubStr(const Utf16String& s, Uint32 fromCuOffset);

// searching string in forward direction, from the beginning of the string or from specified offset
RED_CONTAINERS_API Uint32 Find(const Utf16String& searchInStr, const CodeUnit16* keyStart, const CodeUnit16* keyEnd, Uint32 fromCuOffset = 0);
RED_CONTAINERS_API Uint32 FindFirstOf(const Utf16String& searchInStr, const CodeUnit16* keyStart, const CodeUnit16* keyEnd, Uint32 fromCuOffset = 0);
RED_CONTAINERS_API Uint32 Find(const Utf16String& searchInStr, const Utf16String& s, Uint32 fromCuOffset = 0);
RED_CONTAINERS_API Uint32 Find(const Utf16String& searchInStr, const CodeUnit16* nullTerminatedString, Uint32 fromCuOffset = 0);
RED_CONTAINERS_API Uint32 Find(const Utf16String& searchInStr, CodeUnit16 cu, Uint32 fromCuOffset = 0);
RED_CONTAINERS_API Uint32 Find(const Utf16String& searchInStr, CodePoint cp, Uint32 fromCuOffset = 0);

// searching from specified offset in backward direction
Uint32 RevFind(const Utf16String& searchInStr, const CodeUnit16* keyStart, const CodeUnit16* keyEnd, Uint32 fromCuOffset);
RED_CONTAINERS_API Uint32 RevFindFirstOf(const Utf16String& searchInStr, const CodeUnit16* keyStart, const CodeUnit16* keyEnd, Uint32 fromCuOffset = 0);
Uint32 RevFind(const Utf16String& searchInStr, const Utf16String& s, Uint32 fromCuOffset);
Uint32 RevFind(const Utf16String& searchInStr, const CodeUnit16* nullTerminatedString, Uint32 fromCuOffset);
Uint32 RevFind(const Utf16String& searchInStr, CodeUnit16 cu, Uint32 fromCuOffset);
RED_CONTAINERS_API Uint32 RevFind(const Utf16String& searchInStr, CodePoint cp, Uint32 fromCuOffset);

// searching from the end of string in backward direction
Uint32 RevFind(const Utf16String& searchInStr, const CodeUnit16* keyStart, const CodeUnit16* keyEnd);
Uint32 RevFind(const Utf16String& searchInStr, const Utf16String& s);
Uint32 RevFind(const Utf16String& searchInStr, const CodeUnit16* nullTerminatedString);
Uint32 RevFind(const Utf16String& searchInStr, CodeUnit16 cu);
Uint32 RevFind(const Utf16String& searchInStr, CodePoint cp);

// replacing
Utf16String Replace(const Utf16String& s, Uint32 remCuOff, Uint32 remSize, const Utf16String& rep);
Utf16String Replace(const Utf16String& s, Uint32 remCuOff, Uint32 remSize, const Utf16String& rep, Uint32 repCuOff);
Utf16String Replace(const Utf16String& s, Uint32 remCuOff, Uint32 remSize, const Utf16String& rep, Uint32 repCuOff, Uint32 repSize);
Utf16String Replace(const Utf16String& s, Uint32 remCuOff, Uint32 remSize, const CodeUnit16* repNullTerminatedString);
RED_CONTAINERS_API Utf16String Replace(const Utf16String& s, Uint32 remCuOff, Uint32 remSize, const CodeUnit16* rep, Uint32 repSize);

Uint32 CountLines(const Utf16String& s);

// =================================================================================================
} // namespace utf16
// =================================================================================================

// Equality operators.
Bool operator==(const Utf16String& lhs, const Utf16String& rhs);
Bool operator==(const Utf16String& lhs, const CodeUnit16* rhs);
Bool operator==(const CodeUnit16* lhs, const Utf16String& rhs);

// Inequality operators.
Bool operator!=(const Utf16String& lhs, const Utf16String& rhs);
Bool operator!=(const Utf16String& lhs, const CodeUnit16* rhs);
Bool operator!=(const CodeUnit16* lhs, const Utf16String& rhs);

// Less operators.
Bool operator<(const Utf16String& lhs, const Utf16String& rhs);
Bool operator<(const Utf16String& lhs, const CodeUnit16* rhs);
Bool operator<(const CodeUnit16* lhs, const Utf16String& rhs);

// More operators.
Bool operator>(const Utf16String& lhs, const Utf16String& rhs);
Bool operator>(const Utf16String& lhs, const CodeUnit16* rhs);
Bool operator>(const CodeUnit16* lhs, const Utf16String& rhs);


// =================================================================================================
// implementations of non-member functions
// =================================================================================================

namespace utf16
{

/**
Swaps two Utf16String objects.

This is a wrapper around Utf16String::Swap(). Many algorithms (e.g. all std algorithms) use
non-member swap() function.
*/
RED_INLINE void swap(Utf16String& a, Utf16String& b)
{
    a.Swap(b);
}

// =================================================================================================

/**
Creates substring.

Preconditions:
1. Function is not called for empty string (checked only in debug).
2. fromCuOffset and length does not cause overrun (checked only in debug).

\param s Source string.
\param fromCuOffset CodeUnit16 offset denoting beginning of substring.
\param length Size of substring in code units. If 0 then empty string will be produced.
*/
RED_INLINE Utf16String SubStr(const Utf16String& s, Uint32 fromCuOffset, Uint32 size)
{
	if (s.Length() == 0)
	{
		return Utf16String();
	}

    // Assert we don't overrun.
    RED_ASSERT(fromCuOffset + size <= s.Length());

    return Utf16String(s.GetPtr() + fromCuOffset, size);
}

// =================================================================================================

/**
Creates substring - use code units from specified position to the end of string.

Preconditions:
1. Function is not called for empty string (checked only in debug).
2. fromCuOffset < Length() (checked only in debug).

\param s Source string.
\param fromCuOffset CodeUnit16 offset denoting beginning of substring.
*/
RED_INLINE Utf16String SubStr(const Utf16String& s, Uint32 fromCuOffset)
{
	if (s.Length() == 0)
	{
		return Utf16String();
	}

    // Assert we don't overrun.
    RED_ASSERT(fromCuOffset < s.Length());

    return Utf16String(s.GetPtr() + fromCuOffset, s.Length() - fromCuOffset);
}

// =================================================================================================

/**
Searches string in forward direction for the first occurrence of another string.

Preconditions:
1. s is not empty (checked only in debug).

\param searchInStr String to be searched in, may be empty.
\param s String to be located.
\param fromCuOffset CodeUnit16 offset at which search is to begin, may be >= searchInStr.Length().
\return CodeUnit16 offset at which s was found, Utf16String::npos if s wasn't found.
*/
RED_INLINE Uint32 Find(const Utf16String& searchInStr, const Utf16String& s, Uint32 fromCuOffset /* = 0 */)
{
    return Find(searchInStr, s.GetPtr(), s.GetPtr() + s.Length(), fromCuOffset);
}

// =================================================================================================

/**
Searches string in forward direction for the first occurrence of another string.

Preconditions:
1. nullTerminatedString != 0 and is not empty (checked only in debug).

\param searchInStr String to be searched in, may be empty.
\param nullTerminatedString Null terminated string to be located.
\param fromCuOffset CodeUint16 offset at which search is to begin, may be >= searchInStr.Length().
\return CodeUnit16 offset at which s was found, Utf16String::npos if s wasn't found.
*/
RED_INLINE Uint32 Find(const Utf16String& searchInStr, const CodeUnit16* nullTerminatedString, Uint32 fromCuOffset /* = 0 */)
{
    return Find(searchInStr, nullTerminatedString, nullTerminatedString + UStrLen(nullTerminatedString), fromCuOffset);
}

// =================================================================================================

/**
Searches string in backward direction for the first occurrence of another string.

Preconditions:
1. s is not empty (checked only in debug).

\param searchInStr String to be searched in, may be empty.
\param s String to be located.
\param fromCuOffset CodeUnit16 offset (from beginning of the string) at which search is to begin, may be >= searchInStr.Length().
\return CodeUnit16 offset (from beginning of the string) at which s was found, Utf16String::npos if s wasn't found.
*/
RED_INLINE Uint32 RevFind(const Utf16String& searchInStr, const Utf16String& s, Uint32 fromCuOffset)
{
    return RevFind(searchInStr, s.GetPtr(), s.GetPtr() + s.Length(), fromCuOffset);
}

// =================================================================================================

/**
Searches string in backward direction for the first occurrence of another string.

Preconditions:
1. nullTerminatedString != 0 and is not empty (checked only in debug).

\param searchInStr String to be searched in, may be empty.
\param nullTerminatedString Null terminated string to be located.
\param fromCuOffset CodeUnit16 offset (from beginning of the string) at which search is to begin, mey be >= searchInStr.Length().
\return CodeUnit16 offset (from beginning of the string) at which s was found, Utf16String::npos if s wasn't found.
*/
RED_INLINE Uint32 RevFind(const Utf16String& searchInStr, const CodeUnit16* nullTerminatedString, Uint32 fromCuOffset)
{
    return RevFind(searchInStr, nullTerminatedString, nullTerminatedString + UStrLen(nullTerminatedString), fromCuOffset);
}

// =================================================================================================

/**
Searches string in backward direction for the first occurrence of another string (key).

Preconditions:
1. Utf16String is not empty (checked only in debug).
2. keyStart != 0, keyEnd != 0, keyEnd > keyStart (checked only in debug).

\param searchInStr String to be searched in.
\param keyStart Pointer to first CodeUnit16 in key.
\param keyEnd Pointer to one-past-last CodeUnit16 in key.
\return CodeUnit16 offset (from beginning of the string) at which key was found, Utf16String::npos if key wasn't found.
*/
RED_INLINE Uint32 RevFind(const Utf16String& searchInStr, const CodeUnit16* keyStart, const CodeUnit16* keyEnd)
{
    return RevFind(searchInStr, keyStart, keyEnd, searchInStr.Length() - 1);
}

// =================================================================================================

/**
Searches string in backward direction for the first occurrence of another string.

Preconditions:
1. Utf16String is not empty (checked only in debug).
2. s is not empty (checked only in debug).

\param searchInStr String to be searched in.
\param s String to be located.
\return CodeUnit16 offset (from beginning of the string) at which s was found, Utf16String::npos if s wasn't found.
*/
RED_INLINE Uint32 RevFind(const Utf16String& searchInStr, const Utf16String& s)
{
    return RevFind(searchInStr, s, searchInStr.Length() - 1);
}

// =================================================================================================

/**
Searches string in backward direction for the first occurrence of another string.

Preconditions:
1. Utf16String is not empty (checked only in debug).
2. nullTerminatedString != 0 and is not empty (checked only in debug).

\param searchInStr String to be searched in.
\param nullTerminatedString Null terminated string to be located.
\return CodeUnit16 offset (from beginning of the string) at which s was found, Utf16String::npos if s wasn't found.
*/
RED_INLINE Uint32 RevFind(const Utf16String& searchInStr, const CodeUnit16* nullTerminatedString)
{
    return RevFind(searchInStr, nullTerminatedString, searchInStr.Length() - 1);
}

// =================================================================================================

/**
Searches string in backward direction for the first occurrence of CodeUnit16.

Preconditions:
1. Utf16String is not empty (checked only in debug).

\param searchInStr String to be searched in.
\param cu CodeUnit16 to be located.
\return CodeUnit16 offset (from beginning of the string) at which cu was found, Utf16String::npos if cu wasn't found.
*/
RED_INLINE Uint32 RevFind(const Utf16String& searchInStr, CodeUnit16 cu)
{
    return RevFind(searchInStr, cu, searchInStr.Length() - 1);
}

// =================================================================================================

/**
Searches string in backward direction for the first occurrence of CodePoint.

Preconditions:
1. Utf16String is not empty (checked only in debug).

\param searchInStr String to be searched in.
\param cp CodePoint to be located.
\return CodeUnit16 offset (from beginning of the string) at which cp was found, Utf16String::npos if cp wasn't found.
*/
RED_INLINE Uint32 RevFind(const Utf16String& searchInStr, CodePoint cp)
{
    return RevFind(searchInStr, cp, searchInStr.Length() - 1);
}

// =================================================================================================

/**
Returns Utf16String formed by replacing part of some string with another string.

Preconditions:
1. remCuOff + remSize <= s.Length() (checked only in debug)

Loose notes:
1. When replacing whole string with empty string, empty string with Capacity() == 0 is returned.
   Note this is different from member Replace() which returns empty string with the same capacity
   as before Replace() in such case.

\param s String which part is to be replaced, may be empty.
\param remCuOff Starting CodeUnit16 offset of the part of s that is to be replaced, one-past-last CodeUnit16 offset is ok.
\param remSize Size (in code units) of part in s that is to be replaced, may be 0.
\param rep Replacement string, may be empty.
*/
RED_INLINE Utf16String Replace(const Utf16String& s, Uint32 remCuOff, Uint32 remSize, const Utf16String& rep)
{
    RED_ASSERT(remCuOff + remSize <= s.Length());

    return Replace(s, remCuOff, remSize, rep.GetPtr(), rep.Length());
}

// =================================================================================================

/**
Returns Utf16String formed by replacing part of some string with part of another string.

Preconditions:
1. remCuOff + remSize <= s.Length() (checked only in debug)
2. repCuOff <= rep.Length() (checked only in debug)

Loose notes:
1. When replacing whole string with empty string, empty string with Capacity() == 0 is returned.
   Note this is different from member Replace() which returns empty string with the same capacity
   as before Replace() in such case.

\param s String which part is to be replaced, may be empty.
\param remCuOff Starting CodeUnit16 offset of the part of s that is to be replaced, one-past-last CodeUnit16 offset is ok.
\param remSize Size (in code units) of part in s that is to be replaced, may be 0.
\param rep Replacement string, may be empty.
\param repCuOff Starting CodeUnit16 offset of the part of rep that is to be used, one-past-last CodeUnit16 offset is ok.
*/
RED_INLINE Utf16String Replace(const Utf16String& s, Uint32 remCuOff, Uint32 remSize, const Utf16String& rep, Uint32 repCuOff)
{
    RED_ASSERT(remCuOff + remSize <= s.Length());
    RED_ASSERT(repCuOff <= rep.Length());

    return Replace(s, remCuOff, remSize, rep.GetPtr() + repCuOff, rep.Length() - repCuOff);
}

// =================================================================================================

/**
Returns Utf16String formed by replacing part of some string with part of another string.

Preconditions:
1. remCuOff + remSize <= s.Length() (checked only in debug)
2. repCuOff + repSize <= rep.Length() (checked only in debug)

Loose notes:
1. When replacing whole string with empty string, empty string with Capacity() == 0 is returned.
   Note this is different from member Replace() which returns empty string with the same capacity
   as before Replace() in such case.

\param s String which part is to be replaced, may be empty.
\param remCuOff Starting CodeUnit16 offset of the part of s that is to be replaced, one-past-last CodeUnit16 offset is ok.
\param remSize Size (in code units) of part in s that is to be replaced, may be 0.
\param rep Replacement string, may be empty.
\param repCuOff Starting CodeUnit16 offset of the part of rep that is to be used, one-past-last CodeUnit16 offset is ok.
\param repSize Size (in code units) of part of rep that is to be used, may be 0.
*/
RED_INLINE Utf16String Replace(const Utf16String& s, Uint32 remCuOff, Uint32 remSize, const Utf16String& rep, Uint32 repCuOff, Uint32 repSize)
{
    RED_ASSERT(remCuOff + remSize <= s.Length());
    RED_ASSERT(repCuOff + repSize <= rep.Length()); // note this allows repCuOff == rep.Length() && repSize == 0

    return Replace(s, remCuOff, remSize, rep.GetPtr() + repCuOff, repSize);
}

// =================================================================================================

/**
Returns Utf16String formed by replacing part of some string with another string.

Preconditions:
1. remCuOff + remSize <= s.Length() (checked only in debug)
2. repNullTerminatedString != 0 (checked only in debug)
3. repNullTerminatedString is null terminated string (not checked)

Loose notes:
1. When replacing whole string with empty string, empty string with Capacity() == 0 is returned.
   Note this is different from member Replace() which returns empty string with the same capacity
   as before Replace() in such case.

\param s String which part is to be replaced, may be empty.
\param remCuOff Starting CodeUnit16 offset of the part of s that is to be replaced, one-past-last CodeUnit16 offset is ok.
\param remSize Size (in code units) of part in s that is to be replaced, may be 0.
\param repNullTerminatedString Pointer to null terminated replacement string, may be empty, must not be 0.
*/
RED_INLINE Utf16String Replace(const Utf16String& s, Uint32 remCuOff, Uint32 remSize, const CodeUnit16* repNullTerminatedString)
{
    RED_ASSERT(remCuOff + remSize <= s.Length());
    RED_ASSERT(repNullTerminatedString);

    return Replace(s, remCuOff, remSize, repNullTerminatedString, UStrLen(repNullTerminatedString));
}

// =================================================================================================

/**
Counts number of lines in string.

\param s String to be examined. May be empty.
\return Number of lines in string. For empty strings 0 is returned.
*/
RED_INLINE Uint32 CountLines(const Utf16String& s)
{
    return red::CountLines(s.GetPtr(), s.Length());
}


// =================================================================================================
} // namespace utf16
// =================================================================================================

// =================================================================================================

/**
Returns whether two UTF-16 strings are binary equal.

Two empty strings are considered to be equal.

\param lhs First UTF-16 string.

\param rhs Second UTF-16 string.
*/
RED_INLINE Bool operator==(const Utf16String& lhs, const Utf16String& rhs)
{
    // a == b, where a and b are Utf16String objects
    // Possible cases (see below for meaning of S, E0, EN):
    // 1) aS == bS, different sizes && different ptrs
    // 2) aS == bS, same sizes && different ptrs
    // 3) aS == aS (compare with self)
    // 4) aE0 == bE0
    // 5) aEN == bEN, different ptrs
    // 6) aEN == aEN (compare with self)
    // 7) aE0 == bS
    // 8) aEN == bS
    //
    // where:
    // S - non-empty string:              Length()  > 0 && Capacity()  > 0 && GetPtr() != 0
    // E0 - empty string with no storage: Length() == 0 && Capacity() == 0 && GetPtr() == 0
    // EN - empty string with storage:    Length() == 0 && Capacity()  > 0 && GetPtr() != 0

    // this will handle 3, 4, 6
    if(lhs.GetPtr() == rhs.GetPtr())
        return true;

    // this will handle 1, 7, 8
    if(lhs.Length() != rhs.Length())
        return false;

    // this will handle 2, 5
    return !std::memcmp(lhs.GetPtr(), rhs.GetPtr(), lhs.Length() * sizeof(CodeUnit16));
}

// =================================================================================================

/**
Returns whether two UTF-16 string are binary equal.

Two empty strings compare equal.

\param lhs First UTF-16 string.

\param rhs Second UTF-16 string - this has to be null (U+0000) terminated. Can't be 0, otherwise
behavior is undefined as this is not checked (except for assertion in debug builds).
*/
RED_INLINE Bool operator==(const Utf16String& lhs, const CodeUnit16* rhs)
{
    // a == b, where a is Utf16String object and b is const CodeUnit16* to null terminated UTF-16 string
    // Possible cases (see below for meaning of S, E0, EN):
    // 1)  aS == b, different sizes && different ptrs
    // 2)  aS == b, same sizes && different ptrs
    // 3)  aS == b, same sizes && same ptrs (e.g.: aS == aS.GetPtr())
    // 4)  aS == b, different sizes && same ptrs (e.g.: aS == aS.GetPtr() where a contains null)
    // 5)  aE0 == b, different sizes && different ptrs
    // 6)  aE0 == b, same sizes && different ptrs (e.g.: when b[0] is U+0000)
    // 7)  aE0 == b, same sizes && same ptrs (not possible since b must be != 0)
    // 8)  aE0 == b, different sizes && same ptrs (not possible sice b must be != 0)
    // 9)  aEN == b, differet sizes && different ptrs
    // 10) aEN == b, same sizes && different ptrs (e.g.: when b[0] is U+0000)
    // 11) aEN == b, same sizes && same ptrs (e.g.: aEN == aEN.GetPtr() where first char in aEN is U+0000)
    // 12) aEN == b, different sizes && same ptrs (e.g.: aEN == aEN.GetPtr())
    //
    // where:
    // S - non-empty string:              Length()  > 0 && Capacity()  > 0 && GetPtr() != 0
    // E0 - empty string with no storage: Length() == 0 && Capacity() == 0 && GetPtr() == 0
    // EN - empty string with storage:    Length() == 0 && Capacity()  > 0 && GetPtr() != 0

    // rhs can't be 0
    RED_ASSERT(rhs);

    // this will handle 1, 4, 5, 9, 12
    if(lhs.Length() != UStrLen(rhs))
        return false;

    // this will handle 3, 11
    if(lhs.GetPtr() == rhs)
        return true;

    // this will handle 2, 6, 10
    return !std::memcmp(lhs.GetPtr(), rhs, lhs.Length() * sizeof(CodeUnit16));
}

// =================================================================================================

/**
Returns whether two UTF-16 string are binary equal.

Two empty strings compare equal.

\param lhs First UTF-16 string - this has to be null (U+0000) terminated. Can't be 0, otherwise
behavior is undefined as this is not checked (except for assertion in debug builds).

\param rhs Second UTF-16 string.
*/
RED_INLINE Bool operator==(const CodeUnit16* lhs, const Utf16String& rhs)
{
    return rhs == lhs;
}

// =================================================================================================

/**
Returns whether two UTF-16 strings are binary different.

\param lhs First UTF-16 string.

\param rhs Second UTF-16 string.
*/
RED_INLINE Bool operator!=(const Utf16String& lhs, const Utf16String& rhs)
{
    return !(lhs == rhs);
}

// =================================================================================================

/**
Returns whether two UTF-16 string are binary different.

\param lhs First UTF-16 string.

\param rhs Second UTF-16 string - this has to be null (U+0000) terminated. Can't be 0, otherwise
behavior is undefined as this is not checked (except for assertion in debug builds).
*/
RED_INLINE Bool operator!=(const Utf16String& lhs, const CodeUnit16* rhs)
{
    return !(lhs == rhs);
}

// =================================================================================================

/**
Returns whether two UTF-16 string are binary different.

\param lhs First UTF-16 string - this has to be null (U+0000) terminated. Can't be 0, otherwise
behavior is undefined as this is not checked (except for assertion in debug builds).

\param rhs Second UTF-16 string.
*/
RED_INLINE Bool operator!=(const CodeUnit16* lhs, const Utf16String& rhs)
{
    return !(lhs == rhs);
}

// =================================================================================================

/**
Less than operator. Performs only bitwise comparison.

\param lhs First UTF-16 string.

\param rhs Second UTF-16 string.
*/
RED_INLINE Bool operator<(const Utf16String& lhs, const Utf16String& rhs)
{
	return lhs.Compare( 0, lhs.Length(), rhs.GetPtr(), 0, rhs.Length() ) == -1;
}

// =================================================================================================

/**
Less than operator. Performs only bitwise comparison.

\param lhs First UTF-16 string.

\param rhs Second UTF-16 string - this has to be null (U+0000) terminated. Can't be 0, otherwise
behavior is undefined as this is not checked (except for assertion in debug builds).
*/
RED_INLINE Bool operator<(const Utf16String& lhs, const CodeUnit16* rhs)
{
	return lhs.Compare( 0, lhs.Length(), rhs, 0, UStrLen(rhs) ) < 0;
}

// =================================================================================================

/**
Less than operator. Performs only bitwise comparison.

\param lhs First UTF-16 string - this has to be null (U+0000) terminated. Can't be 0, otherwise
behavior is undefined as this is not checked (except for assertion in debug builds).

\param rhs Second UTF-16 string.
*/
RED_INLINE Bool operator<(const CodeUnit16* lhs, const Utf16String& rhs)
{
	// NOTE : Comparison and operands flipped!
	return rhs > lhs;
}

// =================================================================================================

/**
More than operator. Performs only bitwise comparison.

\param lhs First UTF-16 string.

\param rhs Second UTF-16 string.
*/
RED_INLINE Bool operator>(const Utf16String& lhs, const Utf16String& rhs)
{
	return lhs.Compare( 0, lhs.Length(), rhs.GetPtr(), 0, rhs.Length() ) == 1;
}

// =================================================================================================

/**
More than operator. Performs only bitwise comparison.

\param lhs First UTF-16 string.

\param rhs Second UTF-16 string - this has to be null (U+0000) terminated. Can't be 0, otherwise
behavior is undefined as this is not checked (except for assertion in debug builds).
*/
RED_INLINE Bool operator>(const Utf16String& lhs, const CodeUnit16* rhs)
{
	return lhs.Compare( 0, lhs.Length(), rhs, 0, UStrLen(rhs) ) > 0;
}

// =================================================================================================

/**
More than operator. Performs only bitwise comparison.

\param lhs First UTF-16 string - this has to be null (U+0000) terminated. Can't be 0, otherwise
behavior is undefined as this is not checked (except for assertion in debug builds).

\param rhs Second UTF-16 string.
*/
RED_INLINE Bool operator>(const CodeUnit16* lhs, const Utf16String& rhs)
{
	// NOTE : Comparison and operands flipped!
	return rhs < lhs;
}

// =================================================================================================
} // namespace red
// =================================================================================================
