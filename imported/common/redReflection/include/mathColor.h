/**
* Copyright (c) 2026 RED Vanguard, All Rights Reserved.
*/

#pragma once

#include "mathVector4.h"

/************************************************************************/
/* IMPORTANT: All basic math should be implemented in redMath project   */
/* either as canonical (FPU) or SIMD version. This file should contain  */
/* only RTTI wrappers for mathematical types and some additional fluff  */
/* like ToString/FromString support.                                    */
/************************************************************************/

struct RED_REFLECTION_API Color : public math::Color
{
	RTTI_DECLARE_TYPE( Color );

	RED_FORCE_INLINE Color() = default;

	RED_FORCE_INLINE Color( const math::Color& col )
		: math::Color( col )
	{}

	RED_FORCE_INLINE Color( Uint8 r, Uint8 g, Uint8 b, Uint8 a = 255 )
		: math::Color{ r, g, b, a }
	{}

	RED_FORCE_INLINE Color( const math::Vector4& v )
		: math::Color( v )
	{}

	RED_FORCE_INLINE Color( const Float f[4] )
		: math::Color{ f }
	{}

	RED_FORCE_INLINE explicit Color( Uint32 x )
		: math::Color{ x }
	{}

	// Some predefined colors
	RED_FORCE_INLINE static Color BLACK( Uint8 alpha = 255 ){ return math::Color::BLACK( alpha ); }
	RED_FORCE_INLINE static Color WHITE( Uint8 alpha = 255 ){ return math::Color::WHITE( alpha ); }
	RED_FORCE_INLINE static Color RED( Uint8 alpha = 255 ){ return math::Color::RED( alpha ); }
	RED_FORCE_INLINE static Color GREEN( Uint8 alpha = 255 ){ return math::Color::GREEN( alpha ); }
	RED_FORCE_INLINE static Color BLUE( Uint8 alpha = 255 ){ return math::Color::BLUE( alpha ); }
	RED_FORCE_INLINE static Color YELLOW( Uint8 alpha = 255 ){ return math::Color::YELLOW( alpha ); }
	RED_FORCE_INLINE static Color CYAN( Uint8 alpha = 255 ){ return math::Color::CYAN( alpha ); }
	RED_FORCE_INLINE static Color MAGENTA( Uint8 alpha = 255 ){ return math::Color::MAGENTA( alpha ); }
	RED_FORCE_INLINE static Color LIGHT_RED( Uint8 alpha = 255 ){ return math::Color::LIGHT_RED( alpha ); }
	RED_FORCE_INLINE static Color LIGHT_GREEN( Uint8 alpha = 255 ){ return math::Color::LIGHT_GREEN( alpha ); }
	RED_FORCE_INLINE static Color LIGHT_BLUE( Uint8 alpha = 255 ){ return math::Color::LIGHT_BLUE( alpha ); }
	RED_FORCE_INLINE static Color LIGHT_YELLOW( Uint8 alpha = 255 ){ return math::Color::LIGHT_YELLOW( alpha ); }
	RED_FORCE_INLINE static Color LIGHT_CYAN( Uint8 alpha = 255 ){ return math::Color::LIGHT_CYAN( alpha ); }
	RED_FORCE_INLINE static Color LIGHT_MAGENTA( Uint8 alpha = 255 ){ return math::Color::LIGHT_MAGENTA( alpha ); }
	RED_FORCE_INLINE static Color LIGHT_GRAY( Uint8 alpha = 255 ){ return math::Color::LIGHT_GRAY( alpha ); }
	RED_FORCE_INLINE static Color DARK_RED( Uint8 alpha = 255 ){ return math::Color::DARK_RED( alpha ); }
	RED_FORCE_INLINE static Color DARK_GREEN( Uint8 alpha = 255 ){ return math::Color::DARK_GREEN( alpha ); }
	RED_FORCE_INLINE static Color DARK_BLUE( Uint8 alpha = 255 ){ return math::Color::DARK_BLUE( alpha ); }
	RED_FORCE_INLINE static Color DARK_YELLOW( Uint8 alpha = 255 ){ return math::Color::DARK_YELLOW( alpha ); }
	RED_FORCE_INLINE static Color DARK_CYAN( Uint8 alpha = 255 ){ return math::Color::DARK_CYAN( alpha ); }
	RED_FORCE_INLINE static Color DARK_MAGENTA( Uint8 alpha = 255 ){ return math::Color::DARK_MAGENTA( alpha ); }
	RED_FORCE_INLINE static Color DARK_GRAY( Uint8 alpha = 255 ){ return math::Color::DARK_GRAY( alpha ); }
	RED_FORCE_INLINE static Color BROWN( Uint8 alpha = 255 ){ return math::Color::BROWN( alpha ); }
	RED_FORCE_INLINE static Color GRAY( Uint8 alpha = 255 ){ return math::Color::GRAY( alpha ); }
	RED_FORCE_INLINE static Color NORMAL( Uint8 alpha = 255 ){ return math::Color::NORMAL( alpha ); }
	RED_FORCE_INLINE static Color CLEAR(){ return math::Color::CLEAR(); }
};

// allow simplified copying of the type
template <> struct TCopyableType<math::Color>	{ enum { Value = true }; };
template <> struct TCopyableType<Color>			{ enum { Value = true }; };

// Type aliasing for serialization
template<>
RED_INLINE const CName GetTypeName<math::Color>()
{
	return GetTypeName<Color>();
}

// Manual serialization
RED_FORCE_INLINE void operator<<( IFile& file, Color& val )
{
	static_assert( sizeof( val ) == 4, "" );
	file.Serialize( &val, sizeof( val ) );
}

// Manual serialization
RED_FORCE_INLINE void operator<<( IFile& file, math::Color& val )
{
	static_assert( sizeof( val ) == 4, "" );
	file.Serialize( &val, sizeof( val ) );
}


RED_ALIGNED_STRUCT_API( HDRColor, RED_REFLECTION_API, 16 ) : public math::Vector4
{
	RTTI_DECLARE_TYPE( HDRColor );

	RED_FORCE_INLINE HDRColor() = default;

	RED_FORCE_INLINE HDRColor( const math::Vector4& col )
		: math::Vector4( col )
	{}

	RED_FORCE_INLINE HDRColor( const math::Color& col )
		: math::Vector4( col.R / 255.f, col.G / 255.f, col.B / 255.f, col.A / 255.f )
	{}

	RED_FORCE_INLINE HDRColor( Float r, Float g, Float b, Float a = 1.0f )
		: math::Vector4{ r, g, b, a }
	{}

	RED_FORCE_INLINE HDRColor( const Float f[4] )
		: math::Vector4{ f }
	{}

	RED_FORCE_INLINE explicit HDRColor( Float x )
		: math::Vector4{ x }
	{}

	Vector4 GetHDR() const { return Vector4( X * W, Y * W, Z * W, 1.0f ); }
	Vector4 GetHDR( Float s ) const { return Vector4( s * X * W, s * Y * W, s * Z * W, 1.0f); }

	// Some predefined colors
	static HDRColor BLACK() { return{ 0.f, 0.f, 0.f }; }
	static HDRColor WHITE() { return{ 1.f, 1.f, 1.f }; }
	static HDRColor RED() { return{ 1.f, 0.f, 0.f }; }
	static HDRColor GREEN() { return{ 0.f, 1.f, 0.f }; }
	static HDRColor BLUE() { return{ 0.f, 0.f, 1.f }; }
	static HDRColor YELLOW() { return{ 1.f, 1.f, 0.f }; }
	static HDRColor CYAN() { return{ 0.f, 1.f, 1.f }; }
	static HDRColor MAGENTA() { return{ 1.f, 0.f, 1.f }; }
	static HDRColor LIGHT_RED() { return{ 1.f, 0.5f, 0.5f }; }
	static HDRColor LIGHT_GREEN() { return{ 0.5f, 1.f, 0.5f }; }
	static HDRColor LIGHT_BLUE() { return{ 0.5f, 0.5f, 1.f }; }
	static HDRColor LIGHT_YELLOW() { return{ 1.f, 1.f, 0.5f }; }
	static HDRColor LIGHT_CYAN() { return{ 0.5f, 1.f, 1.f }; }
	static HDRColor LIGHT_MAGENTA() { return{ 1.f, 0.5f, 1.f }; }
	static HDRColor LIGHT_GRAY() { return{ 0.75f, 0.75f, 0.75f }; }
	static HDRColor DARK_RED() { return{ 0.5, 0.f, 0.f }; }
	static HDRColor DARK_GREEN() { return{ 0.f, 0.5f, 0.f }; }
	static HDRColor DARK_BLUE() { return{ 0.f, 0.f, 0.5f }; }
	static HDRColor DARK_YELLOW() { return{ 0.5f, 0.5f, 0.f }; }
	static HDRColor DARK_CYAN() { return{ 0.f, 0.5f, 0.5f }; }
	static HDRColor DARK_MAGENTA() { return{ 0.5f, 0.f, 0.5f }; }
	static HDRColor DARK_GRAY() { return{ 0.36f, 0.36f, 0.36f }; }
	static HDRColor BROWN() { return{ 0.55f, 0.25f, 0.075f }; }
	static HDRColor GRAY() { return{ 0.5f, 0.5f, 0.5f }; }
	static HDRColor NORMAL() { return{ 0.5f, 0.5f, 1.f }; }
	static HDRColor CLEAR() { return{ 0.f, 0.f, 0.f }; }

	// For lerping
	HDRColor& operator*=( const Float x ) { W *= x; return *this; }

	HDRColor operator*( const Float x ) const { return HDRColor( X, Y, Z, x * W ); }
	HDRColor operator+( const HDRColor& other ) const { return HDRColor( other.X * other.W + X * W, other.Y * other.W + Y * W, other.Z * other.W + Z * W, 1.0f ); }

};

// allow simplified copying of the type
template <> struct TCopyableType<HDRColor> { enum { Value = true }; };

// Manual serialization
RED_FORCE_INLINE void operator<<( IFile& file, HDRColor& val )
{
	static_assert( sizeof( val ) == 16, "" );
	file.Serialize( &val, sizeof( val ) );
}

RED_ALIGNED_STRUCT_API(ColorBalance, RED_REFLECTION_API, 16) : public math::Vector4
{
	RTTI_DECLARE_TYPE(ColorBalance);

	ColorBalance() = default;

	RED_FORCE_INLINE ColorBalance(const math::Vector4& col)
		: math::Vector4(col)
	{}

	RED_FORCE_INLINE ColorBalance(const math::Vector3& col, const Float y)
		: math::Vector4(col.X, col.Y, col.Z, y)
	{}

	ColorBalance(const math::Color& col)
		: math::Vector4(col.R / 255.f, col.G / 255.f, col.B / 255.f, col.A / 255.f)
	{}

	RED_FORCE_INLINE ColorBalance(const HDRColor& col)
		: math::Vector4( col )
	{}

	RED_FORCE_INLINE ColorBalance(Float r, Float g, Float b, Float y = 1.0f)
		: math::Vector4{ r, g, b, y }
	{}

	RED_FORCE_INLINE ColorBalance(const Float f[4])
		: math::Vector4{ f }
	{}

	RED_FORCE_INLINE explicit ColorBalance(Float x)
		: math::Vector4{ x }
	{}
};

// allow simplified copying of the type
template <> struct TCopyableType<ColorBalance> { enum { Value = true }; };

// Manual serialization
RED_FORCE_INLINE void operator<<(IFile& file, ColorBalance& val)
{
	static_assert( sizeof( val ) == 16, "" );
	file.Serialize( &val, sizeof( val ) );
}

namespace Config
{
	namespace Helper
	{
		template<>
		struct ResolveConsoleType<Color> { static const EConfigVarType value = eConsoleVarType_Color; };
	}
}

namespace red
{
	RED_INLINE const char* GetFormatString(const Color& val) { return "[%u,%u,%u,%u]"; }
	extern RED_REFLECTION_API Bool CheckFormatString(const Color& val, const char* formatToCheck);
	extern RED_REFLECTION_API Bool ToBuffer(char* buffer, const red::Uint32 bufferLen, const Color& val, red::Int32& written, const char* formatString = nullptr);
}
//
template<>
RED_REFLECTION_API const Bool ToString<Color>( red::String& outTxt, const Color& val, const char* customFormat );

template<>
RED_REFLECTION_API const Bool FromString<Color>( const red::String& txt, Color& outVal );

