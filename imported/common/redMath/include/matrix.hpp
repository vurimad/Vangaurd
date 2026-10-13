/**
* Copyright (c) 2026 RED Vanguard, All Rights Reserved.
*/
#pragma once

namespace math
{
	RED_INLINE Matrix::Matrix( const Float* f )
		: Matrix{ f[0], f[1], f[2], f[3], f[4], f[5], f[6], f[7], f[8], f[9], f[10], f[11], f[12], f[13], f[14], f[15] }
	{}

	RED_FORCE_INLINE Matrix::Matrix( const Matrix& m )
		: Matrix{ m.X, m.Y, m.Z, m.W }
	{}

	RED_FORCE_INLINE Matrix::Matrix( const Vector4& x, const Vector4& y, const Vector4& z, const Vector4& w )
		: X(x), Y(y), Z(z), W(w)
	{}

	RED_INLINE Matrix::Matrix( const Float _00, const Float _01, const Float _02, const Float _03,
							  const Float _10, const Float _11, const Float _12, const Float _13,
							  const Float _20, const Float _21, const Float _22, const Float _23,
							  const Float _30, const Float _31, const Float _32, const Float _33 )
		: Matrix{ { _00, _01, _02, _03 }, 
				  { _10, _11, _12, _13 }, 
				  { _20, _21, _22, _23 }, 
				  { _30, _31, _32, _33 } }
	{}

	RED_FORCE_INLINE Matrix& Matrix::Set( const Matrix& a )
	{
		X = a.X;
		Y = a.Y;
		Z = a.Z;
		W = a.W;
		return *this;
	}

	RED_INLINE Bool Matrix::operator==( const Matrix& a ) const
	{
		return ( X == a.X ) && ( Y == a.Y ) && ( Z == a.Z ) && ( W == a.W );
	}

	RED_FORCE_INLINE Bool Matrix::operator!=( const Matrix& a ) const
	{
		return !(*this == a );
	}

	RED_FORCE_INLINE Matrix& Matrix::operator=( const Matrix& a )
	{
		X = a.X;
		Y = a.Y;
		Z = a.Z;
		W = a.W;
		return *this;
	}

	RED_FORCE_INLINE Matrix& Matrix::SetRows( const Float* f )
	{
		X = Vector4( f + 0 );
		Y = Vector4( f + 4 );
		Z = Vector4( f + 8 );
		W = Vector4( f + 12 );
		return *this;
	}

	RED_INLINE Matrix& Matrix::SetCols( const Float* f )
	{
		X[0] = f[0];
		X[1] = f[4];
		X[2] = f[8];
		X[3] = f[12];
		Y[0] = f[1];
		Y[1] = f[5];
		Y[2] = f[9];
		Y[3] = f[13];
		Z[0] = f[2];
		Z[1] = f[6];
		Z[2] = f[10];
		Z[3] = f[14];
		W[0] = f[3];
		W[1] = f[6];
		W[2] = f[11];
		W[3] = f[15];
		return *this;
	}

	RED_INLINE Matrix& Matrix::SetRows( const Vector4& x, const Vector4& y, const Vector4& z, const Vector4& w )
	{
		X = x;
		Y = y;
		Z = z;
		W = w;
		return *this;
	}

	RED_INLINE Matrix& Matrix::SetCols( const Vector4& x, const Vector4& y, const Vector4& z, const Vector4& w )
	{
		X[0] = x.X;
		Y[0] = x.Y;
		Z[0] = x.Z;
		W[0] = x.W;	
		X[1] = y.X;
		Y[1] = y.Y;
		Z[1] = y.Z;
		W[1] = y.W;	
		X[2] = z.X;
		Y[2] = z.Y;
		Z[2] = z.Z;
		W[2] = z.W;	
		X[3] = w.X;
		Y[3] = w.Y;
		Z[3] = w.Z;
		W[3] = w.W;
		return *this;
	}

	RED_FORCE_INLINE Matrix& Matrix::SetZeros()
	{
		X.SetZeros();
		Y.SetZeros();
		Z.SetZeros();
		W.SetZeros();
		return *this;
	}

	RED_FORCE_INLINE Matrix& Matrix::SetIdentity()
	{
		X = Vector4::EX();
		Y = Vector4::EY();
		Z = Vector4::EZ();
		W = Vector4::EW();
		return *this;
	}

	RED_INLINE Matrix& Matrix::Set33( const Matrix& a )
	{
		X[0] = a.X[0];
		X[1] = a.X[1];
		X[2] = a.X[2];
		Y[0] = a.Y[0];
		Y[1] = a.Y[1];
		Y[2] = a.Y[2];
		Z[0] = a.Z[0];
		Z[1] = a.Z[1];
		Z[2] = a.Z[2];
		return *this;
	}

	RED_INLINE Matrix& Matrix::Set33( const Vector4& x, const Vector4& y, const Vector4& z )
	{
		X[0] = x.X;
		X[1] = x.Y;
		X[2] = x.Z;
		Y[0] = y.X;
		Y[1] = y.Y;
		Y[2] = y.Z;
		Z[0] = z.X;
		Z[1] = z.Y;
		Z[2] = z.Z;
		return *this;
	}

	RED_INLINE Matrix& Matrix::SetRotX33( Float ccwRadians )
	{
		Float rotC = ::MCos( ccwRadians );
		Float rotS = MSin( ccwRadians );

		// CCW rotation around X axis
		// [ 1   0        0        ]
		// [ 0   MCos(a)  MSin(a)  ]
		// [ 0   -MSin(a) MCos(a)  ]
		//
		// Given [0,1,0] rotation order is
		//  0:   Y+
		//  90   Z+
		//  180: Y-
		//  270: Z-
		//  360: Y+

		X[0] = 1.0f;
		X[1] = 0.0f;
		X[2] = 0.0f;
		Y[0] = 0.0f;
		Y[1] = rotC;
		Y[2] = rotS;
		Z[0] = 0.0f;
		Z[1] = -rotS;
		Z[2] = rotC;

		return *this;
	}

	RED_INLINE Matrix& Matrix::SetRotY33( Float ccRadians )
	{
		Float rotC = ::MCos( ccRadians );
		Float rotS = MSin( ccRadians );

		// CCW rotation around Y axis
		// [ MCos(a)   0   -MSin(a)  ]
		// [ 0         1    0        ]
		// [ MSin(a)   0    MCos(a)  ]
		//
		// Given [1,0,0] rotation order is
		//  0:   X+
		//  90   Z-
		//  180: X-
		//  270: Z+
		//  360: X+

		X[0] = rotC;
		X[1] = 0.0f;
		X[2] = -rotS;
		Y[0] = 0.0f;
		Y[1] = 1.0f;
		Y[2] = 0.0f;
		Z[0] = rotS;
		Z[1] = 0.0f;
		Z[2] = rotC;
		return *this;
	}

	RED_INLINE Matrix& Matrix::SetRotZ33( Float ccRadians )
	{
		Float rotC = ::MCos( ccRadians );
		Float rotS = MSin( ccRadians );

		// CCW rotation around Z axis
		// [ MCos(a)   MSin(a)  0 ]
		// [ -MSin(a)  MCos(a)  0 ]
		// [ 0         0        1 ]
		//
		// Given [1,0,0] rotation order is
		//  0:   X+
		//  90   Y+
		//  180: X-
		//  270: Y-
		//  360: X+

		X[0] = rotC;
		X[1] = rotS;
		X[2] = 0.0f;
		Y[0] = -rotS;
		Y[1] = rotC;
		Y[2] = 0.0f;
		Z[0] = 0.0f;
		Z[1] = 0.0f;
		Z[2] = 1.0f;
		return *this;
	}

	RED_INLINE Matrix& Matrix::SetScale( const Vector4& scale )
	{
		X[0] = scale.X;
		Y[1] = scale.Y;
		Z[2] = scale.Z;
		return *this;
	}

	RED_INLINE Matrix& Matrix::SetScale33( const Vector4& scale )
	{
		X[0] *= scale.X;
		X[1] *= scale.X;
		X[2] *= scale.X;
		Y[0] *= scale.Y;
		Y[1] *= scale.Y;
		Y[2] *= scale.Y;
		Z[0] *= scale.Z;
		Z[1] *= scale.Z;
		Z[2] *= scale.Z;
		return *this;
	}

	RED_INLINE Matrix& Matrix::SetScale44( const Vector4& scale )
	{
		X[0] *= scale.X;
		X[1] *= scale.X;
		X[2] *= scale.X;
		X[3] *= scale.X;
		Y[0] *= scale.Y;
		Y[1] *= scale.Y;
		Y[2] *= scale.Y;
		Y[3] *= scale.Y;
		Z[0] *= scale.Z;
		Z[1] *= scale.Z;
		Z[2] *= scale.Z;
		Z[3] *= scale.Z;
		return *this;
	}

	RED_INLINE Vector4 Matrix::GetScale33() const
	{
		return { X.Mag3(), Y.Mag3(), Z.Mag3() };
	}

	RED_INLINE Matrix& Matrix::SetPreScale33( const Vector4& scale )
	{
		X[0] *= scale.X;
		X[1] *= scale.Y;
		X[2] *= scale.Z;
		Y[0] *= scale.X;
		Y[1] *= scale.Y;
		Y[2] *= scale.Z;
		Z[0] *= scale.X;
		Z[1] *= scale.Y;
		Z[2] *= scale.Z;
		return *this;
	}

	RED_INLINE Matrix& Matrix::SetPreScale44( const Vector4& scale )
	{
		X[0] *= scale.X;
		X[1] *= scale.Y;
		X[2] *= scale.Z;
		Y[0] *= scale.X;
		Y[1] *= scale.Y;
		Y[2] *= scale.Z;
		Z[0] *= scale.X;
		Z[1] *= scale.Y;
		Z[2] *= scale.Z;
		W[0] *= scale.X;
		W[1] *= scale.Y;
		W[2] *= scale.Z;
		return *this;
	}

	RED_INLINE Vector4 Matrix::GetPreScale33() const
	{
		Vector4 v0{ X[0], Y[0], Z[0] };
		Vector4 v1{ X[1], Y[1], Z[1] };
		Vector4 v2{ X[2], Y[2], Z[2] };
		return { v0.Mag3(), v1.Mag3(), v2.Mag3() };
	}

	RED_INLINE Matrix& Matrix::SetScale33( Float uniformScale )
	{
		X[0] *= uniformScale;
		X[1] *= uniformScale;
		X[2] *= uniformScale;
		Y[0] *= uniformScale;
		Y[1] *= uniformScale;
		Y[2] *= uniformScale;
		Z[0] *= uniformScale;
		Z[1] *= uniformScale;
		Z[2] *= uniformScale;
		return *this;
	}

	RED_INLINE Matrix& Matrix::SetTranslation( const Vector4& a )
	{
		W[0] = a.X;
		W[1] = a.Y;
		W[2] = a.Z;
		W[3] = 1.0f;
		return *this;
	}

	RED_INLINE Matrix& Matrix::SetTranslation( Float x, Float y, Float z )
	{
		W[0] = x;
		W[1] = y;
		W[2] = z;
		W[3] = 1.0f;
		return *this;
	}

	RED_INLINE Vector4 Matrix::GetTranslation() const
	{
		return { W.X, W.Y, W.Z, 1.f };
	}

	RED_FORCE_INLINE const Vector4& Matrix::GetTranslationRef() const
	{
		return W;
	}

	RED_FORCE_INLINE Vector4& Matrix::GetTranslationRef()
	{
		return W;
	}

	RED_FORCE_INLINE Matrix& Matrix::OrthonormInvert()
	{
		*this = OrthonormInverted();
		return *this;
	}

	RED_FORCE_INLINE Matrix& Matrix::FullInvert()
	{
		*this = FullInverted();
		return *this;
	}

	RED_FORCE_INLINE Matrix& Matrix::Transpose()
	{
		*this = Transposed();
		return *this;
	}

	RED_INLINE Vector4 Matrix::GetColumn( const size_t index ) const
	{
		return { X[ index ], Y[ index ], Z[ index ], W[ index ] };
	}

	RED_INLINE Matrix& Matrix::SetColumn( const size_t index, const Vector4& a )
	{
		X[ index ] = a.X;
		Y[ index ] = a.Y;
		Z[ index ] = a.Z;
		W[ index ] = a.W;
		return *this;
	}

	RED_FORCE_INLINE Vector4 Matrix::GetRow( const size_t index ) const
	{
		return operator[](index);
	}

	RED_FORCE_INLINE Matrix& Matrix::SetRow( const size_t index, const Vector4& a )
	{
		operator[](index) = a;
		return *this;
	}

	RED_INLINE Vector4 Matrix::GetAxisX() const
	{
		return { X.X, X.Y, X.Z, 0.f };
	}

	RED_INLINE Vector4 Matrix::GetAxisY() const
	{
		return { Y.X, Y.Y, Y.Z, 0.f };
	} 

	RED_INLINE Vector4 Matrix::GetAxisZ() const
	{
		return { Z.X, Z.Y, Z.Z, 0.f };
	}

	RED_INLINE void Matrix::GetColumnMajor( Float* data ) const
	{
		//unrolled
		data[0]=X[0];
		data[1]=Y[0];
		data[2]=Z[0];
		data[3]=W[0];
		data[4]=X[1];
		data[5]=Y[1];
		data[6]=Z[1];
		data[7]=W[1];
		data[8]=X[2];
		data[9]=Y[2];
		data[10]=Z[2];
		data[11]=W[2];
		data[12]=X[3];
		data[13]=Y[3];
		data[14]=Z[3];
		data[15]=W[3];	
	}

	RED_INLINE void Matrix::GetColumnMajor3x4( Float* data ) const
	{
		//unrolled
		data[0]=X[0];
		data[1]=Y[0];
		data[2]=Z[0];
		data[3]=W[0];
		data[4]=X[1];
		data[5]=Y[1];
		data[6]=Z[1];
		data[7]=W[1];
		data[8]=X[2];
		data[9]=Y[2];
		data[10]=Z[2];
		data[11]=W[2];
	}

	RED_INLINE void Matrix::SetColumnMajor3x4( const Float* data )
	{
		//unrolled
		X[0]=data[0];		X[1]=data[4];		X[2]=data[8];
		Y[0]=data[1];		Y[1]=data[5];		Y[2]=data[9];
		Z[0]=data[2];		Z[1]=data[6];		Z[2]=data[10];
		W[0]=data[3];		W[1]=data[7];		W[2]=data[11];
	}

	RED_INLINE void Matrix::SetRowMajor4x3( const Float* data )
	{
		//unrolled
		X[0]=data[0];		X[1]=data[1];		X[2]=data[2];		X[3]=data[3];
		Y[0]=data[4];		Y[1]=data[5];		Y[2]=data[6];		Y[3]=data[7];
		Z[0]=data[8];		Z[1]=data[9];		Z[2]=data[10];		Z[3]=data[11];
		W[0]=0;				W[1]=0;				W[2]=0;				W[3]=1;
	}


	RED_FORCE_INLINE Vector4 Matrix::TransformVector( const Vector4& a ) const
	{
		Vector4 result = X * a.X + Y * a.Y + Z * a.Z;
		result.W = 0.0f;
		return result;
	}

	RED_FORCE_INLINE Vector4 Matrix::TransformVectorWithW( const Vector4& a ) const
	{
		return X * a.X + Y * a.Y + Z * a.Z + W * a.W;
	}

	RED_FORCE_INLINE Vector4 Matrix::TransformVectorAsPoint( const Vector4& a ) const
	{
		Vector4 result = X * a.X + Y * a.Y + Z * a.Z + W;
		result.W = 1.0f;
		return result;
	}

	RED_FORCE_INLINE Vector4 Matrix::TransformPoint( const Vector4& a ) const
	{
		Vector4 result = X * a.X + Y * a.Y + Z * a.Z + W;
		result.W = 1.0f;
		return result;
	}

	// Transform point by matrix (W assumed to be equal to 1)
	RED_FORCE_INLINE Vector3 Matrix::TransformPoint( const Vector3& a ) const
	{
		return TransformPoint( Vector4( a ) );
	}

	RED_FORCE_INLINE Matrix Matrix::operator*( const Matrix& other ) const
	{
		return Mul( *this, other );
	}

	RED_INLINE Bool Matrix::IsOk() const
	{
		return X.IsOk() && Y.IsOk() && Z.IsOk() && W.IsOk();
	}


	RED_INLINE Bool Matrix::Equal( const Matrix& a, const Matrix& b )
	{
		return Vector4::Equal4( a.W, b.W ) // Translation first
			&& Vector4::Equal4( a.X, b.X )
			&& Vector4::Equal4( a.Y, b.Y )
			&& Vector4::Equal4( a.Z, b.Z );
	}

	RED_INLINE Bool Matrix::Near( const Matrix& a, const Matrix& b, Float eps )
	{
		return Vector4::Near4( a.W, b.W, eps ) // Translation first
			&& Vector4::Near4( a.X, b.X, eps )
			&& Vector4::Near4( a.Y, b.Y, eps )
			&& Vector4::Near4( a.Z, b.Z, eps );
	}

	RED_INLINE Matrix Matrix::ZEROS()
	{
		return { Vector4::ZEROS(), Vector4::ZEROS(), Vector4::ZEROS(), Vector4::ZEROS() };
	}

	RED_INLINE Matrix Matrix::IDENTITY()
	{
		return { Vector4::EX(), Vector4::EY(), Vector4::EZ(), Vector4::EW() };
	}

} // math