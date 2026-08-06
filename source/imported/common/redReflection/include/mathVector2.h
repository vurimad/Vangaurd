/**
* Copyright (c) 2026 RED Vanguard, All Rights Reserved.
*/

#pragma once

#include "rttiClassDeclarationMacros.h"
#include "rttiFundamentalTypes.h"
#include "../../redFileSystem/include/file.h"

/************************************************************************/
/* IMPORTANT: All basic math should be implemented in redMath project   */
/* either as canonical (FPU) or SIMD version. This file should contain  */
/* only RTTI wrappers for mathematical types and some additional fluff  */
/* like ToString/FromString support.                                    */
/************************************************************************/

// 2-element vector
struct RED_REFLECTION_API Vector2 : public math::Vector2
{
	RTTI_DECLARE_TYPE( Vector2 );

	RED_FORCE_INLINE Vector2() = default;

	RED_FORCE_INLINE Vector2( const Vector2& v )
		: math::Vector2{ v.X, v.Y }
	{}

	RED_FORCE_INLINE Vector2( const Float x, const Float y )
		: math::Vector2{ x, y }
	{}

	RED_FORCE_INLINE Vector2( const math::Vector2& v )
		: math::Vector2{ v }
	{}

	RED_FORCE_INLINE Vector2( const math::Vector3& v )
		: math::Vector2{ v }
	{}

	RED_FORCE_INLINE Vector2( const math::Vector4& v )
		: math::Vector2{ v }
	{}

	RED_FORCE_INLINE Vector2( const Float f[ 2 ] )
		: math::Vector2{ f }
	{}

	Vector2( std::nullptr_t ) = delete;

	RED_FORCE_INLINE Vector2( Float f )
		: math::Vector2{ f }
	{}

	RED_FORCE_INLINE Vector2& operator=( const math::Vector2& v )
	{
		Set( v.X, v.Y );
		return *this;
	}

	RED_FORCE_INLINE Vector2 operator=( const Vector2& v )
	{
		Set( v.X, v.Y );
		return *this;
	}
};

// allow simplified copying of the type
template <> struct TCopyableType<math::Vector2>	{ enum { Value = true }; };
template <> struct TCopyableType<Vector2>		{ enum { Value = true }; };

// Type aliasing for serialization
RED_INLINE const CName GetTypeName( const math::Vector2& )
{
	return TTypeName<Vector2>::GetTypeName();
}

// Manual serialization
RED_FORCE_INLINE void operator<<( IFile& file, math::Vector2& v )
{
	static_assert( sizeof( v ) == 8, "" );
	file.Serialize( &v, sizeof( v ) );
}

// Manual serialization
RED_FORCE_INLINE void operator<<( IFile& file, Vector2& val )
{
	static_assert( sizeof( val ) == 8, "" );
	file.Serialize( &val, sizeof( val ) );
}

RED_REFLECTION_API Bool ToString( red::String& outTxt, const Vector2& val );

RED_REFLECTION_API Bool ToStringMaxPrecision( red::String& outTxt, const Vector2& val );

namespace red
{
	template<Uint32 Length>
	struct err::CrashDataTypeAdapter<Vector2, Length>
	{
		static_assert(Length == 0, "Length is inapplicable");
		using StorageType = Vector2;
		using SetType = StorageType;

		static CrashDataCopyResult Copy(StorageType& mem, const SetType& value)
		{
			mem = value;
			return CrashDataCopyResult::Success;
		}

		static Bool Print(char* buffer, Uint32 bufferLen, const StorageType& val)
		{
			// #tbd: ToBufferMaxPrecision()?
			const Int32 ret = red::SNPrintFUnsafe(buffer, bufferLen, "[%g, %g]", val.X, val.Y);
			return ret > -1;
		}
	};
}