#include "build.h"

#include "ustring/ustringUtil.h"

// =================================================================================================
namespace red {
// =================================================================================================



// =================================================================================================
namespace {
// =================================================================================================

/**
Used by other utility functions, based on ICU's _appendUTF8().

\param c Unicode code point. If this is out of Unicode range then behavior is undefined (the function
doesn't check this).
*/
RED_INLINE CodeUnit8* AppendUTF8(CodeUnit8 *pDest, CodePoint c)
{
    // Note that in spite of CodePoint being signed integer these >> are ok since code points are >= 0
    // (we use negative values as special values in some places but we never store them in strings).

    // it is 0 <= c <= 0x10ffff and not a surrogate if called by a validating function
    if(c <= 0x7f) {
        *pDest++ = static_cast<CodeUnit8>(c);
    }
    else if(c <= 0x7ff) {
        *pDest++ = static_cast<CodeUnit8>((c >> 6) | 0xc0);
        *pDest++ = static_cast<CodeUnit8>((c & 0x3f) | 0x80);
    }
    else if(c <= 0xffff) {
        *pDest++ = static_cast<CodeUnit8>((c >> 12) | 0xe0);
        *pDest++ = static_cast<CodeUnit8>(((c >> 6) & 0x3f) | 0x80);
        *pDest++ = static_cast<CodeUnit8>((c & 0x3f) | 0x80);
    }
    else /* if((uint32_t)(c) <= 0x10ffff) */ {
        *pDest++ = static_cast<CodeUnit8>((c >> 18) | 0xf0);
        *pDest++ = static_cast<CodeUnit8>(((c >> 12) & 0x3f) | 0x80);
        *pDest++ = static_cast<CodeUnit8>(((c >> 6) & 0x3f) | 0x80);
        *pDest++ = static_cast<CodeUnit8>((c & 0x3f) | 0x80);
    }
    return pDest;
}

// =================================================================================================

/**
Masks UTF-8 lead code unit leaving only lower bits that form part of the code point value.

Based on ICU's U8_MASK_LEAD_BYTE.

\param lead Leading code unit.

\param numTrailingCodeUnits Number of trailing code units for specified leading code unit.

\return UTF-8 lead code unit in which only lower bits that form part of the code point value are left.
*/
RED_INLINE CodeUnit8 Utf8MaskLeadCodeUnit(CodeUnit8 lead, Uint32 numTrailingCodeUnits)
{
    CodeUnit8 ret = lead;
    ret &= (1 << (6 - numTrailingCodeUnits)) - 1;
    return ret;
}

// =================================================================================================

/**
Returns code point at specified position and advances pointer to next code point boundary.

Based on ICU's utf8_nextCharSafeBodyPointer().

\param ps (inout) *ps points to after the lead UTF-8 code unit and will be moved to after the last
trailing code unit.

\param lead Lead UTF-8 code unit.

\return Code point at specified position or -1 on invalid sequence.
*/
CodePoint Utf8NextCharSafeBodyPointer(const CodeUnit8** ps, const CodeUnit8* limit, CodeUnit8 lead)
{
    const CodeUnit8* s = *ps;
    CodeUnit8 trail, illegal = 0;
    Uint32 numTrailing = Utf8GetNumTrailingCodeUnits(lead);

    CodePoint cp;

    if((limit - s) >= static_cast<int8_t>(numTrailing)) {
        cp = Utf8MaskLeadCodeUnit(lead, numTrailing);

        // count == 0 for illegally leading trail bytes and the illegal bytes 0xfe and 0xff
        switch(numTrailing) {
            case 5:
            case 4:
                // count >= 4 is always illegal: no more than 3 trail bytes in Unicode's UTF-8
                illegal = 1;
                break;
        
            case 3:
                trail = *s++;
                cp = (cp << 6) | (trail & 0x3f);
                if(cp < 0x110)
                    illegal |= (trail & 0xc0) ^ 0x80;
                else {
                    // code point > 0x10ffff, outside Unicode
                    illegal = 1;
                    break;
                }

            case 2: // fall through
                trail = *s++;
                cp = (cp << 6) | (trail & 0x3f);
                illegal |= (trail & 0xc0) ^ 0x80;

            case 1: // fall through
                trail = *s++;
                cp = (cp << 6) | (trail & 0x3f);
                illegal |= (trail & 0xc0) ^ 0x80;
                break;

            case 0:
                return -1;
        }
    }
    else
        illegal = 1; // too few bytes left

    static const CodePoint utf8_minLegal[4] = { 0, 0x80, 0x800, 0x10000 };

    // correct sequence - all trail bytes have (b7..b6)==(10)?
    // illegal is also set if count >= 4
    RED_ASSERT(numTrailing < sizeof(utf8_minLegal) / sizeof(utf8_minLegal[0]));

    if(illegal || cp < utf8_minLegal[numTrailing] || CodePointIsSurrogate(cp)) {
        // error handling
        // don't go beyond this sequence
        s = *ps;
        while(numTrailing > 0 && s < limit && Utf8CodeUnitIsTrail(*s)) {
            ++s;
            --numTrailing;
        }
        cp = -1;
    }

    *ps = s;
    return cp;
}

// =================================================================================================
} // unnamed namespace
// =================================================================================================

/**
Array in which [i] denotes number of trailing code units for leading code unit with value i.

Based on ICU's utf8_countTrailBytes array.

This table could be replaced on many machines by a few lines of assembler code using an
"index of first 0-bit from msb" instruction and one or two more integer instructions.

For example, on an i386, do something like
  MOV AL, leadByte
  NOT AL         (8-bit, leave b15..b8==0..0, reverse only b7..b0)
  MOV AH, 0
  BSR BX, AX     (16-bit) (BSR: Bit Scan Reverse, scans for a 1-bit, starting from the MSB)
  MOV AX, 6      (result)
  JZ finish      (ZF==1 if leadByte==0xff)
  SUB AX, BX (result)
 finish:

In Unicode, all UTF-8 byte sequences with more than 4 bytes are illegal. Lead bytes above 0xf4 are
illegal. We keep them in this table for skipping long ISO 10646-UTF-8 sequences.
*/
extern const uint8_t utf8NumberOfTrailCodeUnits[256] = {
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,

    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,

    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,

    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,

    2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2,
    3, 3, 3, 3, 3,
    3, 3, 3,    // illegal in Unicode
    4, 4, 4, 4, // illegal in Unicode
    5, 5,       // illegal in Unicode
    0, 0        // illegal bytes 0xfe and 0xff
};

// =================================================================================================

/**
Returns length of UTF-16 string in code units.

Preconditions:
1. str != 0 (checked only in debug)

\param str Null (U+0000) terminated UTF-16 string.
\return Length of str in code units, not including terminating null.
*/
Uint32 UStrLen(const CodeUnit16* str)
{
    RED_ASSERT(str);

    const CodeUnit16* ptr = str;
    while(*ptr)
        ++ptr;
    return (Uint32)(ptr - str);
}

// =================================================================================================

/**
Gets code point at specified index in UTF-16 string.

Based on ICU's U16_GET_UNSAFE.

This function assumes UTF-16 string is well formed, i.e. it has no unpaired surrogates. It's slightly
faster than GetCodePointAtSafer() which doesn't assume this. However, note that the speed difference
occurs only when surrogates are encountered.

Iteration through a string is more efficient with GetCodePointAtAndAdvance()
and GetCodePointAtAndAdvanceSafer() functions.

\param s Well formed UTF-16 string. Result is undefined if string is not well formed.

\param i Offset in string. It may point to code unit that is lead or trail surrogate - in this case,
the other surrogate will be read and supplementary code point will be returned. The result is undefined
if it points to unpaired surrogate (which means that the string is not well formed).

\return Code point at specified index.
*/
CodePoint GetCodePointAt(const CodeUnit16* s, Uint32 i)
{
    CodeUnit16 c = s[i];

    // Code unit is not surrogate.
    if(RED_LIKELY(CodeUnitIsSingle(c)))
        return c;
    // Code unit is surrogate.
    else {
        if(SurrogateCodeUnitIsLead(c))
            return GetSupplementaryCodePoint(c, s[i + 1]);
        else
            return GetSupplementaryCodePoint(s[i - 1], c);
    }
}

// =================================================================================================

/**
Gets code point at specified index in UTF-16 string.

Based on ICU's U16_GET.

This function doesn't assume that UTF-16 string is well formed. Additionally, the function checks for
string boundaries. All this makes it slightly slower than GetCodePointAt(). However, note that the
speed difference occurs only only when surrogates are encountered.

Iteration through a string is more efficient with GetCodePointAtAndAdvance()
and GetCodePointAtAndAdvanceSafer() functions.

\param s UTF-16 string.

\param start Starting string offset (usually 0).

\param i Offset in string. Must satisfy condition: start <= i < length. It may point to code unit that
is lead or trail surrogate - in this case, the other surrogate will be read and supplementary code point
will be returned. If it points to unpaired surrogate then that surrogate is returned as code point.

\param length String length, in code units.

\return Code point at specified index.
*/
CodePoint GetCodePointAtSafer(const CodeUnit16* s, Uint32 start, Uint32 i, Uint32 length)
{
    CodeUnit16 c = s[i];

    // Code unit is not surrogate.
    if(RED_LIKELY(CodeUnitIsSingle(c)))
        return c;
    // Code unit is surrogate.
    else {
        CodePoint r = c;

        CodeUnit16 c2;
        if(SurrogateCodeUnitIsLead(c)) {
            if(i + 1 < length && CodeUnitIsSurrogateTrail(c2 = s[i + 1]))
                r = GetSupplementaryCodePoint(c, c2);
        }
        // surrogate code unit is trail
        else {
            if(i > start && CodeUnitIsSurrogateLead(c2 = s[i - 1]))
                r = GetSupplementaryCodePoint(c2, c);
        }

        return r;
    }
}

// =================================================================================================

/**
Converts UTF-16 string to UTF-8 string.

Based on ICU's u_strToUTF8WithSub().

\param dest (out) Destination buffer. It will be null terminated if there's enough space left for
null character in buffer after converting the string. May be 0 only if destCapacity is also 0.

\param destCapacity Size of destination buffer, in UTF-8 code units. If it's 0 then the function will
only return (via resultLength) the length of the result without writing anything to dest buffer.

\param resultLength (out) Pointer to object that will receive length of result (in UTF-8 code units).
This is always the length of resulting string no matter if destination buffer is big enough for the
result or not. So *resultLength > destCapacity means destination buffer is to small.

\param src Source UTF-16 string. If it's not well formed then illegal sequence will be replaced
by substitution character if it's specified or the function will return RESULT_FAILED.

\param srcLength Length of source string, in UTF-16 code units.

\param subchar Substitution character to use in place of illegal input sequence.
A substitution character can be any valid Unicode code point except for surrogate code points
(note supplementary code points are ok). The recommended value is U+FFFD "REPLACEMENT CHARACTER".
If -1 then the function will return RESULT_FAILED when it encounters illegal input sequence.

\param numSubs (out) Pointer to object that will receive number of substitutions. May be 0.
If substitution character is not specified then this is set to 0. 

\return On success - true. On failure - false. Possible causes are:
1. Illegal argument detected.
2. Illegal sequence in input string - the string is not well formed (Unicode 3.2 forbids surrogate code
points in UTF-8) and substitution character is not specified.
 */
Bool Utf16ToUtf8(CodeUnit8* dest, Uint32 destCapacity, Uint32* resultLength, const CodeUnit16* src, Uint32 srcLength, CodePoint subchar, Uint32* numSubs)
{
    // check args
    if((src == 0 && srcLength != 0) || (dest == 0 && destCapacity > 0) || subchar > 0x10ffff || CodePointIsSurrogate(subchar))
        // illegal arg detected
        return false;

    // reset substitutions counters
    Uint32 numSubstitutions = 0;
    if(numSubs)
        *numSubs = 0;

    CodeUnit8* pDest = dest;

    // compute source string limit and destination buffer limit
    const CodeUnit16* const pSrcLimit = (src != 0)? (src + srcLength) : 0;
    CodeUnit8* const pDestLimit = (pDest != 0)? (pDest + destCapacity) : 0;

    // Process most of the string without constantly checking for pSrcLimit and pDestLimit.
    for(;;) {
        // Compute how much UTF-16 code units will fit destination buffer assuming
        // each UTF-16 code unit needs 3 UTF-8 code units (this is a worst case).
        Uint32 count = (Uint32)(pDestLimit - pDest) / 3;

        // Compute number of UTF-16 code units that still need to be processed.
        srcLength = (Uint32)(pSrcLimit - src);

        if(count > srcLength)
            count = srcLength; // min(remaining dest / 3, remaining src)

        if(count < 3)
            // Too much overhead if we get near the end of the string, continue with the next loop.
            break;

        do {
            // read 16-bit code unit from source string
            CodeUnit16 ch = *src++;

            if(ch <= 0x7f)
                *pDest++ = static_cast<CodeUnit8>(ch);
            else if(ch <= 0x7ff) {
                *pDest++ = static_cast<CodeUnit8>((ch >> 6) | 0xc0);
                *pDest++ = static_cast<CodeUnit8>((ch & 0x3f) | 0x80);
            }
            else if(ch <= 0xd7ff || ch >= 0xe000) {
                *pDest++ = static_cast<CodeUnit8>((ch >> 12) | 0xe0);
                *pDest++ = static_cast<CodeUnit8>(((ch >> 6) & 0x3f) | 0x80);
                *pDest++ = static_cast<CodeUnit8>((ch & 0x3f) | 0x80);
            }
            // ch is a surrogate
            else {

                // Update count because we've encountered a surrogate. This means means we have
                // to read another 16-bit code unit and that we will output four 8-bit code units. 
                --count;

                // If count == 0 then either string ended unexpectedly with unpaired surrogate or there is
                // possibility that we are out of space in destination buffer (note this is only a possibility
                // since we assumed that each UTF-16 code unit needs 3 UTF-8 code units which is a worst case).
                // In both cases we need to break.
                if(count == 0) {
                    --src; // undo ch  =*pSrc++ for the lead surrogate
                    break;  // recompute count
                }

                CodeUnit16 ch2;
                if(SurrogateCodeUnitIsLead(ch) && CodeUnitIsSurrogateTrail(ch2 = *src)) { 
                    ++src;
                    CodePoint cp = GetSupplementaryCodePoint(ch, ch2);

                    // Writing 4 bytes per 2 16-bit code units is ok.
                    // Also, note that in spite of CodePoint being signed integer these >> are ok since code points
                    // are >= 0 (we use negative values as special values in some places but we never store them in strings).
                    *pDest++ = static_cast<CodeUnit8>((cp >> 18) | 0xf0);
                    *pDest++ = static_cast<CodeUnit8>(((cp >> 12) & 0x3f) | 0x80);
                    *pDest++ = static_cast<CodeUnit8>(((cp >> 6) & 0x3f) | 0x80);
                    *pDest++ = static_cast<CodeUnit8>((cp & 0x3f) | 0x80);
                }
                // Handle unpaired surrogate which is an invalid char (Unicode 3.2 forbids surrogate code points in UTF-8).
                else  {
                    // use substitution character if specified
                    if(subchar >= 0) {
                        pDest = AppendUTF8(pDest, subchar);
                        ++numSubstitutions;
                        // If substitution character is from BMP then we have to increment count because we unnecessarily
                        // decremented it after we encountered surrogate (we expected two 16-bit code units to be used).
                        if(GetNumCodeUnits16(subchar) == 1)
                            ++count;
                    }
                    // return error
                    else
                        return false;
                }
            }
        } while(--count > 0);
    }

    Uint32 reqLength = 0;

    // Process remaining part of string but this time check pSrcLimit and pDestLimit.
    while(src < pSrcLimit) {

        // read 16-bit code unit from source string
        CodeUnit16 ch = *src++;

        if(ch <= 0x7f) {
            if(pDest < pDestLimit)
                *pDest++ = static_cast<CodeUnit8>(ch);
            else {
                // not enough space in destination buffer
                reqLength = 1;
                break;
            }
        }
        else if(ch <= 0x7ff) {
            if((pDestLimit - pDest) >= 2) {
                *pDest++ = static_cast<CodeUnit8>((ch >> 6) | 0xc0);
                *pDest++ = static_cast<CodeUnit8>((ch & 0x3f) | 0x80);
            }
            else {
                // not enough space in destination buffer
                reqLength = 2;
                break;
            }
        }
        else if(ch <= 0xd7ff || ch >= 0xe000) {
            if((pDestLimit - pDest) >= 3) {
                *pDest++ = static_cast<CodeUnit8>((ch >> 12) | 0xe0);
                *pDest++ = static_cast<CodeUnit8>(((ch >> 6) & 0x3f) | 0x80);
                *pDest++ = static_cast<CodeUnit8>((ch & 0x3f) | 0x80);
            }
            else {
                // not enough space in destination buffer
                reqLength = 3;
                break;
            }
        }
        // ch is a surrogate
        else {
            CodePoint cp;
            CodeUnit16 ch2;
            if(SurrogateCodeUnitIsLead(ch) && src < pSrcLimit && CodeUnitIsSurrogateTrail(ch2 = *src)) { 
                ++src;
                cp = GetSupplementaryCodePoint(ch, ch2);
            }
            // Invalid char encountered and we have substitution char to use.
            else if(subchar >= 0) {
                cp = subchar;
                ++numSubstitutions;
            }
            // Invalid char encountered and we don't have substitution char to use.
            else
                // invalid char found (Unicode 3.2 forbids surrogate code points in UTF-8)
                return false;

            Uint32 length = GetNumCodeUnits8(cp);
            if(static_cast<Uint32>(pDestLimit - pDest) >= length)
                // convert and append
                pDest = AppendUTF8(pDest, cp);
            else {
                // not enough space in destination buffer
                reqLength = length;
                break;
            }
        }
    }

    // We're out of space in destination buffer but we continue processing remaining part
    // of source string to be able to report length of UTF-8 as if everything went ok.
    while(src < pSrcLimit) {
        CodeUnit16 ch = *src++;
        if(ch <= 0x7f)
            ++reqLength;
        else if(ch <= 0x7ff)
            reqLength += 2;
        else if(!CodeUnitIsSurrogate(ch))
            reqLength += 3;
        else if(SurrogateCodeUnitIsLead(ch) && src < pSrcLimit && CodeUnitIsSurrogateTrail(*src)) {
            ++src;
            reqLength += 4;
        }
        // Invalid char encountered and we have substitution char to use.
        else if(subchar >= 0) {
            reqLength += GetNumCodeUnits8(subchar);
            ++numSubstitutions;
        }
        // Invalid char encountered (Unicode 3.2 forbids surrogate code
        // points in UTF-8) and we don't have substitution char to use.
        else
            return false;
    }

    reqLength += (Uint32)(pDest - dest);

    if(numSubs)
        *numSubs = numSubstitutions;

    if(resultLength)
        *resultLength = reqLength;

    // null terminate the buffer if there is space for this
    if(reqLength < destCapacity)
        dest[reqLength] = 0;

    return true;
}

// =================================================================================================

/**
Converts UTF-8 string to UTF-16 string.

Based on ICU's u_strFromUTF8WithSub().

\param dest (out) Destination buffer. It will be null terminated (U+0000) if there's enough space left for
null character in buffer after converting the string. May be 0 only if destCapacit is also 0.

\param destCapacity Size of destination buffer, in 16-bit code units. If it's 0 then the function will
only return (via resultLength) the length of the result without writing anything to dest buffer.

\param resultLength (out) Pointer to object that will receive length of result (in 16-bit code units).
This is always the length of resulting string no matter if destination buffer is big enough for the
result or not. So *resultLength > destCapacity means destination buffer is to small.

\param src Source UTF-8 string.  If it's not well formed then illegal sequence will be replaced
by substitution character if it's specified or the function will return RESULT_FAILED.

\param srcLength Length of source string, in UTF-8 code units.

\param subchar Substitution character to use in place of illegal input sequence.
A substitution character can by any valid Unicode code point except for surrogate code points
(note supplementary code points are ok). The recommended value is U+FFFD "REPLACEMENT CHARACTER".
If -1 then the function will return RESULT_FAILED when it encounters illegal input sequence.

\param numSubs (out) Pointer to object that will receive number of substitutions. May be 0.
If substitution character is not specified then this is set to 0.

\return On success - true. On failure - false. Possible causes are:
1. Illegal argument detected.
2. Illegal sequence in input string - the string is not well formed and substitution character is not specified.
*/
Bool Utf8ToUtf16(CodeUnit16* dest, Uint32 destCapacity, Uint32* resultLength, const CodeUnit8* src, Uint32 srcLength, CodePoint subchar, Uint32* numSubs)
{
    // check args
    if((src == 0 && srcLength != 0) || (dest == 0 && destCapacity > 0) || subchar > 0x10ffff || CodePointIsSurrogate(subchar))
        return false;

    // reset substitutions counters
    Uint32 numSubstitutions = 0;
    if(numSubs)
        *numSubs = 0;

    const CodeUnit8* pSrc = src;
    CodeUnit16* pDest = dest;

    CodeUnit16* const pDestLimit = dest + destCapacity;
    const CodeUnit8* const pSrcLimit = pSrc + srcLength;

    // Inline processing of UTF-8 byte sequences:
    // Byte sequences for the most common characters are handled inline in the conversion loops. In order to
    // reduce the path lengths for those characters, the tests are arranged in a kind of binary search.
    // ASCII (<= 0x7f) is checked first, followed by the dividing point between 2- and 3-byte sequences (0xe0).
    // The 3-byte branch is tested first to speed up CJK text. The compiler should combine the subtractions
    // for the two tests for 0xe0. Each branch then tests for the other end of its range.

    // Faster loop without ongoing checking for pSrcLimit and pDestLimit.
    for(;;) {
        // Compute how many UTF-16 code units can fit in space left in destination buffer.
        Uint32 count = (Uint32)(pDestLimit - pDest);

        // Compute minimum number of UTF-16 code units that can be produced by the rest of UTF-8 source string
        // (this would be the case in which each UTF-16 code unit is produced from three UTF-8 code units).
        srcLength = (Uint32)(pSrcLimit - pSrc) / 3;
        
        if(count > srcLength)
            count = srcLength; // min(remaining dest, remaining src/3)
        
        if(count < 3)
            // Too much overhead if we get near the end of the string, continue with the next loop.
            break;

        do {
            CodePoint ch = *pSrc;

            if(ch <= 0x7f) {
                *pDest++ = static_cast<CodeUnit16>(ch);
                ++pSrc;
            }
            else {
                if(ch > 0xe0) {
                    // handle U+1000..U+CFFF inline
                    CodeUnit8 t1, t2;
                    if(ch <= 0xec && (t1 = static_cast<CodeUnit8>(pSrc[1] - 0x80)) <= 0x3f && (t2 = static_cast<CodeUnit8>(pSrc[2] - 0x80)) <= 0x3f) {
                        // no need for (ch & 0xf) because the upper bits are truncated after << 12 in the cast to CodeUnit16
                        *pDest++ = static_cast<CodeUnit16>((ch << 12) | (t1 << 6) | t2);
                        pSrc += 3;
                        continue;
                    }
                }
                else if(ch < 0xe0) {
                    // handle U+0080..U+07FF inline
                    CodeUnit8 t1;
                    if(ch >= 0xc2 && (t1 = static_cast<CodeUnit8>(pSrc[1] - 0x80)) <= 0x3f) {
                        *pDest++ = static_cast<CodeUnit16>(((ch & 0x1f) << 6) | t1);
                        pSrc += 2;
                        continue;
                    }
                }

                if(ch >= 0xf0 || subchar > 0xffff) {
                    // We may read up to six bytes and write up to two UTF-16 code units,
                    // which we didn't account for with computing count, so we adjust it here.
                    if(--count == 0)
                        break;
                }

                // function call for "complicated" and error cases
                ++pSrc; // continue after the lead byte
                ch = Utf8NextCharSafeBodyPointer(&pSrc, pSrcLimit, static_cast<CodeUnit8>(ch));
                
                if(ch < 0 && (++numSubstitutions, ch = subchar) < 0)
                    // invalid char found
                    return false;
                else if(ch <= 0xFFFF)
                    *(pDest++) = static_cast<CodeUnit16>(ch);
                else {
                    *(pDest++) = GetLeadSurrogate(ch);
                    *(pDest++) = GetTrailSurrogate(ch);
                }
            }
        } while(--count > 0);
    }

    Uint32 reqLength = 0;

    // Process remaining part of string but this time check pSrcLimit and pDestLimit.
    while((pSrc < pSrcLimit) && (pDest < pDestLimit)) {
        CodePoint ch = *pSrc;
        
        if(ch <= 0x7f) {
            *pDest++ = static_cast<CodeUnit16>(ch);
            ++pSrc;
        }
        else {
            if(ch > 0xe0) {
                // handle U+1000..U+CFFF inline
                CodeUnit8 t1, t2;
                if(ch <= 0xec && ((pSrcLimit - pSrc) >= 3) && (t1 = static_cast<CodeUnit8>(pSrc[1] - 0x80)) <= 0x3f && (t2 = static_cast<CodeUnit8>(pSrc[2] - 0x80)) <= 0x3f) {
                    // no need for (ch & 0xf) because the upper bits are truncated after << 12 in the cast to CodeUnit16
                    *pDest++ = static_cast<CodeUnit16>((ch << 12) | (t1 << 6) | t2);
                    pSrc += 3;
                    continue;
                }
            }
            else if(ch < 0xe0) {
                // handle U+0080..U+07FF inline
                CodeUnit8 t1;
                if(ch >= 0xc2 && ((pSrcLimit - pSrc) >= 2) && (t1 = static_cast<CodeUnit8>(pSrc[1] - 0x80)) <= 0x3f) {
                    *pDest++ = static_cast<CodeUnit16>(((ch & 0x1f) << 6) | t1);
                    pSrc += 2;
                    continue;
                }
            }

            // function call for "complicated" and error cases
            ++pSrc; // continue after the lead byte
            ch = Utf8NextCharSafeBodyPointer(&pSrc, pSrcLimit, static_cast<CodeUnit8>(ch));

            if(ch < 0 && (++numSubstitutions, ch = subchar) < 0)
                // invalid char found
                return false;
            else if(ch <= 0xFFFF)
                *(pDest++) = (CodeUnit16)ch;
            else {
                *(pDest++) = GetLeadSurrogate(ch);
                if(pDest < pDestLimit)
                    *(pDest++) = GetTrailSurrogate(ch);
                else {
                    reqLength++;
                    break;
                }
            }
        }
    }

    // do not fill the dest buffer just count the 16-bit code units needed
    while(pSrc < pSrcLimit) {
        CodePoint ch = *pSrc;

        if(ch <= 0x7f) {
            reqLength++;
            ++pSrc;
        }
        else {

            if(ch > 0xe0) {
                // handle U+1000..U+CFFF inline
                if(ch <= 0xec && ((pSrcLimit - pSrc) >= 3) && static_cast<CodeUnit8>(pSrc[1] - 0x80) <= 0x3f && static_cast<CodeUnit8>(pSrc[2] - 0x80) <= 0x3f) {
                    reqLength++;
                    pSrc += 3;
                    continue;
                }
            }
            else if(ch < 0xe0) {
                // handle U+0080..U+07FF inline
                if(ch >= 0xc2 && ((pSrcLimit - pSrc) >= 2) && static_cast<CodeUnit8>(pSrc[1] - 0x80) <= 0x3f) {
                    reqLength++;
                    pSrc += 2;
                    continue;
                }
            }

            // function call for "complicated" and error cases
            ++pSrc; // continue after the lead byte
            ch = Utf8NextCharSafeBodyPointer(&pSrc, pSrcLimit, static_cast<CodeUnit8>(ch));

            if(ch < 0 && (++numSubstitutions, ch = subchar) < 0)
                // invalid char found
                return false;
            reqLength += GetNumCodeUnits16(ch);
        }
    }

    reqLength += (Uint32)(pDest - dest);

    if(numSubs)
        *numSubs = numSubstitutions;

    if(resultLength)
        *resultLength = reqLength;

    // null terminate the buffer if there is space for this
    if(reqLength < destCapacity)
        dest[reqLength] = 0;

    return true;
}

// =================================================================================================

/**
Returns whether code point represents line terminator.

\param codePoint Code point to check.
\return True - code point is line terminator, false - otherwise.

All code points defined by Unicode as line terminators are recognized by this function.
*/
Bool IsNewline(CodePoint codePoint)
{
    switch(codePoint) {
        case 0x000d: // CR
        case 0x000a: // LF
        case 0x0085: // NEL
        case 0x000b: // VT
        case 0x000c: // FF
        case 0x2028: // LS
        case 0x2029: // PS
            return true;
        default:
            return false;
    }
}

// =================================================================================================

/**
Counts number of lines in string.

\param str Pointer to string. May be 0.
\param size Size of string, in code units. -1 means "until end of null terminated string". May be 0.
\return Number of lines in string. If size == 0 then 0 is returned.

All code points defined by Unicode as line terminators are recognized by this function.

CRLF is treated as one line terminator.
*/
Uint32 CountLines(const CodeUnit16* str, Uint32 size /* = -1 */)
{
    if(size == -1 && str)
        size = UStrLen(str);

    if(size == 0 || !str)
        return 0;

    Uint32 numLines = 1;
    Bool lastCodePointWasCR = false;

    for(Uint32 i = 0; i < size; ) {
        CodePoint cp = GetCodePointAtAndAdvanceSafer(str, i, size);
        if(IsNewline(cp)) {
            if(!(lastCodePointWasCR && IsLF(cp)))
                ++numLines;
            lastCodePointWasCR = IsCR(cp);
        }
        else
            lastCodePointWasCR = false;
    }

    return numLines;
}

// =================================================================================================
} // namespace red
// =================================================================================================
