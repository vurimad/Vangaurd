/**
* Copyright (c) 2026 RED Vanguard, All Rights Reserved.
*/
#include "build.h"
#include "matrixDouble.h"

namespace math {

	void MatrixDouble::Import( const Matrix &src )
	{
		for ( Uint32 i=0; i<4; ++i )
		{
			for ( Uint32 j=0; j<4; ++j )
			{
				V[i][j] = src[i][j];
			}
		}
	}

	void MatrixDouble::Export( Matrix &dest ) const
	{
		for ( Uint32 i=0; i<4; ++i )
		{
			for ( Uint32 j=0; j<4; ++j )
			{
				dest[i][j] = (Float)V[i][j];
			}
		}
	}

	Double MatrixDouble::Det() const
	{
		Double det = 0.0;
		det += V[0][0] * CoFactor(0,0);
		det += V[0][1] * CoFactor(0,1); 
		det += V[0][2] * CoFactor(0,2); 
		det += V[0][3] * CoFactor(0,3); 
		return det;
	}

	Double MatrixDouble::CoFactor( Int32 i, Int32 j ) const
	{
	#define M( dx, dy ) V[ (i+dx)&3 ][ (j+dy) & 3 ]
		Double val = 0.0;
		val += M(1,1) * M(2,2) * M(3,3);
		val += M(1,2) * M(2,3) * M(3,1);
		val += M(1,3) * M(2,1) * M(3,2);
		val -= M(3,1) * M(2,2) * M(1,3);
		val -= M(3,2) * M(2,3) * M(1,1);
		val -= M(3,3) * M(2,1) * M(1,2);
		val *= ((i+j) & 1) ? -1.0f : 1.0f;
		return val; 
	#undef M
	}

	MatrixDouble MatrixDouble::FullInverted() const
	{
		MatrixDouble out;

		// Get determinant
		Double d = Det();
		if ( ::fabs(d) > 1e-12f )
		{
			Double id = 1.0 / d;

			// Invert matrix
			out.V[0][0] = CoFactor(0,0) * id;
			out.V[0][1] = CoFactor(1,0) * id;
			out.V[0][2] = CoFactor(2,0) * id;
			out.V[0][3] = CoFactor(3,0) * id;
			out.V[1][0] = CoFactor(0,1) * id;
			out.V[1][1] = CoFactor(1,1) * id;
			out.V[1][2] = CoFactor(2,1) * id;
			out.V[1][3] = CoFactor(3,1) * id;
			out.V[2][0] = CoFactor(0,2) * id;
			out.V[2][1] = CoFactor(1,2) * id;
			out.V[2][2] = CoFactor(2,2) * id;
			out.V[2][3] = CoFactor(3,2) * id;
			out.V[3][0] = CoFactor(0,3) * id;
			out.V[3][1] = CoFactor(1,3) * id;
			out.V[3][2] = CoFactor(2,3) * id;
			out.V[3][3] = CoFactor(3,3) * id;
		}
		else
		{
			out.Import( Matrix::IDENTITY() );
		}

		return out;
	}

	MatrixDouble MatrixDouble::operator*( const MatrixDouble& other ) const
	{
		return Mul( other, *this );
	}

	MatrixDouble MatrixDouble::Mul( const MatrixDouble& a, const MatrixDouble& b )
	{
		MatrixDouble ret;
		ret.V[0][0] = b.V[0][0] * a.V[0][0] + b.V[0][1] * a.V[1][0] + b.V[0][2] * a.V[2][0] + b.V[0][3] * a.V[3][0];
		ret.V[0][1] = b.V[0][0] * a.V[0][1] + b.V[0][1] * a.V[1][1] + b.V[0][2] * a.V[2][1] + b.V[0][3] * a.V[3][1];
		ret.V[0][2] = b.V[0][0] * a.V[0][2] + b.V[0][1] * a.V[1][2] + b.V[0][2] * a.V[2][2] + b.V[0][3] * a.V[3][2];
		ret.V[0][3] = b.V[0][0] * a.V[0][3] + b.V[0][1] * a.V[1][3] + b.V[0][2] * a.V[2][3] + b.V[0][3] * a.V[3][3];
		ret.V[1][0] = b.V[1][0] * a.V[0][0] + b.V[1][1] * a.V[1][0] + b.V[1][2] * a.V[2][0] + b.V[1][3] * a.V[3][0];
		ret.V[1][1] = b.V[1][0] * a.V[0][1] + b.V[1][1] * a.V[1][1] + b.V[1][2] * a.V[2][1] + b.V[1][3] * a.V[3][1];
		ret.V[1][2] = b.V[1][0] * a.V[0][2] + b.V[1][1] * a.V[1][2] + b.V[1][2] * a.V[2][2] + b.V[1][3] * a.V[3][2];
		ret.V[1][3] = b.V[1][0] * a.V[0][3] + b.V[1][1] * a.V[1][3] + b.V[1][2] * a.V[2][3] + b.V[1][3] * a.V[3][3];
		ret.V[2][0] = b.V[2][0] * a.V[0][0] + b.V[2][1] * a.V[1][0] + b.V[2][2] * a.V[2][0] + b.V[2][3] * a.V[3][0];
		ret.V[2][1] = b.V[2][0] * a.V[0][1] + b.V[2][1] * a.V[1][1] + b.V[2][2] * a.V[2][1] + b.V[2][3] * a.V[3][1];
		ret.V[2][2] = b.V[2][0] * a.V[0][2] + b.V[2][1] * a.V[1][2] + b.V[2][2] * a.V[2][2] + b.V[2][3] * a.V[3][2];
		ret.V[2][3] = b.V[2][0] * a.V[0][3] + b.V[2][1] * a.V[1][3] + b.V[2][2] * a.V[2][3] + b.V[2][3] * a.V[3][3];
		ret.V[3][0] = b.V[3][0] * a.V[0][0] + b.V[3][1] * a.V[1][0] + b.V[3][2] * a.V[2][0] + b.V[3][3] * a.V[3][0];
		ret.V[3][1] = b.V[3][0] * a.V[0][1] + b.V[3][1] * a.V[1][1] + b.V[3][2] * a.V[2][1] + b.V[3][3] * a.V[3][1];
		ret.V[3][2] = b.V[3][0] * a.V[0][2] + b.V[3][1] * a.V[1][2] + b.V[3][2] * a.V[2][2] + b.V[3][3] * a.V[3][2];
		ret.V[3][3] = b.V[3][0] * a.V[0][3] + b.V[3][1] * a.V[1][3] + b.V[3][2] * a.V[2][3] + b.V[3][3] * a.V[3][3];  
		return ret;
	}

} // math