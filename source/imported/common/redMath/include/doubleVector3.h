/**
* Copyright © 2007 CD Projekt Red. All Rights Reserved.
*/

#pragma once

#include "../../redSystem/include/compilerExtensions.h"

namespace math
{

	/// Last resort structure to represent accurate positions
	/// NOTE: this is ___unimaginably___ slow on consoles.
	/// NOTE: this class can be used only on the offline site if at all.
	/// NOTE: nothing here is written with performance in mind.
	class DoubleVector3
	{
	public:
		Double X,Y,Z;

	public:
		RED_FORCE_INLINE DoubleVector3() {}; // No zero initialization constructor
		RED_FORCE_INLINE DoubleVector3( Double _x, Double _y, Double _z );
		RED_FORCE_INLINE DoubleVector3( const Double* ptr );
		RED_FORCE_INLINE DoubleVector3( const DoubleVector3& other );

		// conversion from single precission floating point vectors requires providing error term :) It's on your conciousness if you provide zeros...
		RED_FORCE_INLINE DoubleVector3( const Vector3& vec, const Vector3& vecError );
		RED_FORCE_INLINE DoubleVector3( const Vector& vec, const Vector3& vecError );

		// assignment is supported only form other vector of doubles
		RED_FORCE_INLINE DoubleVector3& operator=( const DoubleVector3& other );

		// Conversion to vector3, the error term is your responsibility (i.e. the conversion is anoying for a reason)
		RED_FORCE_INLINE Vector3 ToVector3( Vector3& errorTerm ) const;

		// Conversion to vector, the error term is your responsibility (i.e. the conversion is anoying for a reason)
		RED_FORCE_INLINE Vector ToVector( Vector3& errorTerm ) const;

	public:
		// Addition
		RED_FORCE_INLINE DoubleVector3 operator+( const DoubleVector3& other ) const;
		RED_FORCE_INLINE DoubleVector3& operator+=( const DoubleVector3& other );

		// Subtraction
		RED_FORCE_INLINE DoubleVector3 operator-( const DoubleVector3& other ) const;
		RED_FORCE_INLINE DoubleVector3& operator-=( const DoubleVector3& other );

		// Multiplication
		RED_FORCE_INLINE DoubleVector3 operator*( const DoubleVector3& other ) const;
		RED_FORCE_INLINE DoubleVector3& operator*=( const DoubleVector3& other );
		RED_FORCE_INLINE DoubleVector3 operator*( Double scalar ) const;
		RED_FORCE_INLINE DoubleVector3& operator*=( Double scalar );

		// Division (note: don't divide by zero)
		RED_FORCE_INLINE DoubleVector3 operator/( const DoubleVector3& other ) const;
		RED_FORCE_INLINE DoubleVector3& operator/=( const DoubleVector3& other );
		RED_FORCE_INLINE DoubleVector3 operator/( Double scalar ) const;
		RED_FORCE_INLINE DoubleVector3& operator/=( Double scalar );

		// Compute magnitude of the vector
		RED_FORCE_INLINE Double Mag() const;

		// Compute magnitude of vector difference
		RED_FORCE_INLINE Double Distance( const DoubleVector3& other ) const;

		// Static, predefined vectors
		RED_FORCE_INLINE static const DoubleVector3& EX(); // 1,0,0
		RED_FORCE_INLINE static const DoubleVector3& EY(); // 0,1,0
		RED_FORCE_INLINE static const DoubleVector3& EZ(); // 0,0,1
		RED_FORCE_INLINE static const DoubleVector3& ZEROS(); // 0,0,0
		RED_FORCE_INLINE static const DoubleVector3& ONES(); // 1,1,1
	};

}
