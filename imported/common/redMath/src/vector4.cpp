#include "build.h"

namespace math
{

	EulerAngles Vector4::ToEulerAnglesInversePitch() const
	{
		Float yaw, pitch;

		if ( !X && !Y )
		{
			yaw = 0.0f; 
			pitch = (Z > 0) ? 90.0f : -90.0f;
		}
		else
		{
			yaw = RAD2DEG( -atan2f( X,Y ) );
			pitch = RAD2DEG( atan2f( -Z, sqrtf( X*X + Y*Y )) );
		}

		return EulerAngles( 0.0f, pitch, yaw );
	}

	EulerAngles Vector4::ToEulerAngles() const
	{
		Float yaw, pitch;

		const Float xyLengthSquared = X * X + Y * Y;
		if ( !xyLengthSquared )
		{
			yaw = 0.f; 
			pitch = Z > 0.f ? 90.f : -90.f;
		}
		else
		{
			// To make sure atan2f result is the same for Release Orbis and other configurations when Y < 0 and X == 0 (signed or unsigned zero)
			const Float x = X == -0.f ? +0.f : X;

			yaw = RAD2DEG( -atan2f( x, Y ) );
			pitch = RAD2DEG( atanf( Z / sqrtf( xyLengthSquared ) ) );
		}

		return EulerAngles( 0.f, pitch, yaw );
	}

} // math