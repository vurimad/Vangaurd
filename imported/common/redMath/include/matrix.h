/**
* Copyright (c) 2026 RED Vanguard, All Rights Reserved.
*/
#pragma once

namespace simd
{
	class QsTransform;
}

namespace math
{
	// 4x4 Matrix, row major, multiplication of matrices happens left to right i.e [V] * [S][R][T]
	RED_ALIGNED_STRUCT_API( Matrix, REDMATH_API, 16 )
	{
		Matrix() = default;
		Matrix( const Vector4& x, const Vector4& y, const Vector4& z, const Vector4& w );
		Matrix( const Float f[16] );
		Matrix( const Matrix& m );

		Matrix( const Float _00, const Float _01, const Float _02, const Float _03,
						  const Float _10, const Float _11, const Float _12, const Float _13,
						  const Float _20, const Float _21, const Float _22, const Float _23,
						  const Float _30, const Float _31, const Float _32, const Float _33 );

		RED_INLINE const Float* AsFloat() const { return X.AsFloat(); }
		RED_INLINE Float* AsFloat() { return X.AsFloat(); }

		Bool operator==( const Matrix& a ) const;
		Bool operator!=( const Matrix& a ) const;

		RED_INLINE Matrix& operator=( const Matrix& rhs );

		// Setting
		RED_INLINE Matrix& Set( const Matrix& a );
		RED_INLINE Matrix& SetRows( const Float* f );
		RED_INLINE Matrix& SetRows( const Vector4& x, const Vector4& y, const Vector4& z, const Vector4& w );
		RED_INLINE Matrix& SetCols( const Float* f );
		RED_INLINE Matrix& SetCols( const Vector4& x, const Vector4& y, const Vector4& z, const Vector4& w );
		RED_INLINE Matrix& SetZeros();
		RED_INLINE Matrix& SetIdentity();

		// Set the 3x3 part of the matrix
		RED_INLINE Matrix& Set33( const Matrix& a );
		RED_INLINE Matrix& Set33( const Vector4& x, const Vector4& y, const Vector4& z );
		RED_INLINE Matrix& SetRotX33( Float ccwRadians );
		RED_INLINE Matrix& SetRotY33( Float ccwRadians );
		RED_INLINE Matrix& SetRotZ33( Float ccwRadians );
		
		RED_INLINE Matrix& SetScale(const Vector4& scale);
		RED_INLINE Matrix& SetScale33( const Vector4& scale );
		RED_INLINE Matrix& SetScale44( const Vector4& scale );
		RED_INLINE Matrix& SetPreScale33( const Vector4& scale );
		RED_INLINE Matrix& SetPreScale44( const Vector4& scale );
		RED_INLINE Matrix& SetScale33( Float uniformScale );
		RED_INLINE Vector4 GetScale33() const;
		RED_INLINE Vector4 GetPreScale33() const;

		// Set translation part, only XYZ is used
		RED_INLINE Matrix& SetTranslation( const Vector4& a );
		RED_INLINE Matrix& SetTranslation( Float x, Float y, Float z );
		Vector4 GetTranslation() const;
		RED_INLINE const Vector4& GetTranslationRef() const;
		RED_INLINE Vector4& GetTranslationRef();

		// Inplace Invert/Transpose
		RED_INLINE Matrix& OrthonormInvert();
		RED_INLINE Matrix& FullInvert();
		RED_INLINE Matrix& Transpose();

		// Column/Row access
		RED_INLINE Vector4 GetColumn( const size_t index ) const;
		RED_INLINE Matrix& SetColumn( const size_t index, const Vector4& a );
		RED_INLINE Vector4 GetRow( const size_t index ) const;
		RED_INLINE Matrix& SetRow( const size_t index, const Vector4& a );

		// Return ROW
		RED_FORCE_INLINE const Vector4& operator[]( const size_t index ) const
		{
			RED_FATAL_ASSERT( index < 4, "Error: Index out of bounds" );
			return *(reinterpret_cast<const Vector4*>(&X) + index);
		}

		// Return ROW
		RED_FORCE_INLINE Vector4& operator[]( const size_t index )
		{
			RED_FATAL_ASSERT( index < 4, "Error: Index out of bounds" );
			return *(reinterpret_cast<Vector4*>(&X) + index);
		}

		// Extract matrix axes
		Vector4 GetAxisX() const;
		Vector4 GetAxisY() const;
		Vector4 GetAxisZ() const;

		// Extract matrix as column major data
		RED_INLINE void GetColumnMajor( Float* data ) const;
		RED_INLINE void GetColumnMajor3x4( Float* data ) const;
		RED_INLINE void SetColumnMajor3x4( const Float* data );

		RED_INLINE void SetRowMajor4x3( const Float* data );

		// Multiply matrix
		static Matrix Mul( const Matrix& a, const Matrix& b );
		RED_INLINE Matrix operator*( const Matrix& other ) const;

		// Checks for bad values (denormals or infinities).
		RED_INLINE Bool IsOk() const;

		// Are two matrices equal
		RED_INLINE static Bool Equal( const Matrix& a, const Matrix& b );

		// Are two matrices similar
		RED_INLINE static Bool Near( const Matrix& a, const Matrix& b, Float eps=1e-3f );

		// Convert rotation to Euler angles
		EulerAngles ToEulerAngles() const;

		// Convert rotation to Euler angles, works properly also for scaled matrices
		EulerAngles ToEulerAnglesFull() const;

		// Convert rotation to Euler angles, works properly also for scaled matrices, returns validity flag
		EulerAngles ToEulerAnglesFullChecked(Bool& isValid) const;

		// Get yaw rotation (around Z axis) represented by this matrix
		Float GetYaw() const;

		// Extract basis vectors from this matrix
		void ToAngleVectors( Vector4* forward, Vector4* right, Vector4* up ) const;

		// Convert to a raw quaternion
		Quaternion ToQuat() const;

		// Convert to a transform: Note this will isn't safe if matrix contains a scale
		Transform ToXform() const;

		// Decompose this matrix into original components. 
		// NOTE: These functions are expensive! use wisely
		static Bool SLOW_Decompose(const Matrix& m, Vector4& translation, Matrix& rotation, Vector4& scale);
		static Bool SLOW_Decompose(const Matrix& m, Vector4& translation, Quaternion& rotation, Vector4& scale);
		static Bool SLOW_Decompose(const Matrix& m, Vector3& translation, Matrix& rotation, Vector3& scale);
		static Bool SLOW_Decompose(const Matrix& m, Vector3& translation, Quaternion& rotation, Vector3& scale);
		static Bool SLOW_Decompose(const Matrix& m, Vector4& translation, Quaternion& rotation);
		static Bool SLOW_Decompose(const Matrix& m, Vector3& translation, Quaternion& rotation);

		static Bool SLOW_Decompose(const Matrix& m, simd::QsTransform& xform);
		static Bool SLOW_Decompose(const Matrix& m, Transform& xform);

		// Transform 3D vector by this matrix
		Vector4 TransformVector( const Vector4& a ) const;			// Assumed W = 0.0f

		// Transform 4D vector (W used) by this matrix
		Vector4 TransformVectorWithW( const Vector4& a ) const;		// W used directly

		// Transform 4D vector as point (W assumed to be equal to 1)
		Vector4 TransformVectorAsPoint( const Vector4& a ) const;	// Assumed W = 1.0f

		// Transform point by matrix (W assumed to be equal to 1)
		Vector4 TransformPoint( const Vector4& a ) const;

		// Transform point by matrix (W assumed to be equal to 1)
		Vector3 TransformPoint( const Vector3& a ) const;

		// Transform box by this matrix
		Box TransformBox( const Box& box ) const;

		// Compute and return inversion of this matrix (use only on orthonormal matrices)
		Matrix OrthonormInverted() const;

		// Compute and return full inversion of this matrix
		Matrix FullInverted() const;

		// Return transposed matrix
		Matrix Transposed() const;

		// Calculate matrix determinant
		Float Det() const;

		// Calculate inner 3x3 matrix determinant, specify which column and row to skip
		Float CoFactor( Int32 i, Int32 j ) const;

		// Decompose matrix to the orthonormal part and the scale
		void ExtractScale( Matrix &trMatrix, Vector4& scale ) const;

		// Build matrix for perspective projection with given parameters, fov is horizontal and in radians
		Matrix& BuildPerspectiveLH( Float fovy, Float aspect, Float zn, Float zf );
		Matrix& BuildPerspectiveOffCenterLH( Float zn, Float zf, Float zoomX, Float zoomY, Float l, Float r, Float b, Float t );
		Matrix& BuildPerspectiveLH_Z(Float fovy, Float aspect, Float zn, Float zf);
		Matrix& BuildPerspectiveLH_XY(Vector2 nearPlaneSize, Float zn, Float zf);

		// Build matrix for orthographic projection with given parameters
		Matrix& BuildOrthoLH( Float w, Float h, Float zn, Float zf );	

		// Modify matrix to oblique projection
		Matrix& ModifyProjectionToOblique( Vector4& clippingPlane );

		// Build matrix with EY from given direction vector
		Matrix& BuildFromDirectionVector( const Vector4& dirVec, const Vector4& upVec = Vector4::EZ() );

		// Build matrix from quaternion
		Matrix& BuildFromQuaternion( const Quaternion& quaternion );

		// Some predefined matrices
		static Matrix ZEROS();
		static Matrix IDENTITY();

		static const Matrix IDENTITY_CONSTANT; // ctremblay: Mostly use for rendering where a pointer is required or matrix is only copied on submit

        Vector4 X = Vector4::EX();
        Vector4 Y = Vector4::EY();
        Vector4 Z = Vector4::EZ();
        Vector4 W = Vector4::EW();
	};

} // math