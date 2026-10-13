/**
* Copyright (c) 2026 RED Vanguard, All Rights Reserved.
*/

#pragma once
#include "../../redFileSystem/include/file.h"
#include "../../redMath/include/box.h"
#include "mathVector4.h"

/************************************************************************/
/* IMPORTANT: All basic math should be implemented in redMath project   */
/* either as canonical (FPU) or SIMD version. This file should contain  */
/* only RTTI wrappers for mathematical types and some additional fluff  */
/* like ToString/FromString support.                                    */
/************************************************************************/

// 3D axis aligned bounding box
struct RED_REFLECTION_API Box : public math::Box
{
	RTTI_DECLARE_TYPE( Box );

	RED_FORCE_INLINE Box() = default;

	RED_FORCE_INLINE Box( const math::Box& rhs )
		: math::Box{ rhs }
	{}

	RED_FORCE_INLINE Box( const math::Vector4& min, const math::Vector4& max )
		: math::Box{ min, max }
	{}

	RED_FORCE_INLINE Box( const math::Vector4& center, Float radius )
		: math::Box{ center, radius }
	{}

	RED_FORCE_INLINE static Box UNIT()
	{
		return math::Box::UNIT();
	}

	RED_FORCE_INLINE static Box EMPTY()
	{
		return math::Box::EMPTY();
	}

	RED_FORCE_INLINE static Box FULL()
	{
		return math::Box::FULL();
	}
};


// allow simplified copying of the type
template <> struct TCopyableType<math::Box>		{ enum { Value = true }; };
template <> struct TCopyableType<Box>			{ enum { Value = true }; };

// Type aliasing for serialization
template<>
RED_INLINE const CName GetTypeName<math::Box>()
{
	return GetTypeName<Box>();
}

// Manual serialization
RED_FORCE_INLINE void operator<<( IFile& file, Box& val )
{
	static_assert( sizeof( val ) == 32, "" );
	file.Serialize( &val, sizeof( val ) );
}

// Manual serialization
RED_FORCE_INLINE void operator<<( IFile& file, math::Box& val )
{
	static_assert( sizeof( val ) == 32, "" );
	file.Serialize( &val, sizeof( val ) );
}