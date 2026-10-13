/*
* Copyright (c) 2017 CD Projekt Red. All Rights Reserved.
*/

#pragma once

#include "box.h"

namespace math
{
	RED_ALIGNED_STRUCT( Transform, 16 )
	{
	private:
		enum EIdentity { EIDENTITY };

	public:
		Transform();
		Transform( EIdentity );

		RED_INLINE Transform& operator=( const Transform& xform );
		RED_INLINE Bool operator==( const Transform& xform ) const;
		RED_INLINE Bool operator!=( const Transform& xform ) const;
		RED_INLINE Bool operator<( const Transform& xform ) const;
		RED_INLINE Bool IsOk() const;
		
		RED_INLINE Transform( const Vector3& position, const Quaternion& orientation = Quaternion::IDENTITY() );
		RED_INLINE Transform( const Vector4& position, const Quaternion& orientation = Quaternion::IDENTITY() );
		RED_INLINE Transform( const Quaternion& q );
		RED_INLINE Transform( const Transform& xform );

		RED_INLINE Vector4   operator*( const Vector4& v ) const; //< This will be equivalent to a transform point or vector depending on w component
		RED_INLINE Vector3   operator*( const Vector3& v ) const; //< This will be equivalent to a transform point, forcing the W component to be a 1
		RED_INLINE Transform operator*( const Transform& xform ) const;

		RED_INLINE Vector3 TransformPoint( const Vector3& v ) const;
		RED_INLINE Vector4 TransformPoint( const Vector4& v ) const;
		RED_INLINE Vector3 TransformInvPoint( const Vector3& v ) const;

		RED_INLINE Vector3 TransformVector( const Vector3& v ) const;
		RED_INLINE Vector4 TransformVector( const Vector4& v ) const;

		RED_INLINE Box TransformBox( const Box& b ) const;

		RED_INLINE EulerAngles ToEulerAngles() const;
		RED_INLINE Matrix ToMatrix() const;
		RED_INLINE Matrix ToInvMatrix() const;

		RED_INLINE Vector3 GetForward() const;
		RED_INLINE Vector3 GetRight() const;
		RED_INLINE Vector3 GetUp() const;

		RED_INLINE Float GetPitch() const;
		RED_INLINE Float GetYaw() const;
		RED_INLINE Float GetRoll() const;

		RED_INLINE void SetIdentity();
		RED_INLINE void SetInverse();
		RED_INLINE Transform GetInverse() const;
		RED_INLINE Transform& Invert();

		RED_INLINE const Vector4& GetPosition() const;
		RED_INLINE const Vector3& GetPosition3() const;
		RED_INLINE const Quaternion& GetOrientation() const;

		RED_INLINE void SetPosition(const Vector3& v );
		RED_INLINE void SetPosition(const Vector4& v );
		RED_INLINE void SetOrientation(const Quaternion& q);
		RED_INLINE void SetOrientation(const EulerAngles& e);
		RED_INLINE void SetOrientation(const Vector3& direction); //< Set the orientation of this transform from a direction (look at) vector
		RED_INLINE void SetOrientation(const Vector4& direction); //< Set the orientation of this transform from a direction (look at) vector

		RED_INLINE void Set( const Vector3& position, const Quaternion& orientation );
		RED_INLINE void Set( const Vector3& position, const EulerAngles& orientation );
		RED_INLINE void Set( const Vector3& position, const Vector4& direction );

		RED_INLINE void Set( const Vector4& position, const Quaternion& orientation );
		RED_INLINE void Set( const Vector4& position, const EulerAngles& orientation );
		RED_INLINE void Set( const Vector4& position, const Vector4& direction );

		static Transform IDENTITY();

		Vector4		m_position = Vector4::ZEROS();		//< This position is a vector and not a point so its W component is forced to 0
		Quaternion	m_orientation = Quaternion::IDENTITY();
	};
}