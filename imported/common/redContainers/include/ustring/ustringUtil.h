#pragma once

#include "utypes.h"

// =================================================================================================
namespace red {
// =================================================================================================

Bool CodePointIsNoncharacter(CodePoint codePoint);
Bool CodePointIsCharacter(CodePoint codePoint);

Bool CodePointIsBMP(CodePoint codePoint);
Bool CodePointIsSupplementary(CodePoint codePoint);

Bool CodePointIsSurrogate(CodePoint codePoint);
Bool CodePointIsSurrogateLead(CodePoint codePoint);
Bool CodePointIsSurrogateTrail(CodePoint codePoint);

Bool SurrogateCodePointIsLead(CodePoint surrogateCodePoint);
Bool SurrogateCodePointIsTrail(CodePoint surrogateCodePoint);

Uint32 UStrLen(const CodeUnit16* str);

Uint32 GetNumCodeUnits16(CodePoint codePoint);

Bool CodeUnitIsSingle(CodeUnit16 codeUnit);
Bool CodeUnitIsSurrogate(CodeUnit16 codeUnit);

Bool CodeUnitIsSurrogateLead(CodeUnit16 codeUnit);
Bool CodeUnitIsSurrogateTrail(CodeUnit16 codeUnit);

Bool SurrogateCodeUnitIsLead(CodeUnit16 surrogateCodeUnit);
Bool SurrogateCodeUnitIsTrail(CodeUnit16 surrogateCodeUnit);

template <typename T0, typename T1>
CodePoint GetSupplementaryCodePoint(T0 lead, T1 trail);

CodeUnit16 GetLeadSurrogate(CodePoint supplementaryCodePoint);
CodeUnit16 GetTrailSurrogate(CodePoint supplementaryCodePoint);

CodePoint GetCodePointAt(const CodeUnit16* s, Uint32 i);
CodePoint GetCodePointAtSafer(const CodeUnit16* s, Uint32 start, Uint32 i, Uint32 length);

void AdjustToCodePoint(CodeUnit16* s, Uint32& i);
void AdjustToCodePointSafer(CodeUnit16* s, Uint32 start, Uint32& i);

void Advance(const CodeUnit16* s, Uint32& i);
void AdvanceSafer(const CodeUnit16*, Uint32& i, Uint32 length);

void BackAdvance(const CodeUnit16* s, Uint32& i);
void BackAdvanceSafer(const CodeUnit16* s, Uint32 start, Uint32& i);

CodePoint GetCodePointAtAndAdvance(const CodeUnit16* s, Uint32& i);
CodePoint GetCodePointAtAndAdvanceSafer(const CodeUnit16* s, Uint32& i, Uint32 length);

CodePoint BackAdvanceAndGetCodePointAt(const CodeUnit16* s, Uint32& i);
CodePoint BackAdvanceAndGetCodePointAtSafer(const CodeUnit16* s, Uint32 start, Uint32& i);

Uint32 GetNumCodeUnits8(CodePoint codePoint);
Uint32 Utf8GetNumTrailingCodeUnits(CodeUnit8 lead);
Bool Utf8CodeUnitIsSingle(CodeUnit8 codeUnit);
Bool Utf8CodeUnitIsLead(CodeUnit8 codeUnit);
Bool Utf8CodeUnitIsTrail(CodeUnit8 codeUnit);

Bool Utf16ToUtf8(CodeUnit8* dest, Uint32 destCapacity, Uint32* resultLength, const CodeUnit16* src, Uint32 srcLength, CodePoint subchar, Uint32* numSubs);
Bool Utf8ToUtf16(CodeUnit16* dest, Uint32 destCapacity, Uint32* resultLength, const CodeUnit8* src, Uint32 srcLength, CodePoint subchar, Uint32* numSubs);

Bool IsNewline(CodePoint codePoint);
Bool IsCR(CodePoint codePoint);
Bool IsLF(CodePoint codePoint);
Uint32 CountLines(const CodeUnit16* str, Uint32 size = static_cast<Uint32>(-1));
Bool IsASCIILetter( CodePoint codePoint );

// =================================================================================================
// implementation
// =================================================================================================

/**
Returns whether code point is Unicode noncharacter.

Based on ICU's U_IS_UNICODE_NONCHAR.

\param codePoint Code point to check.

\return True - code point is noncharacter, false - code point is not noncharacter (this doesn't mean
it's character - it may be a character but it may also be surrogate or code point outside Unicode
range; see CodePointIsCharacter() for more info).
*/
RED_INLINE Bool CodePointIsNoncharacter(CodePoint codePoint)
{
    return (codePoint >= 0xfdd0
            && (static_cast<uint32_t>(codePoint) <= 0xfdef || (codePoint & 0xfffe) == 0xfffe)
            && static_cast<uint32_t>(codePoint) <= 0x10ffff);
}

// =================================================================================================

/**
Returns whether code point is a Unicode code point (0..U+10ffff) that can be assigned a character.

Based on ICU's U_IS_UNICODE_CHAR.

Following code points are not considered characters:
1. Code points outside Unicode range (> 0x10ffff).
2. Surrogate code points.
3. Noncharacter code points.

\param codePoint Code point to check.

\return True - code point is a character, false - code point is not a character (this doesn't mean
it's noncharacter - it may be noncharacter, but it may also be surrogate or code point outside
Unicode range; see CodePointIsNoncharacter() for more info).
*/
RED_INLINE Bool CodePointIsCharacter(CodePoint codePoint)
{
    return (static_cast<uint32_t>(codePoint) < 0xd800           // 0xd800 - start of surrogate range
            || (static_cast<uint32_t>(codePoint) > 0xdfff       // 0xdfff - end of surrogate range
                && static_cast<uint32_t>(codePoint) <= 0x10ffff // 0x10ffff - end of Unicode range
                && !CodePointIsNoncharacter(codePoint)));
}

// =================================================================================================

/**
Returns whether code point belongs to basic multilingual plane (U+0000..U+ffff).

Based on ICU's U_IS_BMP.

\param codePoint Code point to check.
\return True - code point belongs to BMP, false - code point doesn't belong to BMP (this doesn't
necessarily mean it belongs to one of supplementary planes - it may, but it may also be outside
Unicode range).
*/
RED_INLINE Bool CodePointIsBMP(CodePoint codePoint)
{
    return static_cast<uint32_t>(codePoint) <= 0xffff;
}

// =================================================================================================

/**
Returns whether code point is a supplementary code point (U+10000..U+10ffff).

Based on ICU's U_IS_SUPPLEMENTARY.

\param codePoint Code point to check.
\return True - code point is supplementary code point, false - code point is not supplementary (this
doesn't necessarily mean it belongs to BMP - it may, but it may also be outside Unicode range).
*/
RED_INLINE Bool CodePointIsSupplementary(CodePoint codePoint)
{
    return static_cast<uint32_t>(codePoint - 0x10000) <= 0xfffff;
}

// =================================================================================================

/**
Returns whether code point is a surrogate (U+d800..U+dfff).

Based on ICU's U_IS_SURROGATE.

\param codePoint Code point to check.
\return True - code point is surrogate. Otherwise, false.
*/
RED_INLINE Bool CodePointIsSurrogate(CodePoint codePoint)
{
    return (codePoint & 0xfffff800) == 0xd800;
}

// =================================================================================================

/**
Returns whether code point is surrogate lead (U+d800..U+dbff).

Based on ICU's U_IS_LEAD.

\param codePoint Code point to check.
\return True - code point is surrogate lead. Otherwise, false.
*/
RED_INLINE Bool CodePointIsSurrogateLead(CodePoint codePoint)
{
    return (codePoint & 0xfffffc00) == 0xd800;
}

// =================================================================================================

/**
Returns whether code point is surrogate trail (U+dc00..U+dfff).

Based on ICU's U_IS_TRAIL.

\param codePoint Code point to check.
\return True - code point is surrogate trail. Otherwise, false.
*/
RED_INLINE Bool CodePointIsSurrogateTrail(CodePoint codePoint)
{
    return (codePoint & 0xfffffc00) == 0xdc00;
}

// =================================================================================================

/**
Returns whether surrogate code point is surrogate lead.

Based on ICU's U_IS_SURROGATE_LEAD.

\param surrogateCodePoint Surrogate code point to check. Must be surrogate - the function doesn't check this.
\return True - surrogate code point is surrogate lead, otherwise it's surrogate trail.
*/
RED_INLINE Bool SurrogateCodePointIsLead(CodePoint surrogateCodePoint)
{
    return (surrogateCodePoint & 0x400) == 0;
}

// =================================================================================================

/**
Returns whether surrogate code point is surrogate trail.

Based on ICU's U_IS_SURROGATE_TRAIL.

\param surrogateCodePoint Surrogate code point to check. Must be surrogate - the function doesn't check this.
\return True - surrogate code point is surrogate trail, otherwise it's surrogate lead.
*/
RED_INLINE Bool SurrogateCodePointIsTrail(CodePoint surrogateCodePoint)
{
    return !SurrogateCodePointIsLead(surrogateCodePoint);
}

// =================================================================================================

/**
Returns how may 16-bit code units (CodeUnit16) are used to encode Unicode code point (it's  or 2).

Based on ICU's U16_LENGTH.

\param codePoint Code point to check. Must be Unicode code point (U+0000..U+10ffff), otherwise result
is undefined since the function doesn't check this.

\return Number of 16-bit code units (CodeUnit16) used to encode Unicode code point (it's 1 or 2).
*/
RED_INLINE Uint32 GetNumCodeUnits16(CodePoint codePoint)
{
    return static_cast<uint32_t>(codePoint) <= 0xffff ? 1 : 2;
}

// =================================================================================================

/**
Returns whether this code unit alone encodes a code point.

Based on ICU's U16_IS_SINGLE.

\param codeUnit Code unit to check.

\return True - this code unit alone encodes a code point, false - code unit is surrogate.
*/
RED_INLINE Bool CodeUnitIsSingle(CodeUnit16 codeUnit)
{
    return !CodeUnitIsSurrogate(codeUnit);
}

// =================================================================================================

/**
Returns whether code unit is surrogate (U+d800..U+dfff).

Based on ICU's U16_IS_SURROGATE.

\param codeUnit Code unit to check.

\return True - code unit is surrogate. Otherwise, false.
*/
RED_INLINE Bool CodeUnitIsSurrogate(CodeUnit16 codeUnit)
{
    return (codeUnit & 0xf800) == 0xd800;
}

// =================================================================================================

/**
Returns whether code unit is surrogate lead (U+d800..U+dbff).

Based on ICU's U16_IS_LEAD.

\param codeUnit Code unit to check.

\param True - code unit is surrogate lead. Otherwise, false.
*/
RED_INLINE Bool CodeUnitIsSurrogateLead(CodeUnit16 codeUnit)
{
    return (codeUnit & 0xfc00) == 0xd800;
}

// =================================================================================================

/**
Returns whether code unit is surrogate trail (U+dc00..U+dfff).

Based on ICU's U16_IS_TRAIL.

\param codeUnit Code unit to check.

\param True - code unit is surrogate trail. Otherwise, false.
*/
RED_INLINE Bool CodeUnitIsSurrogateTrail(CodeUnit16 codeUnit)
{
    return (codeUnit & 0xfc00) == 0xdc00;
}

// =================================================================================================

/**
Returns whether surrogate code unit is surrogate lead.

Based on ICU's U16_IS_SURROGATE_LEAD.

\param surrogateCodeUnit Surrogate code unit to check. Must be surrogate - the function doesn't check this.

\return True - surrogate code unit is surrogate lead, otherwise it's surrogate trail.
*/
RED_INLINE Bool SurrogateCodeUnitIsLead(CodeUnit16 surrogateCodeUnit)
{
    return (surrogateCodeUnit & 0x400) == 0;
}

// =================================================================================================

/**
Returns whether surrogate code unit is surrogate trail.

Based on ICU's U16_IS_SURROGATE_TRAIL.

\param surrogateCodeUnit Surrogate code unit to check. Must be surrogate - the function doesn't check this.

\return True - surrogate code unit is surrogate trail, otherwise it's surrogate lead.
*/
RED_INLINE Bool SurrogateCodeUnitIsTrail(CodeUnit16 surrogateCodeUnit)
{
    return !SurrogateCodeUnitIsLead(surrogateCodeUnit);
}

// =================================================================================================

/**
Constructs supplementary code point (U+10000..U+10ffff) from lead and trail surrogates.

Based on ICU's U16_GET_SUPPLEMENTARY.

\param lead Lead surrogate (either CodePoint or CodeUnit16). Must be lead surrogate, otherwise
result is undefined since the function doesn't check this.

\param trail Trail surrogate (either CodePoint or CodeUnit16). Must be trail surrogate, otherwise
result is undefined since the function doesn't check this.

\return Supplementary code point.

\tparam T0 CodePoint or CodeUnit16.

\tparam T1 CodePoint or CodeUnit16.
*/
template <typename T0, typename T1>
RED_INLINE CodePoint GetSupplementaryCodePoint(T0 lead, T1 trail)
{
    return (static_cast<CodePoint>(lead) << 10UL) + static_cast<CodePoint>(trail) - ((0xd800 << 10UL) + 0xdc00 - 0x10000);
}

// =================================================================================================

/**
Gets code unit that is lead surrogate for supplementary code point.

Based on ICU's U16_LEAD.

\param supplementaryCodePoint Supplementary code point. Must be supplementary, otherwise result is
undefined since the function doesn't check this.

\return Code unit that is lead surrogate (0xd800..0xdbff) for supplementary code point.
*/
RED_INLINE CodeUnit16 GetLeadSurrogate(CodePoint supplementaryCodePoint)
{
    return static_cast<CodeUnit16>((supplementaryCodePoint >> 10) + 0xd7c0);
}

// =================================================================================================

/**
Gets code unit that is trail surrogate for supplementary code point.

Based on ICU's U16_TRAIL.

\param supplementaryCodePoint Supplementary code point. Must be supplementary, otherwise result is
undefined since the function doesn't check this.

\return Code unit that is trail surrogate (U+dc00..U+dfff) for supplementary code point.
*/
RED_INLINE CodeUnit16 GetTrailSurrogate(CodePoint supplementaryCodePoint)
{
    return static_cast<CodeUnit16>((supplementaryCodePoint & 0x3ff) | 0xdc00);
}

// =================================================================================================

/**
Adjusts offset to code point boundary (at the start of a code point).

Based on ICU's U16_SET_CP_START_UNSAFE.

This functions assumes UTF-16 string is well formed. This is why it's slightly faster
than AdjustToCodePointSafer(). However, note that the speed difference occurs only when
surrogates are encountered.

\param s Well formed UTF-16 string. Otherwise, result is undefined.

\param i (inout) Offset in string. If it points to trail surrogate then it is decremented. Otherwise,
it is not modified.
*/
RED_INLINE void AdjustToCodePoint(CodeUnit16* s, Uint32& i)
{
    if(CodeUnitIsSurrogateTrail(s[i]))
        --i;
}

// =================================================================================================

/**
Adjusts offset to code point boundary (at the start of a code point).

Based on ICU's U16_SET_CP_START.

This function doesn't assume UTF-16 is well formed. It handles unpaired surrogates. Additionally, it
checks string boundaries. This is why it's slightly slower than AdjustToCodePoint(). However, note
that the speed difference occurs only when surrogate are encountered.

\param s UTF-16 string.

\param start Starting string offset (usually 0).

\param i (inout) Offset in string. If it points to trail surrogate then it is decremented. Otherwise,
it is not modified. It must satisfy condition: start <= i.
*/
RED_INLINE void AdjustToCodePointSafer(CodeUnit16* s, Uint32 start, Uint32& i)
{
    if(CodeUnitIsSurrogateTrail(s[i]) && i > start && CodeUnitIsSurrogateLead(s[i - 1]))
        --i;
}

// =================================================================================================

/**
Advances string offset to next code point boundary.

Based on ICU's U16_FWD_1_UNSAFE.

This function assumes UTF-16 string is well formed. This is why it's slightly faster than
AdvanceSafer(). However, note that the speed difference occurs only when surrogates are
encountered.

String offset may point to code point boundary or to trail surrogate - in either case it will be
advanced to next code point boundary. Unpaired surrogates mean UTF-16 string is not well formed
and the behavior is undefined.

\param s Well formed UTF-16 string. If string is not well formed then result is undefined.

\param i (inout) String offset.
*/
RED_INLINE void Advance(const CodeUnit16* s, Uint32& i)
{
    if(CodeUnitIsSurrogateLead(s[i++]))
        ++i;
}

// =================================================================================================

/**
Advances string offset to next code point boundary.

Based on ICU's U16_FWD_1.

This function doesn't assume UTF-16 string is well formed. It handles unpaired surrogates and checks
string boundaries. This is why it's slightly slower than Advance(). However, not the the speed
difference occurs only when surrogates are encountered.

String offset may point to code point boundary or to trail surrogate - in either case it will be
advanced to next code point boundary. Unpaired surrogates are treated as surrogate code points.

\param s UTF-16 string.

\param i (inout) String offset. Initial value must satisfy condition: i < length.

\param length String length, in code units.
*/
RED_INLINE void AdvanceSafer(const CodeUnit16* s, Uint32& i, Uint32 length)
{
    if(CodeUnitIsSurrogateLead(s[i++]) && i < length && CodeUnitIsSurrogateTrail(s[i]))
        ++i;
}

// =================================================================================================

/**
Back advances string offset to closest code point boundary.

Based on ICU's U16_BACK_1_UNSAFE.

This function assumes UTF-16 string is well formed. This is why it's slightly faster than
BackAdvanceSafer(). However, note that the speed difference occurs only when surrogates
are encountered.

If string offset points at code point boundary then it will be back advanced to boundary of previous
code point. If string offset points at trail surrogate then it will be back advanced to boundary of
current code point (i.e. to lead surrogate). Unpaired surrogates mean UTF-16 string is not well formed
and the behavior is undefined.

\param s Well formed UTF-16 string. If string is not well formed then behavior is undefined.

\param i (inout) String offset. Initial value may point to one-past-last code unit of the string.
*/
RED_INLINE void BackAdvance(const CodeUnit16* s, Uint32& i)
{
    if(CodeUnitIsSurrogateTrail(s[--i]))
        --i;
}

// =================================================================================================

/**
Back advances string offset to closest code point boundary.

Based on ICU's U16_BACK_1.

This function doesn't assume UTF-16 string is well formed. It handles unpaired surrogates and it
also checks string boundaries. This is why it's slightly slower than BackAdvance(). However, note
that the speed difference occurs only when surrogates are encountered.

If string offset points at code point boundary then it will be back advanced to boundary of previous
code point. If string offset points at trail surrogate then it will be back advanced to boundary of
current code point (i.e. to lead surrogate). Unpaired surrogates are treated as surrogate code points.

\param s UTF-16 string.

\param limit String limit offset - string offset will not be back advanced beyond limit. Usually 0.

\param i (inout) String offset. Initial value must satisfy condition: i > limit. Otherwise, behavior
is undefined since the function doesn't check this condition. Initial value may point to one-past-last
code unit of the string.
*/
RED_INLINE void BackAdvanceSafer(const CodeUnit16* s, Uint32 limit, Uint32& i)
{
    if(CodeUnitIsSurrogateTrail(s[--i]) && i > limit && CodeUnitIsSurrogateLead(s[i - 1]))
        --i;
}

// =================================================================================================

/**
Gets code point at specified index in UTF-16 string and advances the index.

Based on ICU's U16_NEXT_UNSAFE.

This function assumes UTF-16 string is well formed. This is why it's slightly faster than
GetCodePointAtAndAdvanceSafer() function. However, note that the speed difference occurs only
when surrogates are encountered.

\param s Well formed UTF-16 string. Result is undefined if string is not well formed. Actually, only
unpaired lead surrogates cause undefined behavior. Unpaired trail surrogates are handled fine - they
are read and returned as code points and then the index is properly advanced.

\param i (inout) Offset in string. If it points to lead surrogate then the function will also read
trail surrogate and return supplementary code point. If it points to unpaired lead surrogate then the
result is undefined. If it points to trail surrogate then that surrogate will be returned as code point
(i.e. the function will not read lead surrogate to construct supplementary code point). 

\return Code point at specified index (before incrementation).
*/
RED_INLINE CodePoint GetCodePointAtAndAdvance(const CodeUnit16* s, Uint32& i)
{
    CodePoint c = s[i++];

    if(CodeUnitIsSurrogateLead(static_cast<CodeUnit16>(c)))
        c = GetSupplementaryCodePoint(c, s[i++]);

    return c;
}

// =================================================================================================

/**
Gets code point at specified index in UTF-16 string and advances the index.

Based on ICU's U16_NEXT.

This function doesn't assume UTF-16 string is well formed - it handles unpaired surrogates. It also
checks for string boundaries. This is why it's slightly slower than GetCodePointAtAndAdvance().
However, note that the speed difference occurs only when surrogates are encountered.

\param s UTF-16 string.

\param i (inout) Offset in string. Must satisfy condition: i < length. If it points to code unit that
is lead surrogate, then the function will also read trail surrogate and return supplementary code point.
If it points to trail surrogate or unpaired lead surrogate then that surrogate will be returned as code
point.

\param length String length, in code units.

\return Code point at specified index (before incrementation).
*/
RED_INLINE CodePoint GetCodePointAtAndAdvanceSafer(const CodeUnit16* s, Uint32& i, Uint32 length)
{
    CodePoint c = s[i++];

    if(CodeUnitIsSurrogateLead(static_cast<CodeUnit16>(c))) {
        CodeUnit16 c2;
        if(i < length && CodeUnitIsSurrogateTrail(c2 = s[i])) {
            ++i;
            c = GetSupplementaryCodePoint(c, c2);
        }
    }

    return c;
}

// =================================================================================================

/**
Back advances the index to previous code point boundary and gets code point at that position.

Based on ICU's U16_PREV_UNSAFE.

This function assumes UTF-16 string is well formed. This is why it's slightly faster than
BackAdvanceAdnGetCodePointAtSafer() function. However, note that the speed difference occurs
only when surrogates are encountered.

\param s Well formed UTF-16 string. If string is not well formed, result is undefined (actually,
only unpaired trail surrogates will result in undefined behavior).

\param i Offset in string. May point to one-past-last code unit. If it points behind code unit
that is a trail surrogate, the function will also read lead surrogate and return supplementary
code point. If it points behind unpaired trail surrogate then result is undefined. If it points
behind unpaired lead surrogate then that surrogate will be returned as code point.

\return Code point at decremented index.
*/
RED_INLINE CodePoint BackAdvanceAndGetCodePointAt(const CodeUnit16* s, Uint32& i)
{
    CodePoint c = s[--i];

    if(CodeUnitIsSurrogateTrail(static_cast<CodeUnit16>(c)))
        c = GetSupplementaryCodePoint(s[--i], c);

    return c;
}

// =================================================================================================

/**
Back advances string offset from one code point boundary to the previous one and gets code point at that position.

Based on ICU's U16_PREV.

This function doesn't assume UTF-16 string is well formed - it handles unpaired surrogates. It also
checks for string boundaries. This is why it's slightly slower than BackAdvanceAndGetCodePointAt().
However, note that the speed difference occurs only when surrogates are encountered.

\param UTF-16 string.

\param start Starting string offset (usually 0).

\param i (inout) String offset. Must point to code point boundary. Must satisfy condition: start < i.
May point to one-past-last code unit. If it points behind code unit that is a trail surrogate, the
function will also read lead surrogate and return supplementary code point. If it points behind
unpaired trail or lead surrogate then that surrogate will be returned as code point.

\return Code point at decremented index.
*/
RED_INLINE CodePoint BackAdvanceAndGetCodePointAtSafer(const CodeUnit16* s, Uint32 start, Uint32& i)
{
    CodePoint c = s[--i];

    if(CodeUnitIsSurrogateTrail(static_cast<CodeUnit16>(c))) {
        CodeUnit16 c2;
        if(i > start && CodeUnitIsSurrogateLead(c2 = s[i - 1])) {
            --i;
            c = GetSupplementaryCodePoint(c2, c);
        }
    }

    return c;
}

// =================================================================================================

/**
Returns how may 8-bit code units (CodeUnit8) are used to encode Unicode code point.

Based on ICU's U8_LENGTH.

\param codePoint Code point to check.

\return Number of 8-bit code units (CodeUnit8) used to encode Unicode code point - it's 1..4
or 0 if code point is surrogate or is not Unicode code point (outside of range U+0000..U+10ffff).
*/
RED_INLINE Uint32 GetNumCodeUnits8(CodePoint codePoint)
{
    uint32_t c = codePoint;

    if( c <= 0x7f )
        return 1;
    else if( c <= 0x7ff )
        return 2;
    else if( c <= 0xd7ff )
        return 3;
    else if( c <= 0xdfff || c > 0x10ffff )
        return 0;
    else
        return 4;
}

// =================================================================================================

/**
Returns number of trailing code units for given UTF-8 lead code unit.

Based on ICU's U8_COUNT_TRAIL_BYTES.
*/
RED_INLINE Uint32 Utf8GetNumTrailingCodeUnits(CodeUnit8 lead)
{
    extern const uint8_t utf8NumberOfTrailCodeUnits[256];
    return utf8NumberOfTrailCodeUnits[lead];
}

// =================================================================================================

/**
Returns whether this UTF-8 code unit alone encodes a code point.

Based on ICU's U8_IS_SINGLE.

All UTF-8 code units in range [0, 0x7f] encode code points by itself - those correspond
to US-ASCII characters.

\param codeUnit UTF-8 code unit to check.

\return True - UTF-8 code unit encodes code point by itself, false - otherwise.
*/
RED_INLINE Bool Utf8CodeUnitIsSingle(CodeUnit8 codeUnit)
{
    return (codeUnit & 0x80) == 0;
}

// =================================================================================================

/**
Returns whether UTF-8 code unit is a leading code unit.

Based on ICU's U8_IS_LEAD.

\param codeUnit UTF-8 code unit to check.

\return True - code unit is leading code unit, false - otherwise.
*/
RED_INLINE Bool Utf8CodeUnitIsLead(CodeUnit8 codeUnit)
{
    return static_cast<CodeUnit8>(codeUnit - 0xc0) < 0x3e;
}

// =================================================================================================

/**
Returns whether UTF-8 code unit is a trailing code unit.

Based on ICU's U8_IS_TRAIL.

\param codeUnit UTF-8 code unit to check.

\return True - code unit is trailing code unit, false - otherwise.
*/
RED_INLINE Bool Utf8CodeUnitIsTrail(CodeUnit8 codeUnit)
{
    return (codeUnit & 0xc0) == 0x80;
}

// =================================================================================================

/**
Returns whether code point is CR line terminator.
*/
RED_INLINE Bool IsCR(CodePoint codePoint)
{
    return codePoint == 0x000d;
}

// =================================================================================================

/**
Returns whether code point is LF line terminator.
*/
RED_INLINE Bool IsLF(CodePoint codePoint)
{
    return codePoint == 0x000a;
}

// =================================================================================================

/**
Returns whether code point is ASCII Letter.
*/
RED_INLINE Bool IsASCIILetter(CodePoint cp) 
{ 
	return (cp >= 'a' && cp <='z') || (cp >= 'A' && cp <='Z');
}

// =================================================================================================
} // namespace red
// =================================================================================================
