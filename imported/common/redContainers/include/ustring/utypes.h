#pragma once

// =================================================================================================
namespace red {
// =================================================================================================

/**
A Unicode code point is an integer with a value from 0 to 0x10FFFF. It is 32 bits wide signed
integer (int32_t). This allows the use of easily testable negative values as sentinels, to
indicate errors, exceptions or "done" conditions. All negative values and positive values greater
than 0x10FFFF are illegal as Unicode code points.
*/
typedef int32_t CodePoint;

/**
UTF-16 code unit.
*/
typedef uint16_t CodeUnit16;

/**
UTF-8 code unit.
*/
typedef uint8_t CodeUnit8;

// =================================================================================================

/**
This namespace contains constants for various Unicode characters.
*/
namespace UnicodeChars {
    const CodePoint replacementCharacter = 0xfffd; ///< U+FFFD "REPLACEMENT CHARACTER".
    const CodePoint textTagCharacter = 0xe001; ///< U+E001 "text formatting tag".
}

// =================================================================================================
} // namespace red
// =================================================================================================
