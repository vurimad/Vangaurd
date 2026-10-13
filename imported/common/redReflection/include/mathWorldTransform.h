#pragma once

#include "rttiClassDeclarationMacros.h"
#include "rttiTypeName.h"
#include "../../redFileSystem/include/file.h"
#include "../../redMath/include/worldTransform.h"
#include "scriptable.h"
#include "mathVector3.h"
#include "mathQuaternion.h"
#include "mathWorldPosition.h"
#include "mathEulerAngles.h"

struct RED_REFLECTION_API WorldTransform : public math::WorldTransform
{
	RTTI_DECLARE_TYPE( WorldTransform );

	WorldTransform() = default;

	RED_FORCE_INLINE WorldTransform( const math::WorldTransform& t )
		: math::WorldTransform( t )
	{}
	
	RED_FORCE_INLINE WorldTransform( const math::WorldPosition& p )
		: math::WorldTransform( p )
	{}

	RED_INLINE WorldTransform( const math::WorldPosition& p, const math::Quaternion& q )
		: math::WorldTransform( p, q )
	{}
	RED_INLINE explicit WorldTransform( const math::Quaternion& q )
		: math::WorldTransform( q )
	{}
	RED_INLINE explicit WorldTransform( const math::Transform& xform )
		: math::WorldTransform( xform )
	{}
	RED_INLINE explicit WorldTransform( const math::Vector3& p )
		: math::WorldTransform( p )
	{}
	RED_INLINE explicit WorldTransform( const math::Vector3& p, const math::Quaternion& q )
		: math::WorldTransform( p, q )
	{}

	RED_INLINE Bool operator == ( const math::WorldTransform& v ) const
	{
		return math::WorldTransform::operator==( v );
	}
	RED_INLINE Bool operator != ( const math::WorldTransform& v ) const
	{
		return math::WorldTransform::operator!=( v );
	}

	RED_INLINE const WorldPosition& GetPosition() const
	{
		return reinterpret_cast<const WorldPosition&>(math::WorldTransform::GetPosition());
	}
	RED_INLINE const Quaternion& GetOrientation() const
	{
		return reinterpret_cast<const Quaternion&>(math::WorldTransform::GetOrientation());
	}

	static RED_FORCE_INLINE WorldTransform IDENTITY() { return math::WorldTransform::IDENTITY(); }

	static void funcSetIdentity( IScriptable*, CScriptStackFrame& stack, void* result, const rtti::IType* resultType );
	static void funcSetWorldPosition( IScriptable*, CScriptStackFrame& stack, void* result, const rtti::IType* resultType );
	static void funcSetPosition( IScriptable*, CScriptStackFrame& stack, void* result, const rtti::IType* resultType );
	static void funcSetOrientation_Quat( IScriptable*, CScriptStackFrame& stack, void* result, const rtti::IType* resultType );
	static void funcSetOrientation_Eulers( IScriptable*, CScriptStackFrame& stack, void* result, const rtti::IType* resultType );
	static void funcSetOrientation_Direction( IScriptable*, CScriptStackFrame& stack, void* result, const rtti::IType* resultType );

	static void funcTransformXForm(IScriptable*, CScriptStackFrame& stack, void* result, const rtti::IType* resultType );
	static void funcTransformWorldXForm(IScriptable*, CScriptStackFrame& stack, void* result, const rtti::IType* resultType );
	
	static void funcTransformPoint(IScriptable*, CScriptStackFrame& stack, void* result, const rtti::IType* resultType );
	static void funcTransformWorldPosition(IScriptable*, CScriptStackFrame& stack, void* result, const rtti::IType* resultType );
	
	static void funcTransformInvWorldXForm(IScriptable*, CScriptStackFrame& stack, void* result, const rtti::IType* resultType );
	static void funcTransformInvXForm(IScriptable*, CScriptStackFrame& stack, void* result, const rtti::IType* resultType );
	static void funcTransformInvPoint( IScriptable*, CScriptStackFrame& stack, void* result, const rtti::IType* resultType );
	static void funcTransformInvWorldPosition( IScriptable*, CScriptStackFrame& stack, void* result, const rtti::IType* resultType );
	
	static void funcGetWorldPosition( IScriptable*, CScriptStackFrame& stack, void* result, const rtti::IType* resultType );
	static void funcGetOrientation( IScriptable*, CScriptStackFrame& stack, void* result, const rtti::IType* resultType );
	static void funcGetInverse(IScriptable*, CScriptStackFrame& stack, void* result, const rtti::IType* resultType );
	static void funcGetForward(IScriptable*, CScriptStackFrame& stack, void* result, const rtti::IType* resultType );
	static void funcGetRight( IScriptable*, CScriptStackFrame& stack, void* result, const rtti::IType* resultType );
	static void funcGetUp( IScriptable*, CScriptStackFrame& stack, void* result, const rtti::IType* resultType );

	static void funcToMatrix( IScriptable*, CScriptStackFrame& stack, void* result, const rtti::IType* resultType );
	static void funcToXForm( IScriptable*, CScriptStackFrame& stack, void* result, const rtti::IType* resultType );
};

namespace red
{
	template<Uint32 Length>
	struct err::CrashDataTypeAdapter<WorldTransform, Length>
	{
		static_assert(Length == 0, "Length is inapplicable");
		using StorageType = WorldTransform;
		using SetType = WorldTransform;

		static CrashDataCopyResult Copy(StorageType& mem, const SetType& value)
		{
			mem = value;
			return CrashDataCopyResult::Success;
		}

		static Bool Print(char* buffer, Uint32 bufferLen, const StorageType& val)
		{
			const auto& xform = val._ToXForm();
			const auto& pos4 = xform.GetPosition();
			const EulerAngles angles = xform.GetOrientation().ToEulerAngles();
			// #tbd: ToBufferMaxPrecision()?
			const Int32 ret = red::SNPrintFUnsafe(buffer, bufferLen,
				"Position=[%g, %g, %g, %g] "
				"Rotation=[Roll:%g, Pitch:%g, Yaw:%g]",
				pos4.X, pos4.Y, pos4.Z, pos4.W,
				angles.Roll, angles.Pitch, angles.Yaw
			);
			return ret > -1;
		}
	};
}