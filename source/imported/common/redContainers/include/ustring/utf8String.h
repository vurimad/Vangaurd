#pragma once

#include "utypes.h"
#include "ustringUtil.h"
#include <cstring>   // memcpy, memcmp
#include "../redContainersApi.h"

// =================================================================================================
namespace red {
// =================================================================================================

// forward declaration
class Utf16String;

/**
UTF-8 string.

Size of code unit is 8 bytes.

Sizes, lengths, offsets etc. are expressed in code units unless explicitly stated otherwise. Of course,
in case of UTF-8, code unit is 1 byte - still, it's better to think in terms of code units.
*/
class RED_CONTAINERS_API Utf8String
{
public:
    explicit Utf8String(Uint32 minCapacity = 0);
    Utf8String(const char* src);
    Utf8String(const char* src, Uint32 numBytes);
	explicit Utf8String(const UniChar* src);
	Utf8String(const UniChar* src, Uint32 numCodeUnits);
    explicit Utf8String(const CodeUnit8* src);
    Utf8String(const CodeUnit8* src, Uint32 numCodeUnits);
    explicit Utf8String(const CodeUnit16* src);
    Utf8String(const CodeUnit16* src, Uint32 numCodeUnits);
    explicit Utf8String(const Utf16String& src);
    Utf8String(const Utf8String& other);
	Utf8String(Utf8String&& other);
    ~Utf8String();

    Utf8String& operator=(const Utf8String& rhs);
    Utf8String& operator+=(const Utf8String& rhs);

    Utf8String operator+(const Utf8String& rhs) const;

    CodeUnit8& operator[](Uint32 i);
    const CodeUnit8& operator[](Uint32 i) const;

    void Swap(Utf8String& other);

    Bool Empty() const;

    Uint32 Length() const;
    Uint32 Capacity() const;

    CodeUnit8* GetPtr();
    const CodeUnit8* GetPtr() const;

    void Resize(Uint32 newSize);
    void SetSize(Uint32 newSize);
    void Clear();

    void Reserve(Uint32 minCapacity);
    void DelStorage();

    void FromUtf16(const CodeUnit16* src);
    void FromUtf16(const CodeUnit16* src, Uint32 numCodeUnits);
    void FromUtf16(const Utf16String& src);

    Utf8String& Append(const Utf8String& rhs);

private:
    void AllocateStorage(Uint32 numCodeUnits);

    Uint32 strSize;     ///< String size in code units. For empty string it is 0.
    Uint32 capacity;    ///< String capacity in code units. It doesn't have to be 0 for empty strings.
    CodeUnit8* str;     ///< Pointer to string storage. It doesn't have to be 0 for empty strings.
};

// =================================================================================================
// non-member interface
// =================================================================================================

namespace utf8
{

// Wrapper around Utf8String::Swap() for algorithms that use non-member swap() (e.g. std algorithms).
void swap(Utf8String& a, Utf8String& b);

// =================================================================================================
} // namespace utf8
// =================================================================================================

// Equality operators.
Bool operator==(const Utf8String& lhs, const Utf8String& rhs);
Bool operator==(const Utf8String& lhs, const CodeUnit8* rhs);
Bool operator==(const CodeUnit8* lhs, const Utf8String& rhs);

// Inequality operators.
Bool operator!=(const Utf8String& lhs, const Utf8String& rhs);
Bool operator!=(const Utf8String& lhs, const CodeUnit8* rhs);
Bool operator!=(const CodeUnit8* lhs, const Utf8String& rhs);

// =================================================================================================
// implementations of member functions
// =================================================================================================

/**
Ctor - creates Utf8String without initializing string's contents.

\param minCapacity Min capacity of string, in code units. If 0 then no storage will be allocated.
*/
RED_INLINE Utf8String::Utf8String(Uint32 minCapacity /* = 0 */)
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

\param src Null terminated US-ASCII or UTF-8 string. If 0 then empty string will be produced.
*/
RED_INLINE Utf8String::Utf8String(const char* src)
: strSize(0),
  capacity(0),
  str(0)
{
    if(RED_LIKELY(src != 0)) {
        strSize = (Uint32)red::Strlen(src);

        if(RED_LIKELY(strSize > 0)) {
            AllocateStorage(strSize);
            std::memcpy(str, src, strSize);
            str[strSize] = 0; // null terminate
        }
    }
}

// =================================================================================================

/**
Ctor.

See Utf8String::Utf8String(const char*) docs for info on using character literals.

\param src US-ASCII or UTF-8 string. May contain null characters. If 0 then empty string will be produced.

\param numBytes Number of bytes in src. If 0 then empty string will be produced.
*/
RED_INLINE Utf8String::Utf8String(const char* src, Uint32 numBytes)
: strSize(0),
  capacity(0),
  str(0)
{
    if(RED_LIKELY(src && numBytes > 0)) {
        AllocateStorage(numBytes);
        std::memcpy(str, src, numBytes);
        strSize = numBytes;
        str[strSize] = 0; // null terminate
    }
}

// =================================================================================================

/**
Ctor.

\param src Null terminated US-ASCII or UTF-8 string. If 0 then empty string will be produced.
*/
RED_INLINE Utf8String::Utf8String(const CodeUnit8* src)
: strSize(0),
  capacity(0),
  str(0)
{
    if(RED_LIKELY(src != 0)) {
        strSize = (Uint32)red::Strlen(reinterpret_cast<const char*>(src));

        if(RED_LIKELY(strSize > 0)) {
            AllocateStorage(strSize);
            std::memcpy(str, src, strSize);
            str[strSize] = 0; // null terminate
        }
    }
}

// =================================================================================================

/**
Ctor.

\param src US-ASCII or UTF-8 string. May contain null characters. If 0 then empty string will be produced.

\param numCodeUnits Number of code units in src. If 0 then empty string will be produced.
*/
RED_INLINE Utf8String::Utf8String(const CodeUnit8* src, Uint32 numCodeUnits)
: strSize(0),
  capacity(0),
  str(0)
{
    if(RED_LIKELY(src && numCodeUnits > 0)) {
        AllocateStorage(numCodeUnits);
        std::memcpy(str, src, numCodeUnits);
        strSize = numCodeUnits;
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
RED_INLINE Utf8String::Utf8String(const CodeUnit16* src)
: strSize(0),
  capacity(0),
  str(0)
{
    if(RED_LIKELY(src != 0)) {
        Uint32 srcNumCodeUnits = UStrLen(src);
        if(RED_LIKELY(srcNumCodeUnits > 0)) {
            AllocateStorage(srcNumCodeUnits * 3); // we need at most 3x more UTF-8 code units
            Utf16ToUtf8(str, capacity, &strSize, src, srcNumCodeUnits, UnicodeChars::replacementCharacter, 0);
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
RED_INLINE Utf8String::Utf8String(const CodeUnit16* src, Uint32 numCodeUnits)
: strSize(0),
  capacity(0),
  str(0)
{
    if(RED_LIKELY(src && numCodeUnits > 0)) {
        AllocateStorage(numCodeUnits * 3); // we need at most 3x more UTF-8 code units
        Utf16ToUtf8(str, capacity, &strSize, src, numCodeUnits, UnicodeChars::replacementCharacter, 0);
        str[strSize] = 0; // null terminate
    }
}

// =================================================================================================

/**
Cctor - creates string copy.

Note that copy may have different (smaller/bigger) capacity than original string - this depends on
different factors taken into account by storage allocator.

\param other Utf8String that is to be copied.
*/
RED_INLINE Utf8String::Utf8String(const Utf8String& other)
: strSize(other.strSize),
  capacity(0),
  str(0)
{
    // Creating from non-empty string.
    if(RED_LIKELY(strSize > 0)) {
        AllocateStorage(strSize);
        std::memcpy(str, other.str, other.strSize);
        str[strSize] = 0; // null terminate
    }
}

// =================================================================================================

/**
Move constructor
*/

RED_INLINE Utf8String::Utf8String( Utf8String&& src )
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
RED_INLINE Utf8String::~Utf8String()
{
    DelStorage();
}

// =================================================================================================

/**
Returns reference to 8-bit code unit at specified index.

\param i Index of code unit that is to be returned. It has to satisfy condition: i < Length().
Otherwise, behavior is undefined since operator[] doesn't check this condition (except for
assertion in debug builds).
*/
RED_INLINE CodeUnit8& Utf8String::operator[](Uint32 i)
{
    RED_ASSERT(i < Length());
    return str[i];
}

// =================================================================================================

/**
Returns const reference to 8-bit code unit at specified index.

\param i Index of code unit that is to be returned. It has to satisfy condition: i < Length().
Otherwise, behavior is undefined since operator[] doesn't check this condition (except for
assertion in debug builds).
*/
RED_INLINE const CodeUnit8& Utf8String::operator[](Uint32 i) const
{
    RED_ASSERT(i < Length());
    return str[i];
}

// =================================================================================================

/**
Swaps this string with another.

Trying to Swap() Utf8String with itself does nothing.
*/
RED_INLINE void Utf8String::Swap(Utf8String& other)
{
    if(RED_LIKELY(this != &other)) {
        unsigned char temp[sizeof(Utf8String)];
        std::memcpy(temp, this, sizeof(Utf8String));
        std::memcpy(this, &other, sizeof(Utf8String));
        std::memcpy(&other, temp, sizeof(Utf8String));
    }
}

// =================================================================================================

/**
Returns whether string is empty.

String is empty if its size is 0. Note that this doesn't necessarily means that Capacity() == 0.
*/
RED_INLINE Bool Utf8String::Empty() const
{
    return Length() == 0;
}

// =================================================================================================

/**
Returns size of string in code units.

\return Size of string in code units, for empty string it's 0.
*/
RED_INLINE Uint32 Utf8String::Length() const
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
RED_INLINE Uint32 Utf8String::Capacity() const
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
RED_INLINE const CodeUnit8* Utf8String::GetPtr() const
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
RED_INLINE CodeUnit8* Utf8String::GetPtr()
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
RED_INLINE void Utf8String::Resize(Uint32 newSize)
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
        Utf8String s(newSize);                     // newSize as min capacity
        std::memcpy(s.str, GetPtr(), Length());   // copy old contents
        s.strSize = newSize;                       // set requested size
        s.str[newSize] = 0;                        // null terminate

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
RED_INLINE void Utf8String::SetSize(Uint32 newSize)
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
RED_INLINE void Utf8String::Clear()
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
RED_INLINE void Utf8String::Reserve(Uint32 minCapacity)
{
    if(Capacity() < minCapacity) {
        Utf8String s(minCapacity);
        std::memcpy(s.str, GetPtr(), Length());    // copy old contents
        s.strSize = Length();
        s.str[Length()] = 0;                       // null terminate

        s.Swap(*this);
    }
}

// =================================================================================================

/**
Makes string empty and deletes its storage.
*/
RED_INLINE void Utf8String::DelStorage()
{
    if(RED_LIKELY(Capacity() > 0))
	{
		RED_FREE( red::PoolEngine,  str );
	}
    strSize = 0;
    capacity = 0;
    str = nullptr;
}

// =================================================================================================

/**
Allocates storage for specified number of code units.

Preconditions:
1. no storage is allocated (checked only in debug)

The function may allocate more storage than requested.
*/
RED_INLINE void Utf8String::AllocateStorage(Uint32 numCodeUnits)
{
    // assert no storage is allocated
    RED_ASSERT(!str);

    // Allocate storage. We ask for numCodeUnits + 1, because we need space for null. This is also
    // why we decrement capacity by one after allocation.
	str = static_cast< CodeUnit8* >( RED_ALLOCATE( red::PoolEngine, sizeof( CodeUnit8 ) * ( numCodeUnits + 1 ) ) ); 
    capacity = numCodeUnits;
}

// =================================================================================================
// implementations of non-member functions
// =================================================================================================

namespace utf8
{
/**
Swaps two Utf8String objects.

This is a wrapper around Utf8String::Swap(). Many algorithms (e.g. all std algorithms) use
non-member swap() function.
*/
RED_INLINE void swap(Utf8String& a, Utf8String& b)
{
    a.Swap(b);
}

// =================================================================================================
} // namespace utf8
// =================================================================================================

/**
Returns whether two UTF-8 strings are binary equal.

Two empty strings are considered to be equal.

\param lhs First UTF-8 string.

\param rhs Second UTF-8 string.
*/
RED_INLINE Bool operator==(const Utf8String& lhs, const Utf8String& rhs)
{
    // a == b, where a and b are Utf8String objects
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
    return !std::memcmp(lhs.GetPtr(), rhs.GetPtr(), lhs.Length() * sizeof(CodeUnit8));
}

// =================================================================================================

/**
Returns whether two UTF-8 string are binary equal.

Two empty strings compare equal.

\param lhs First UTF-8 string.

\param rhs Second UTF-8 string - this has to be null terminated. Can't be 0, otherwise
behavior is undefined as this is not checked (except for assertion in debug builds).
*/
RED_INLINE Bool operator==(const Utf8String& lhs, const CodeUnit8* rhs)
{
    // a == b, where a is Utf8String object and b is const CodeUnit8* to null terminated UTF-8 string
    // Possible cases (see below for meaning of S, E0, EN):
    // 1)  aS == b, different sizes && different ptrs
    // 2)  aS == b, same sizes && different ptrs
    // 3)  aS == b, same sizes && same ptrs (e.g.: aS == aS.GetPtr())
    // 4)  aS == b, different sizes && same ptrs (e.g.: aS == aS.GetPtr() where a contains null)
    // 5)  aE0 == b, different sizes && different ptrs
    // 6)  aE0 == b, same sizes && different ptrs (e.g.: when b[0] is null)
    // 7)  aE0 == b, same sizes && same ptrs (not possible since b must be != 0)
    // 8)  aE0 == b, different sizes && same ptrs (not possible sice b must be != 0)
    // 9)  aEN == b, differet sizes && different ptrs
    // 10) aEN == b, same sizes && different ptrs (e.g.: when b[0] is null)
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
    if(lhs.Length() != (Uint32)red::Strlen(reinterpret_cast<const char*>(rhs)))
        return false;

    // this will handle 3, 11
    if(lhs.GetPtr() == rhs)
        return true;

    // this will handle 2, 6, 10
    return !std::memcmp(lhs.GetPtr(), rhs, lhs.Length() * sizeof(CodeUnit16));
}

// =================================================================================================

/**
Returns whether two UTF-8 string are binary equal.

Two empty strings compare equal.

\param lhs First UTF-8 string - this has to be null terminated. Can't be 0, otherwise
behavior is undefined as this is not checked (except for assertion in debug builds).

\param rhs Second UTF-8 string.
*/
RED_INLINE Bool operator==(const CodeUnit8* lhs, const Utf8String& rhs)
{
    return rhs == lhs;
}

// =================================================================================================

/**
Returns whether two UTF-8 strings are binary different.

\param lhs First UTF-8 string.

\param rhs Second UTF-8 string.
*/
RED_INLINE Bool operator!=(const Utf8String& lhs, const Utf8String& rhs)
{
    return !(lhs == rhs);
}

// =================================================================================================

/**
Returns whether two UTF-8 string are binary different.

\param lhs First UTF-8 string.

\param rhs Second UTF-8 string - this has to be null terminated. Can't be 0, otherwise
behavior is undefined as this is not checked (except for assertion in debug builds).
*/
RED_INLINE Bool operator!=(const Utf8String& lhs, const CodeUnit8* rhs)
{
    return !(lhs == rhs);
}

// =================================================================================================

/**
Returns whether two UTF-8 string are binary different.

\param lhs First UTF-8 string - this has to be null terminated. Can't be 0, otherwise
behavior is undefined as this is not checked (except for assertion in debug builds).

\param rhs Second UTF-8 string.
*/
RED_INLINE Bool operator!=(const CodeUnit8* lhs, const Utf8String& rhs)
{
    return !(lhs == rhs);
}

// =================================================================================================
} // namespace red
// =================================================================================================
