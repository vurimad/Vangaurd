/**
* Copyright (c) 2026 RED Vanguard, All Rights Reserved.
*/
#pragma once

namespace math
{
	struct Matrix;
	struct Quaternion;

	// Euler angles, rotations are CCW, in degrees, order: Y X Z
	struct EulerAngles
	{
		EulerAngles() = default;
		RED_INLINE EulerAngles( const EulerAngles &ea );
		RED_INLINE EulerAngles( const Float roll, const Float pitch, const Float yaw );
		RED_INLINE EulerAngles( const Vector3& v );
		RED_INLINE EulerAngles( const Float f[ 3 ] );

		RED_INLINE EulerAngles operator-() const;
		RED_INLINE EulerAngles operator+( const EulerAngles& a ) const;
		RED_INLINE EulerAngles operator-( const EulerAngles& a ) const;
		RED_INLINE EulerAngles operator*( const EulerAngles& a ) const;
		RED_INLINE EulerAngles operator/( const EulerAngles& a ) const;

		RED_INLINE EulerAngles operator+( const Float a ) const;
		RED_INLINE EulerAngles operator-( const Float a ) const;
		RED_INLINE EulerAngles operator*( const Float a ) const;
		RED_INLINE EulerAngles operator/( const Float a ) const;

		RED_INLINE EulerAngles& operator+=( const EulerAngles& a );
		RED_INLINE EulerAngles& operator-=( const EulerAngles& a );
		RED_INLINE EulerAngles& operator*=( const EulerAngles& a );
		RED_INLINE EulerAngles& operator/=( const EulerAngles& a );

		RED_INLINE EulerAngles& operator+=( const Float a );
		RED_INLINE EulerAngles& operator-=( const Float a );
		RED_INLINE EulerAngles& operator*=( const Float a );
		RED_INLINE EulerAngles& operator/=( const Float a );

		RED_INLINE Bool operator==( const EulerAngles& a ) const;
		RED_INLINE Bool operator!=( const EulerAngles& a ) const;

		RED_INLINE Bool AlmostEquals( const EulerAngles& a, const Float epsilon = 0.01f ) const;

		// Normalize angle [0 360]
		RED_INLINE static Float NormalizeAngle( Float angle );
		RED_INLINE EulerAngles& Normalize();

		// Normalize angle [-180 180]
		RED_INLINE static Float NormalizeAngle180( Float angle );

		// Converts angle to an angle nearest to referenceAngle by adding/subtracting 360
		RED_INLINE static Float ToNearestAngle( Float angle, const Float referenceAngle );

		// Compute yaw angle from various stuff
		RED_INLINE static Float YawFromXY( const Float x, const Float y );
		RED_INLINE static Double YawFromXY( const Double x, const Double y );
		RED_INLINE static Vector4 YawToVector( const Float yaw );
		RED_INLINE static Vector2 YawToVector2( const Float yaw );

		// Shortest angular distance between two angles (aware of 360 wrap around problem)
		RED_INLINE static Float AngleDistance( const Float a, const Float b );

		// Shortest angular distance between two set of Euler angles (aware of 360 wrap around problem)
		RED_INLINE static EulerAngles AngleDistance( const EulerAngles& a, const EulerAngles& b );

		// Lame interpolation of angle
		RED_INLINE static Float Interpolate( const Float a, const Float b, const Float weight );

		// Lame interpolation of Euler angles
		RED_INLINE static EulerAngles Interpolate( const EulerAngles& a, const EulerAngles& b, const Float weight );

		// Interpolate to a given set of angles
		RED_INLINE void Interpolate( const EulerAngles& a, const Float weight );

	public:
		// Convert this Euler rotation to matrix form
		REDMATH_API Matrix ToMatrix() const;

		// Convert this Euler rotation to matrix form. Alternative version, eliminates the use of the operator=
		REDMATH_API void ToMatrix( Matrix& out_matrix ) const;

		// Calculate angle vectors
		REDMATH_API void ToAngleVectors( Vector4* forward, Vector4* right, Vector4* up ) const;

		// Calculate quaternion
		REDMATH_API Quaternion ToQuat() const;

		// Transform point directly by the rotation (SLOW!)
		REDMATH_API Vector4 TransformPoint( const Vector4& a ) const;

		// Transform vector directly by the rotation (SLOW!)
		REDMATH_API Vector4 TransformVector( const Vector4& a ) const;

	public:
		// Predefined values
		RED_INLINE static EulerAngles ZEROS();

		Float Roll = 0.f;			//!< Rotation in degrees around the Y axis
		Float Pitch = 0.f;			//!< Rotation in degrees around the X axis
		Float Yaw = 0.f;			//!< Rotation in degrees around the Z axis
	};

} // math