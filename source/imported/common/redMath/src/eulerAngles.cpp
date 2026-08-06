#include "build.h"

namespace math
{

	Matrix EulerAngles::ToMatrix() const
	{
		// The rotation matrix for the Euler angles
		// Order of rotation: Y ( roll ), X ( pitch ), Z ( yaw )
		// All rotations are CCW

		Float cosRoll = ::MCos( DEG2RAD( Roll ) );
		Float sinRoll = MSin( DEG2RAD( Roll ) );
		Float cosPitch = ::MCos( DEG2RAD( Pitch ) );
		Float sinPitch = MSin( DEG2RAD( Pitch ) );
		Float cosYaw = ::MCos( DEG2RAD( Yaw ) );
		Float sinYaw = MSin( DEG2RAD( Yaw ) );

		Matrix ret;
		ret.SetIdentity();
		ret[0][0] = cosRoll * cosYaw - sinPitch * sinRoll * sinYaw;
		ret[0][1] = sinPitch * sinRoll * cosYaw + cosRoll * sinYaw;
		ret[0][2] = -cosPitch * sinRoll;
		ret[1][0] = cosPitch * -sinYaw;
		ret[1][1] = cosPitch * cosYaw;
		ret[1][2] = sinPitch;
		ret[2][0] = sinPitch * cosRoll * sinYaw + sinRoll * cosYaw;
		ret[2][1] = sinRoll * sinYaw - sinPitch * cosRoll * cosYaw;
		ret[2][2] = cosPitch * cosRoll;
		return ret;
	}

	void EulerAngles::ToMatrix( Matrix& out_matrix ) const
	{
		// ab> same as above, except will eliminate use of the operator=

		// The rotation matrix for the Euler angles
		// Order of rotation: Y ( roll ), X ( pitch ), Z ( yaw )
		// All rotations are CCW

		Float cosRoll = ::MCos( DEG2RAD( Roll ) );
		Float sinRoll = MSin( DEG2RAD( Roll ) );
		Float cosPitch = ::MCos( DEG2RAD( Pitch ) );
		Float sinPitch = MSin( DEG2RAD( Pitch ) );
		Float cosYaw = ::MCos( DEG2RAD( Yaw ) );
		Float sinYaw = MSin( DEG2RAD( Yaw ) );

		out_matrix[0][0] = cosRoll * cosYaw - sinPitch * sinRoll * sinYaw;
		out_matrix[0][1] = sinPitch * sinRoll * cosYaw + cosRoll * sinYaw;
		out_matrix[0][2] = -cosPitch * sinRoll;
		out_matrix[0][3] = 0.0f;

		out_matrix[1][0] = cosPitch * -sinYaw;
		out_matrix[1][1] = cosPitch * cosYaw;
		out_matrix[1][2] = sinPitch;
		out_matrix[1][3] = 0.0f;

		out_matrix[2][0] = sinPitch * cosRoll * sinYaw + sinRoll * cosYaw;
		out_matrix[2][1] = sinRoll * sinYaw - sinPitch * cosRoll * cosYaw;
		out_matrix[2][2] = cosPitch * cosRoll;
		out_matrix[2][3] = 0.0f;

		out_matrix[3][0] = 0.0f;
		out_matrix[3][1] = 0.0f;
		out_matrix[3][2] = 0.0f;
		out_matrix[3][3] = 1.0f;
	}

	Quaternion EulerAngles::ToQuat() const
	{
		Quaternion q;

		const Float c1 = ::MCos( 0.5f * DEG2RAD( Yaw ) );
		const Float c2 = ::MCos( 0.5f * DEG2RAD( Pitch ) );
		const Float c3 = ::MCos( 0.5f * DEG2RAD( Roll ) );

		const Float s1 = MSin( 0.5f * DEG2RAD( Yaw ) );
		const Float s2 = MSin( 0.5f * DEG2RAD( Pitch ) );
		const Float s3 = MSin( 0.5f * DEG2RAD( Roll ) );

		q.r = c1 * c2 * c3 - s1 * s2 * s3;
		q.i = c1 * s2 * c3 - s1 * c2 * s3;
		q.j = s1 * s2 * c3 + c1 * c2 * s3;
		q.k = s1 * c2 * c3 + c1 * s2 * s3;

		return q.Normalized();
	}

	Vector4 EulerAngles::TransformPoint( const Vector4& a ) const
	{
		Matrix mat = ToMatrix();
		return mat.TransformPoint( a );
	}

	Vector4 EulerAngles::TransformVector( const Vector4& a ) const
	{
		Matrix mat = ToMatrix();
		return mat.TransformVector( a );
	}

	void EulerAngles::ToAngleVectors( Vector4* forward, Vector4* right, Vector4* up) const
	{
		Vector4 f( 0, 1, 0 );
		Vector4 r( 1, 0, 0 );
		Vector4 u( 0, 0, 1 );

		Matrix mat = ToMatrix();

		if ( forward )
		{
			*forward = mat.TransformVector( f );
			forward->Normalize3();
		}

		if ( right )
		{
			*right = mat.TransformVector( r );
			right->Normalize3();
		}

		if ( up )
		{
			*up = mat.TransformVector( u );
			up->Normalize3();
		}
	}

} // math