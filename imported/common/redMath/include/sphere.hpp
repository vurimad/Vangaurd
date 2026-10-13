/**
* Copyright (c) 2026 RED Vanguard, All Rights Reserved.
*/

#pragma once

namespace math
{
	RED_FORCE_INLINE Sphere::Sphere( const Vector4& geometry )
		: CenterRadius( geometry )
	{}

	RED_FORCE_INLINE Sphere::Sphere( const Vector4& center, Float radius )
		: CenterRadius{ center.X, center.Y, center.Z, radius }
	{}

	RED_FORCE_INLINE Sphere::Sphere( Float c0, Float c1, Float c2, Float radius )
		: CenterRadius{ c0, c1, c2, radius }
	{}

	RED_FORCE_INLINE Sphere Sphere::operator+( const Vector4& dir ) const
	{
		return { CenterRadius + dir, CenterRadius.W };
	}

	RED_FORCE_INLINE Sphere Sphere::operator-( const Vector4& dir ) const
	{
		return { CenterRadius - dir, CenterRadius.W };
	}

	RED_INLINE void Sphere::operator+=( const Vector4& dir )
	{
		CenterRadius.Add3(dir);
	}

	RED_INLINE void Sphere::operator-=( const Vector4& dir )
	{
		CenterRadius.Sub3(dir);
	}

	RED_INLINE Vector4 Sphere::GetCenter() const
	{
        return { CenterRadius.X, CenterRadius.Y, CenterRadius.Z, 1.0 };
	}

	RED_INLINE Float Sphere::GetSquareRadius() const
	{
		return CenterRadius.W * CenterRadius.W;
	}

	RED_INLINE Float Sphere::GetRadius() const
	{
		return CenterRadius.W;
	}

	RED_INLINE Float Sphere::GetDistance( const Vector4& point ) const
	{
		return Vector4::Sub3( CenterRadius, point ).Mag3() - CenterRadius.W;
	}

	RED_INLINE Float Sphere::GetDistance( const Sphere& sphere ) const
	{
		return GetDistance( sphere.GetCenter() ) - sphere.GetRadius();
	}

	RED_INLINE Bool Sphere::Contains( const Vector4& point ) const
	{
		return Vector4::Sub3( CenterRadius, point ).SquareMag3() <= CenterRadius.W * CenterRadius.W;
	}

	RED_INLINE Bool Sphere::Contains( const Sphere& sphere ) const
	{
		// math: dist^2 <= (r1 - r2)^2 = r1^2 - 2*r1*r2 + r2^2
		const Float distSq = Vector4::Sub3( CenterRadius, sphere.GetCenter() ).SquareMag3();
		const Float r1 = CenterRadius.W;
		const Float r2 = sphere.CenterRadius.W;
		const Float rSq = r1 * ( r1 - 2.f*r2 ) + r2*r2;

		return distSq <= rSq;
	}

	RED_INLINE Bool Sphere::Touches( const Sphere& sphere ) const
	{
		return GetDistance(sphere) <= 0;
	}

} // math