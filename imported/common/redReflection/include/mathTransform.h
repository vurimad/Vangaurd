/**
* Copyright (c) 2017 CD Projekt Red. All Rights Reserved.
*/

#pragma once

/************************************************************************/
/* IMPORTANT: All basic math should be implemented in redMath project   */
/* either as canonical (FPU) or SIMD version. This file should contain  */
/* only RTTI wrappers for mathematical types and some additional fluff  */
/* like ToString/FromString support.                                    */
/************************************************************************/

RED_ALIGNED_STRUCT_API( Transform, RED_REFLECTION_API, 16 ) : public math::Transform
{
	RTTI_DECLARE_TYPE( Transform );

	RED_FORCE_INLINE Transform() 
		: math::Transform()
	{}

	Transform( const math::Vector3& position, const math::Quaternion& orientation = math::Quaternion::IDENTITY() )
		: math::Transform{ position, orientation }
	{}

	Transform( const math::Vector4& position, const math::Quaternion& orientation = math::Quaternion::IDENTITY() )
		: math::Transform{ position, orientation }
	{}

	Transform( const math::Quaternion& q )
		: math::Transform{ q }
	{}

	Transform( const math::Transform& xform )
		: math::Transform{ xform }
	{}

	Bool operator==( const Transform& xform ) const
	{
		return math::Transform::operator==(xform);
	}

	Bool operator!=( const Transform& xform ) const
	{
		return math::Transform::operator!=(xform);
	}

	const Vector4& GetPosition() const 
	{ 
		return reinterpret_cast<const Vector4&>(math::Transform::GetPosition()); 
	}

	const Vector3& GetPosition3() const 
	{ 
		return reinterpret_cast<const Vector3&>(math::Transform::GetPosition3()); 
	}

	const Quaternion& GetOrientation() const 
	{ 
		return reinterpret_cast<const Quaternion&>(math::Transform::GetOrientation()); 
	}

	static Transform Lerp( const Transform& a, const Transform& b, const float t )
	{
		return Transform( Vector4::Lerp( a.m_position, b.m_position, t ), Quaternion::Lerp( a.m_orientation, b.m_orientation, t ) );
	}

private:
	static void funcTransformPoint(IScriptable*, CScriptStackFrame& stack, void* result, const rtti::IType* resultType );
	static void funcTransformVector(IScriptable*, CScriptStackFrame& stack, void* result, const rtti::IType* resultType );
	static void funcToEulerAngles(IScriptable*, CScriptStackFrame& stack, void* result, const rtti::IType* resultType );
	static void funcToMatrix(IScriptable*, CScriptStackFrame& stack, void* result, const rtti::IType* resultType );
	static void funcGetForward(IScriptable*, CScriptStackFrame& stack, void* result, const rtti::IType* resultType );
	static void funcGetRight(IScriptable*, CScriptStackFrame& stack, void* result, const rtti::IType* resultType );
	static void funcGetUp(IScriptable*, CScriptStackFrame& stack, void* result, const rtti::IType* resultType );
	static void funcGetPitch(IScriptable*, CScriptStackFrame& stack, void* result, const rtti::IType* resultType );
	static void funcGetYaw(IScriptable*, CScriptStackFrame& stack, void* result, const rtti::IType* resultType );
	static void funcGetRoll(IScriptable*, CScriptStackFrame& stack, void* result, const rtti::IType* resultType );
	static void funcSetIdentity(IScriptable*, CScriptStackFrame& stack, void* result, const rtti::IType* resultType );
	static void funcSetInverse(IScriptable*, CScriptStackFrame& stack, void* result, const rtti::IType* resultType );
	static void funcGetInverse(IScriptable*, CScriptStackFrame& stack, void* result, const rtti::IType* resultType );
	static void funcGetPosition(IScriptable*, CScriptStackFrame& stack, void* result, const rtti::IType* resultType );
	static void funcGetOrientation(IScriptable*, CScriptStackFrame& stack, void* result, const rtti::IType* resultType );
	static void funcSetPosition(IScriptable*, CScriptStackFrame& stack, void* result, const rtti::IType* resultType );
	static void funcSetOrientation_Quat(IScriptable*, CScriptStackFrame& stack, void* result, const rtti::IType* resultType );
	static void funcSetOrientation_Eulers(IScriptable*, CScriptStackFrame& stack, void* result, const rtti::IType* resultType );
	static void funcSetOrientation_Direction(IScriptable*, CScriptStackFrame& stack, void* result, const rtti::IType* resultType );

	friend void operator<<( IFile& file, ::Transform& xform );
};

// Type aliasing for serialization
template<>
RED_INLINE const CName GetTypeName<math::Transform>( const math::Transform& )
{
	return TTypeName<::Transform>::GetTypeName();
}

// Manual serialization
RED_FORCE_INLINE void operator<<( IFile& file, math::Transform& xform )
{
	// TODO bulk serialisation
	auto& q = const_cast< math::Quaternion& >( xform.GetOrientation() );
	file << xform.m_position;
	file << q;
}

// Manual serialization
RED_FORCE_INLINE void operator<<( IFile& file, ::Transform& xform )
{
	// TODO bulk serialisation
	auto& q = const_cast< Quaternion& >( xform.GetOrientation() );
	file << xform.m_position;
	file << q;
}

// allow simplified copying of the type
template <> struct TCopyableType<math::Transform>{ enum { Value = true }; };
template <> struct TCopyableType<::Transform>	 { enum { Value = true }; };

namespace red
{
	template<Uint32 Length>
	struct err::CrashDataTypeAdapter<Transform, Length>
	{
		static_assert(Length == 0, "Length is inapplicable");
		using StorageType = Transform;
		using SetType = Transform;

		static CrashDataCopyResult Copy(StorageType& mem, const SetType& value)
		{
			mem = value;
			return CrashDataCopyResult::Success;
		}

		static Bool Print(char* buffer, Uint32 bufferLen, const StorageType& val)
		{
			const auto& pos4 = val.GetPosition();
			const EulerAngles angles = val.GetOrientation().ToEulerAngles();
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