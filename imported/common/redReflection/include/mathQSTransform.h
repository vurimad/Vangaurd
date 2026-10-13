/**
* Copyright (c) 2016 CD Projekt Red. All Rights Reserved.
*/

#pragma once

#include "../../../common/redMath/include/simdVector4.h"
#include "../../../common/redMath/include/simdQSTransform.h"
/************************************************************************/
/* IMPORTANT: All basic math should be implemented in redMath project   */
/* either as canonical (FPU) or SIMD version. This file should contain  */
/* only RTTI wrappers for mathematical types and some additional fluff  */
/* like ToString/FromString support.                                    */
/************************************************************************/

RED_ALIGNED_STRUCT_API( QsTransform, RED_REFLECTION_API, 16 ) : public simd::QsTransform
{
	RTTI_DECLARE_TYPE( QsTransform );

	QsTransform() = default;

	RED_FORCE_INLINE QsTransform( const simd::QsTransform& _other )
		: simd::QsTransform( _other )
	{}

	RED_INLINE QsTransform( const simd::Vector4& _translation, const math::Quaternion& _rotation, const simd::Vector4& _scale )
		: simd::QsTransform(_translation, _rotation, _scale )
	{}

	RED_FORCE_INLINE QsTransform( const simd::Vector4& _translation, const math::Quaternion& _rotation)
		: simd::QsTransform(_translation, _rotation)
	{}
};

// Type aliasing for serialization
template<>
RED_INLINE const CName GetTypeName<simd::QsTransform>( const simd::QsTransform& )
{
	return TTypeName<QsTransform>::GetTypeName();
}

// Manual serialization
RED_FORCE_INLINE void operator<<( IFile& file, simd::QsTransform& xform )
{
	// TODO bulk serialisation
	file << xform.Translation;
	file << xform.Rotation;
	file << xform.Scale;
}

// Manual serialization
RED_FORCE_INLINE void operator<<( IFile& file, QsTransform& xform )
{
	// TODO bulk serialisation
	file << xform.Translation;
	file << xform.Rotation;
	file << xform.Scale;
}

// allow simplified copying of the type
template <> struct TCopyableType<simd::QsTransform>	{ enum { Value = true }; };
template <> struct TCopyableType<QsTransform>		{ enum { Value = true }; };