/**
* Copyright (c) 2016 CD Projekt Red. All Rights Reserved.
*/

#pragma once

namespace math
{
	RED_ALIGNED_STRUCT( Quaternion, 16 )
	{
		Quaternion();

		Quaternion( const Float i, const Float j, const Float k, const Float real );
		Quaternion( const Float arr[4] );
		Quaternion( const Quaternion& q );
		explicit Quaternion( const Vector4& v );

		Quaternion( const Float roll, const Float pitch, const Float yaw );
		Quaternion( const Vector4& axis, const Float angle );
		Quaternion( const Vector3& axis, const Float angle );

		Quaternion& operator=( const Quaternion& other );

		static Quaternion	IDENTITY();
		static Quaternion	I();
		static Quaternion	J();
		static Quaternion	K();

		void SetIdentity();
		void SetZeros();
		void SetInverse ();
		void SetNegative();
		void SetConjugate();

		void SetAxisAngle( const Vector4& axis, const Float angle );
		void SetAxisAngle( const Vector3& axis, const Float angle );
		void SetXRot( const Float ccwRadians );
		void SetYRot( const Float ccwRadians );
		void SetZRot( const Float ccwRadians );

		void SetAdd(const Quaternion& q1, const Quaternion& q2);
		void SetSub(const Quaternion& q1, const Quaternion& q2);
		void SetMul(const Quaternion& q1, const Quaternion& q2);
		void SetMul(const Quaternion& q,  const Float s);
		void SetDiv(const Quaternion& q,  const Float s);

		// Math geometrical interpretation
		// Quaternion q raised to power of n represents a rotation around the same axis but angle multiplied by n, using the shortest arc.
		void Power( Float exponent );
		Quaternion Powered( Float exponent ) const;


		Float Dot( const Quaternion& q ) const;
		Quaternion Neg() const;

		Quaternion operator+( const Quaternion& q ) const;
		Quaternion operator-( const Quaternion& q ) const;
		Quaternion operator*( const Quaternion& q ) const;
		Quaternion operator*( const Float s ) const;
		Quaternion operator/( const Float s ) const;

		Bool operator==( const Quaternion& q ) const;
		Bool operator!=( const Quaternion& q ) const;

		Quaternion& operator+=( const Quaternion& q );
		Quaternion& operator-=( const Quaternion& q );
		Quaternion& operator*=( const Quaternion& q );
		Quaternion& operator*=( const Float s );
		Quaternion& operator/=( const Float s );

		Vector3 operator*( const Vector3& v ) const;
		Vector4 operator*( const Vector4& v ) const;
							 
		Quaternion MulInverse( const Quaternion& q ) const;
		Quaternion MulConjugate( const Quaternion& q ) const;

		Float Magnitude() const;
		Float MagnitudeSq() const;
		Quaternion Conjugate() const;
		Quaternion Inverse() const;
		Quaternion Normalized() const;
		void Normalize();

		Vector4 TransformUnsafe( const Vector4& in ) const;
		Vector3 TransformUnsafe( const Vector3& in ) const;
		Vector4 Transform( const Vector4& in ) const;
		Vector3 Transform( const Vector3& in ) const;

		Vector4 TransformInverse( const Vector4& in ) const;
		Vector3 TransformInverse( const Vector3& in ) const;

		Vector4 TransformConjugate( const Vector4& in ) const;
		Vector3 TransformConjugate( const Vector3& in ) const;

		Vector3 GetXAxisUnsafe() const;
		Vector3 GetYAxisUnsafe() const;
		Vector3 GetZAxisUnsafe() const;

		Vector4 GetXAxis4() const;
		Vector3 GetXAxis3() const;

		Vector4 GetYAxis4() const;
		Vector3 GetYAxis3() const;

		Vector4 GetZAxis4() const;
		Vector3 GetZAxis3() const;

		Vector3 GetForward3() const;
		Vector3 GetRight3() const;
		Vector3 GetUp3() const;

		Vector4 GetForward4() const;
		Vector4 GetRight4() const;
		Vector4 GetUp4() const;

		Float GetAngle() const;
		Vector4 GetAxis4() const;
		Vector3 GetAxis3() const;
						 
		Float GetPitch() const;
		Float GetYaw() const;
		Float GetRoll() const;

		const Vector4& AsVector() const;

		Matrix ToMatrix() const;
		Matrix ToMatrixUnsafe() const;
		EulerAngles ToEulerAngles() const;

		static Quaternion Lerp ( const Quaternion& q1, const Quaternion& q2, const Float t );
		static Quaternion Slerp( const Quaternion& q1, const Quaternion& q2, const Float t, const Float tolerance = 0.99f );

		Bool IsOk() const;
		Bool IsAlmostEqual( const Quaternion& q, const Float epsilon = FLT_EPSILON )  const;

		void RemoveAxisComponent( const Vector4& axis);
		void RemoveAxisComponent( const Vector3& axis);

		void DecomposeRestAxis( const Vector4& axis, Quaternion& restOut, float& angleOut ) const;
		void DecomposeRestAxis( const Vector3& axis, Quaternion& restOut, float& angleOut ) const;

		void SetShortestRotation( const Vector4& from, const Vector4& to, const Float eps = 1e-5f);
		void SetShortestRotation( const Vector4& from, const Vector4& to, const Vector4& perp, const Float eps = 1e-5f);

		Quaternion& BuildFromDirectionVector( const Vector4& direction, const Vector4& upVec = Vector4::EZ() );
		Quaternion& Scale( const Vector4& scale );

		static Quaternion MakeFromDirectionVector( const Vector4& direction, const Vector4& upVec = Vector4::EZ() );

		const Float* AsFloat() const { return &i; }
		Float* AsFloat() { return &i; }

		union
		{
			struct
			{
				Float i, j, k, r;
			};
			Vector4 vec;
		};
	};
}