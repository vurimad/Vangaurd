/**
* Copyright (c) 2018 CD Projekt Red. All Rights Reserved.
*/

#pragma once

#include "rttiClassDeclarationMacros.h"
#include "rttiTypeName.h"
#include "../../redFileSystem/include/file.h"

#include "../../redMath/include/fixedPoint.h"
#include "../../redMath/include/fixedPoint.h"
#include "../../redMath/include/worldPosition.h"

//using FixedPoint = math::WorldPosition::FixedPoint;
//using WorldPosition = math::WorldPosition;
//using WorldTransform = math::WorldTransform;

// Disabled for now. Currently we don't want to serialize and unserialize FixedPoint, WorldPosition and WorldTransform 
// Transforms are read directly from memory blob and switching to WorldTransform will require worlds resave.
// (Remember to add RTTI_REGISTER_TYPE_WRAPPER in math.cpp after creating reflection classes for the types)

struct RED_REFLECTION_API FixedPoint : public math::WorldPosition::FixedPoint
{
	RTTI_DECLARE_TYPE( FixedPoint );

	using FixedPointType = math::WorldPosition::FixedPoint;

	FixedPoint() {}

	constexpr FixedPoint( FixedPointType::BaseType value )
		: FixedPointType( value )
	{}
	constexpr FixedPoint( const FixedPointType& value )
		: FixedPointType( value )
	{}

	RED_INLINE FixedPoint( Float value )
		: FixedPointType( value )
	{}
	RED_INLINE FixedPoint( Double value )
		: FixedPointType( value )
	{}

	static constexpr FixedPoint ZERO() { return FixedPointType::ZERO(); }
};
RED_ALLOW_TYPE_AS_POD( FixedPoint );

// Type aliasing for serialization
template<>
RED_INLINE const CName GetTypeName<FixedPoint::FixedPointType>( const FixedPoint::FixedPointType& )
{
	return TTypeName<FixedPoint>::GetTypeName();
}
// allow simplified copying of the type
template <> struct TCopyableType<FixedPoint::FixedPointType > { enum { Value = true }; };
template <> struct TCopyableType<FixedPoint> { enum { Value = true }; };

// --- 
class IScriptable;
class CScriptStackFrame;
struct RED_REFLECTION_API WorldPosition : public math::WorldPosition
{
	RTTI_DECLARE_TYPE( WorldPosition );

	using FixedPointType = math::WorldPosition::FixedPoint;

	WorldPosition() = default;

	constexpr WorldPosition( const WorldPosition& wp )
		: math::WorldPosition( wp )
	{}

	constexpr WorldPosition( const math::WorldPosition& wp )
		: math::WorldPosition( wp )
	{}

	constexpr WorldPosition( const FixedPointType& v )
		: math::WorldPosition( v )
	{}
	constexpr WorldPosition( const FixedPointType& vx, const FixedPointType& vy, const FixedPointType& vz )
		: math::WorldPosition( vx, vy, vz )
	{}

	RED_INLINE WorldPosition( Float vx, Float vy, Float vz )
		: math::WorldPosition( vx, vy, vz )
	{}
	
	RED_INLINE explicit WorldPosition( const math::Vector3& v )
		: math::WorldPosition( v )
	{}

	RED_INLINE explicit WorldPosition( const math::Vector4& v )
		: math::WorldPosition( v )
	{}

	static constexpr WorldPosition ZEROS() { return math::WorldPosition::ZEROS(); }

	static void funcSetX	  ( IScriptable*, CScriptStackFrame& stack, void* result, const rtti::IType* resultType );
	static void funcSetY	  ( IScriptable*, CScriptStackFrame& stack, void* result, const rtti::IType* resultType );
	static void funcSetZ	  ( IScriptable*, CScriptStackFrame& stack, void* result, const rtti::IType* resultType );
	static void funcSetXYZ	  ( IScriptable*, CScriptStackFrame& stack, void* result, const rtti::IType* resultType );
	static void funcSetVector4( IScriptable*, CScriptStackFrame& stack, void* result, const rtti::IType* resultType );
	static void funcGetX	  ( IScriptable*, CScriptStackFrame& stack, void* result, const rtti::IType* resultType );
	static void funcGetY	  ( IScriptable*, CScriptStackFrame& stack, void* result, const rtti::IType* resultType );
	static void funcGetZ	  ( IScriptable*, CScriptStackFrame& stack, void* result, const rtti::IType* resultType );
	static void funcToVector4 ( IScriptable*, CScriptStackFrame& stack, void* result, const rtti::IType* resultType );

};

RED_ALLOW_TYPE_AS_POD( WorldPosition );

// allow simplified copying of the type
template <> struct TCopyableType<math::WorldPosition> { enum { Value = true }; };
template <> struct TCopyableType<WorldPosition>		  { enum { Value = true }; };

// Type aliasing for serialization
RED_INLINE const CName GetTypeName( const math::WorldPosition& )
{
	return TTypeName<WorldPosition>::GetTypeName();
}

// Manual serialization
RED_FORCE_INLINE void operator<<( IFile& file, math::WorldPosition& v )
{
	static_assert( sizeof( v ) == 12, "" );
	file.Serialize( &v, sizeof( v ) );
}

// Manual serialization
RED_FORCE_INLINE void operator<<( IFile& file, WorldPosition& val )
{
	static_assert( sizeof( val ) == 12, "" );
	file.Serialize( &val, sizeof( val ) );
}
