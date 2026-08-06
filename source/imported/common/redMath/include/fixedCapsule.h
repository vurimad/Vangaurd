/**
* Copyright (c) 2026 RED Vanguard, All Rights Reserved.
*/

#pragma once

namespace math
{

	/********************************/
	/* FixedCapsule					*/
	/*	__							*/
	/* /  \							*/
	/* |  |	<--- Point B			*/
	/* |  |							*/
	/* |  |	<--- Point A			*/
	/* \__/							*/
	/*	 <--- Position				*/
	/********************************/

	// Upright standing capsule defined via position, radius and height, height doesn't include two caps
	struct FixedCapsule
	{
		Vector4 PointRadius;
		Float Height;

		// Undefined constructor
		FixedCapsule() = default;

		// Define via capsule point (base), radius and height
		FixedCapsule( const Vector4& point, Float radius, Float height );

		// Get center of mass (center of capsule)
		Vector4 GetMassCenter() const;

		// Get mass, assuming density 1.0f 
		Float GetMass() const;

		// Change parameters
		void Set( const Vector4& point, Float radius, Float height );

		// Gets capsule's position
		Vector4 GetPosition() const;

		// Get generalized orientation (zero in case of capsules)
		EulerAngles GetOrientation() const;

		// Calc capsule's start position
		Vector4 CalcPointA() const;

		// Calc capsule's end position
		Vector4 CalcPointB() const;

		// Gets capsule's radius
		Float GetRadius() const;

		// Gets capsule's height
		Float GetHeight() const;

		// Return box translated by a vector
		FixedCapsule operator+( const Vector4& dir ) const;

		// Return box translated by a -vector
		FixedCapsule operator-( const Vector4& dir ) const;

		// Translate by a vector
		FixedCapsule& operator+=( const Vector4& dir );

		// Translate by a -vector
		FixedCapsule& operator-=( const Vector4& dir );

		// Check if capsule contains point
		REDMATH_API Bool Contains( const Vector4& point ) const;

		// Check if sphere contains sphere
		REDMATH_API Bool Contains( const Sphere& sphere ) const;
	};

} // math

#include "fixedCapsule.hpp"