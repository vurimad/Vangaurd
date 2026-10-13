/**
* Copyright (c) 2026 RED Vanguard, All Rights Reserved.
*/

#pragma once

namespace math
{

	// 4x4 Matrix, Row Major, Double precision
	// NOTE: very slow
	struct REDMATH_API MatrixDouble
	{
	public:
		Double	V[4][4];

		// Import data from normal matrix
		void Import( const Matrix &src );

		// Export data to normal matrix
		void Export( Matrix &dest ) const;

		// Compute matrix determinant
		Double Det() const;

		// Compute matrix cofactor (determinant of inner 3x3 matrix)
		Double CoFactor( Int32 i, Int32 j ) const;

		// Compute full inversion of the matrix (very, very slow)
		MatrixDouble FullInverted() const;

		// Multiply matrices (very, very slow)
		MatrixDouble operator*( const MatrixDouble& other ) const;

		// Multiply matrices (very, very slow)
		static MatrixDouble Mul( const MatrixDouble& a, const MatrixDouble& b );
	};

} // math
