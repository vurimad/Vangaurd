/**
* Copyright (c) 2026 RED Vanguard, All Rights Reserved.
*/

#pragma once

#include "box.h"

namespace math
{
	struct Vector4;

	/// Plane in 3D space, implemented using 4D vector
	struct Plane
	{
		Vector4 NormalDistance;

	public:
		enum ESide
		{
			PS_None	 = 0,
			PS_Front = 1,
			PS_Back  = 2,
			PS_Both	 = PS_Front | PS_Back
		};

	public:
		RED_INLINE Plane() {};
		RED_INLINE Plane( const Vector4& normal, const Vector4& point );
		RED_INLINE Plane( const Vector4& normal, const Float& distance );
		RED_INLINE Plane( const Vector4& p1, const Vector4& p2, const Vector4& p3 );

		RED_INLINE void SetPlane( const Vector4& normalAndDistance ) { NormalDistance = normalAndDistance; }
		RED_INLINE void SetPlane( const Vector4& normal, const Vector4& point );
		RED_INLINE void SetPlane( const Vector4& p1, const Vector4& p2, const Vector4& p3 );

		RED_INLINE Float DistanceTo( const Vector4& point ) const;
		RED_INLINE static Float DistanceTo( const Vector4& plane, const Vector4& point );
		RED_INLINE ESide GetSide( const Vector4& point ) const;
		RED_INLINE ESide GetSide( const Box& box ) const;
		RED_INLINE ESide GetSide( const Vector4& boxCenter, const Vector4& boxExtents ) const;
		RED_INLINE const Vector4& GetVectorRepresentation() const { return NormalDistance; }

		RED_INLINE Vector4 Project( const Vector4& point ) const;
		RED_INLINE Bool FrontIntersectLine( const Vector4& origin, const Vector4& direction, Vector4& intersectionPoint, Float &intersectionDistance ) const;
		RED_INLINE ESide IntersectLine( const Vector4& origin, const Vector4& direction, Vector4& intersectionPoint, Float &intersectionDistance ) const;
		RED_INLINE static Bool IntersectPlanes( const Plane& p0, const Plane& p1, Vector4& outOrigin, Vector4& outDirection );

		RED_INLINE Bool IsOk() const;

		RED_INLINE Plane operator-() const;
	};

}

#include "plane.hpp"