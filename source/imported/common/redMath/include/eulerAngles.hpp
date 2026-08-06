/**
* Copyright (c) 2026 RED Vanguard, All Rights Reserved.
*/
#pragma once

namespace math
{

	EulerAngles::EulerAngles( const Float roll, const Float pitch, const Float yaw )
		: Roll( roll ), Pitch( pitch ), Yaw( yaw )
	{}

	EulerAngles::EulerAngles( const EulerAngles &ea )
		: EulerAngles{ ea.Roll, ea.Pitch, ea.Yaw }
	{}

	EulerAngles::EulerAngles( const Vector3& v )
		: EulerAngles{ v.X, v.Y, v.Z }
	{}

	EulerAngles::EulerAngles( const Float f[ 3 ] )
		: EulerAngles{ f[ 0 ], f[ 1 ], f[ 2 ] }
	{}

	EulerAngles EulerAngles::operator-() const
	{
		return { -Roll, -Pitch, -Yaw };
	}

	EulerAngles EulerAngles::operator+( const EulerAngles& a ) const
	{
		return { Roll + a.Roll, Pitch + a.Pitch, Yaw + a.Yaw };
	}

	EulerAngles EulerAngles::operator-( const EulerAngles& a ) const
	{
		return { Roll - a.Roll, Pitch - a.Pitch, Yaw - a.Yaw };
	}

	EulerAngles EulerAngles::operator*( const EulerAngles& a ) const
	{
		return { Roll * a.Roll, Pitch * a.Pitch, Yaw * a.Yaw };
	}

	EulerAngles EulerAngles::operator/( const EulerAngles& a ) const
	{
		return { Roll / a.Roll, Pitch / a.Pitch, Yaw / a.Yaw };
	}

	EulerAngles EulerAngles::operator+( const Float a ) const
	{
		return { Roll + a, Pitch + a, Yaw + a };
	}

	EulerAngles EulerAngles::operator-( const Float a ) const
	{
		return { Roll - a, Pitch - a, Yaw - a };
	}

	EulerAngles EulerAngles::operator*( const Float a ) const
	{
		return { Roll * a, Pitch * a, Yaw * a };
	}

	EulerAngles EulerAngles::operator/( const Float a ) const
	{
		return { Roll / a, Pitch / a, Yaw / a };
	}

	RED_INLINE EulerAngles& EulerAngles::operator+=( const EulerAngles& a )
	{
		Roll += a.Roll;
		Pitch += a.Pitch;
		Yaw += a.Yaw;
		return *this;
	}

	RED_INLINE EulerAngles& EulerAngles::operator-=( const EulerAngles& a )
	{
		Roll -= a.Roll;
		Pitch -= a.Pitch;
		Yaw -= a.Yaw;
		return *this;
	}

	RED_INLINE EulerAngles& EulerAngles::operator*=( const EulerAngles& a )
	{
		Roll *= a.Roll;
		Pitch *= a.Pitch;
		Yaw *= a.Yaw;
		return *this;
	}

	RED_INLINE EulerAngles& EulerAngles::operator/=( const EulerAngles& a )
	{
		Roll /= a.Roll;
		Pitch /= a.Pitch;
		Yaw /= a.Yaw;
		return *this;
	}

	RED_INLINE EulerAngles& EulerAngles::operator+=( const Float a )
	{
		Roll += a;
		Pitch += a;
		Yaw += a;
		return *this;
	}

	RED_INLINE EulerAngles& EulerAngles::operator-=( const Float a )
	{
		Roll -= a;
		Pitch -= a;
		Yaw -= a;
		return *this;
	}

	RED_INLINE EulerAngles& EulerAngles::operator*=( const Float a )
	{
		Roll *= a;
		Pitch *= a;
		Yaw *= a;
		return *this;
	}

	RED_INLINE EulerAngles& EulerAngles::operator/=( const Float a )
	{
		Roll /= a;
		Pitch /= a;
		Yaw /= a;
		return *this;
	}

	Bool EulerAngles::operator==( const EulerAngles& a ) const
	{
		return ( Roll == a.Roll ) && ( Pitch == a.Pitch ) && ( Yaw == a.Yaw );
	}

	Bool EulerAngles::operator!=( const EulerAngles& a ) const
	{
		return ( Roll != a.Roll ) || ( Pitch != a.Pitch ) || ( Yaw != a.Yaw );
	}

	RED_INLINE Bool EulerAngles::AlmostEquals( const EulerAngles& a, const Float epsilon /* = 0.01f */ ) const
	{
		Float r = Abs< Float >( Roll - a.Roll );
		Float p = Abs< Float >( Pitch - a.Pitch );
		Float y = Abs< Float >( Yaw - a.Yaw );
		Float hl = 360.f - epsilon;				// That limit is important to compare angles like 0.00001 and 359.9999999
		return (r < epsilon || r > hl) &&
			 (p < epsilon || p > hl) &&
			 (y < epsilon || y > hl);
	}

	RED_INLINE Float EulerAngles::NormalizeAngle( Float angle )
	{
		Int32 cycles = (Int32)angle / 360;
		if ( cycles != 0 ) angle -= cycles * 360.0f; // angle e ( -360, 360 )
		if ( angle < 0 ) angle += 360; // angle e ( 0, 360 )
		return angle;
	}

	RED_INLINE EulerAngles& EulerAngles::Normalize()
	{
		Roll = NormalizeAngle( Roll );
		Pitch = NormalizeAngle( Pitch );
		Yaw = NormalizeAngle( Yaw );
		return *this;
	}

	RED_INLINE Float EulerAngles::NormalizeAngle180( Float angle )
	{
		angle = NormalizeAngle( angle );
		return angle > 180.f ? angle - 360.f : angle;
	}

	RED_INLINE Float EulerAngles::ToNearestAngle( Float angle, const Float referenceAngle )
	{
		angle = NormalizeAngle( angle );
		while ( angle + 180.f < referenceAngle )
		{
			angle += 360.f;
		}
		while ( angle - 180.f > referenceAngle )
		{
			angle -= 360.f;
		}
		return angle;
	}

	RED_INLINE Float EulerAngles::YawFromXY( const Float x, const Float y )
	{
		if ( fabsf( x ) < 0.0001f && fabsf( y ) < 0.0001f )
			return 0.0f;

		return RAD2DEG( -atan2f( x,y ) );
	}

	RED_INLINE Double EulerAngles::YawFromXY( const Double x, const Double y )
	{
		if ( fabs( x ) < 0.0001 && fabs( y ) < 0.0001 )
			return 0.0;

		return RAD2DEG( -atan2( x,y ) );
	}

	RED_INLINE Vector4 EulerAngles::YawToVector( const Float yaw )
	{
		Float angle = DEG2RAD( yaw );
		return { -MSin( angle ), MCos( angle ), 0.f, 0.f };
	}

	RED_INLINE Vector2 EulerAngles::YawToVector2( const Float yaw )
	{
		Float angle = DEG2RAD( yaw );
		return { -MSin( angle ), MCos( angle ) };
	}

	RED_INLINE Float EulerAngles::AngleDistance( const Float a, const Float b )
	{
		Float delta = EulerAngles::NormalizeAngle( b ) - EulerAngles::NormalizeAngle( a );

		// Get shortest distance
		if ( delta < -180.0f )
		{
			return delta + 360.0f;
		}
		else if ( delta > 180.0f )
		{
			return delta - 360.0f;
		}
		else
		{
			return delta;
		}
	}

	RED_INLINE EulerAngles EulerAngles::AngleDistance( const EulerAngles& a, const EulerAngles& b )
	{
		EulerAngles ret;

		ret.Pitch = AngleDistance( a.Pitch, b.Pitch );
		ret.Roll = AngleDistance( a.Roll, b.Roll );
		ret.Yaw = AngleDistance( a.Yaw, b.Yaw );

		return ret;
	}

	RED_INLINE Float EulerAngles::Interpolate( const Float a, const Float b, const Float weight )
	{
		const Float diff = AngleDistance( a, b );
		return a + diff * weight;
	}

	RED_INLINE EulerAngles EulerAngles::Interpolate( const EulerAngles& a, const EulerAngles& b, const Float weight )
	{
		const EulerAngles diff = AngleDistance( a, b );

		return a + diff * weight;
	}

	RED_INLINE void EulerAngles::Interpolate( const EulerAngles& a, const Float weight )
	{
		operator+=( AngleDistance( *this, a ) * weight );
	}

	EulerAngles EulerAngles::ZEROS()
	{
		return { 0.f, 0.f, 0.f };
	}

} // math