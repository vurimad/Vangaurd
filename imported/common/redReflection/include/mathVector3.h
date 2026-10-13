/**
* Copyright (c) 2026 RED Vanguard, All Rights Reserved.
*/

#pragma once

#include "rttiClassDeclarationMacros.h"
#include "rttiTypeName.h"

#include "../../redFileSystem/include/file.h"

/************************************************************************/
/* IMPORTANT: All basic math should be implemented in redMath project   */
/* either as canonical (FPU) or SIMD version. This file should contain  */
/* only RTTI wrappers for mathematical types and some additional fluff  */
/* like ToString/FromString support.                                    */
/************************************************************************/

// Vector3 is for memory critical structures. It lacks many features so don't use it everywhere. Note: must not be aligned !
struct RED_REFLECTION_API Vector3 : public math::Vector3
{
	RTTI_DECLARE_TYPE( Vector3 );

	RED_FORCE_INLINE Vector3() = default;

	RED_FORCE_INLINE Vector3( const Vector3& v )
		: math::Vector3{ v.X, v.Y, v.Z }
	{}

	RED_FORCE_INLINE Vector3( const math::Vector4& v )
		: math::Vector3{ v }
	{}

	RED_FORCE_INLINE Vector3( const math::Vector3& v )
		: math::Vector3{ v }
	{}

	RED_FORCE_INLINE explicit Vector3( const math::Vector2& v )
		: math::Vector3{ v.X, v.Y, 0.f }
	{}

	RED_FORCE_INLINE Vector3( const Float f[3] )
		: math::Vector3{ f }
	{}

	Vector3( std::nullptr_t ) = delete;

	RED_FORCE_INLINE explicit Vector3( const Float f )
		: math::Vector3( f )
	{}

	RED_FORCE_INLINE Vector3( const Float x, const Float y, const Float z )
		: math::Vector3{ x, y, z }
	{}

	RED_FORCE_INLINE Vector3& operator=( const math::Vector2& v )
	{
		Set( v.X, v.Y, 0.f );
		return *this;
	}

	RED_FORCE_INLINE Vector3& operator=( const math::Vector3& v )
	{
		Set( v.X, v.Y, v.Z );
		return *this;
	}

	RED_FORCE_INLINE Vector3& operator=( const math::Vector4& v )
	{
		Set( v.X, v.Y, v.Z );
		return *this;
	}

	RED_FORCE_INLINE Vector3& operator=( const Float f )
	{
		Set( f, f, f );
		return *this;
	}

	RED_FORCE_INLINE static Vector3 Lerp( const Vector3& a, const Vector3& b, float t )
	{
		return Vector3( math::Lerp( t, a.X, b.X ), math::Lerp( t, a.Y, b.Y ), math::Lerp( t, a.Z, b.Z ) );
	}
};

RED_ALLOW_TYPE_AS_POD( Vector3 );

// allow simplified copying of the type
template <> struct TCopyableType<math::Vector3>	{ enum { Value = true }; };
template <> struct TCopyableType<Vector3>		{ enum { Value = true }; };

// Type aliasing for serialization
RED_INLINE const CName GetTypeName( const math::Vector3& )
{
	return TTypeName<Vector3>::GetTypeName();
}

// Manual serialization
RED_FORCE_INLINE void operator<<( IFile& file, math::Vector3& v )
{
	static_assert( sizeof( v ) == 12, "" );
	file.Serialize( &v, sizeof( v ) );
}

// Manual serialization
RED_FORCE_INLINE void operator<<( IFile& file, Vector3& val )
{
	static_assert( sizeof( val ) == 12, "" );
	file.Serialize( &val, sizeof( val ) );
}

// Specialization
template <>
RED_FORCE_INLINE Vector3 Clamp<Vector3>( const Vector3& a, const Vector3& b, const Vector3& c )
{
	return{ ::Clamp( a.X, b.X, c.X ),
			::Clamp( a.Y, b.Y, c.Y ) ,
			::Clamp( a.Z, b.Z, c.Z ) };
}

RED_FORCE_INLINE Vector3 Clamp( const Vector3& a, const Float b, const Float c )
{
	return{ ::Clamp( a.X, b, c ),
			::Clamp( a.Y, b, c ) ,
			::Clamp( a.Z, b, c ) };
}

RED_REFLECTION_API Bool ToString( red::String& outTxt, const Vector3& val );

RED_REFLECTION_API Bool ToStringMaxPrecision( red::String& outTxt, const Vector3& val );

namespace red
{
	template<Uint32 Length>
	struct err::CrashDataTypeAdapter<Vector3, Length>
	{
		static_assert(Length == 0, "Length is inapplicable");
		using StorageType = Vector3;
		using SetType = StorageType;

		static CrashDataCopyResult Copy(StorageType& mem, const SetType& value )
		{
			mem = value;
			return CrashDataCopyResult::Success;
		}

		static Bool Print(char* buffer, Uint32 bufferLen, const StorageType& val)
		{
			// #tbd: ToBufferMaxPrecision()?
			const Int32 ret = red::SNPrintFUnsafe(buffer, bufferLen, "[%g, %g, %g]", val.X, val.Y, val.Z);
			return ret > -1;
		}
	};
}