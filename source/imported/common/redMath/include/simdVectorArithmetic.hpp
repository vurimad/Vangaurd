namespace simd
{
	//////////////////////////////////////////////////////////////////////////
	// '+' Addition Functions.
	//////////////////////////////////////////////////////////////////////////

	//////////////////////////////////////////////////////////////////////////
	void Add( Scalar& _a, const Scalar& _b, const Scalar& _c )
	{
		_a.V = _mm_add_ps( _b.V, _c.V );
	}

	//////////////////////////////////////////////////////////////////////////
	void Add( Vector4& _a, const Vector4& _b, const Scalar& _c )
	{
		_a.V = _mm_add_ps( _b.V, _c.V );
	}

	//////////////////////////////////////////////////////////////////////////
	void Add( Vector4& _a, const Vector4& _b, const Vector4& _c )
	{
		_a.V = _mm_add_ps( _b.V, _c.V );
	}

	//////////////////////////////////////////////////////////////////////////
	void Add3( Vector4& _a, const Vector4& _b, const Vector4& _c )
	{
		_a.V = _mm_add_ps( _b.V, _mm_and_ps( _c.V, XYZ_MASK ) );
	}

	//////////////////////////////////////////////////////////////////////////
	Scalar Add( const Scalar& _a, const Scalar& _b )
	{
		return _mm_add_ps( _a.V, _b.V );
	}

	//////////////////////////////////////////////////////////////////////////
	Vector4 Add( const Vector4& _a, const Scalar& _b )
	{
		return Vector4( _mm_add_ps( _a.V, _b.V ) );
	}

	//////////////////////////////////////////////////////////////////////////
	Vector4 Add( const Vector4& _a, const Vector4& _b )
	{
		return Vector4( _mm_add_ps( _a.V, _b.V ) );
	}

	//////////////////////////////////////////////////////////////////////////
	Vector4 Add3( const Vector4& _a, const Vector4& _b )
	{
		return Vector4( _mm_add_ps( _a.V, _mm_and_ps( _b.V, XYZ_MASK ) ) );
	}

	//////////////////////////////////////////////////////////////////////////
	void SetAdd( Scalar& _a, const Scalar&_b )
	{
		_a.V = _mm_add_ps( _a.V, _b.V );
	}

	//////////////////////////////////////////////////////////////////////////
	void SetAdd( Vector4& _a, const Scalar&_b )
	{
		_a.V = _mm_add_ps( _a.V, _b.V );
	}

	//////////////////////////////////////////////////////////////////////////
	void SetAdd( Vector4& _a, const Vector4& _b )
	{
		_a.V = _mm_add_ps( _a.V, _b.V );
	}

	//////////////////////////////////////////////////////////////////////////
	void SetAdd3( Vector4& _a, const Vector4& _b )
	{
		_a.V = _mm_add_ps( _mm_and_ps( _a.V, XYZ_MASK ), _mm_and_ps( _b.V, XYZ_MASK ) );
	}

	//////////////////////////////////////////////////////////////////////////
	// '-' Subtraction Functions
	//////////////////////////////////////////////////////////////////////////

	//////////////////////////////////////////////////////////////////////////
	void Sub( Scalar& _a, const Scalar& _b, const Scalar& _c )
	{
		_a.V = _mm_sub_ps( _b.V, _c.V );
	}

	//////////////////////////////////////////////////////////////////////////
	void Sub( Vector4& _a, const Vector4& _b, const Scalar& _c )
	{
		_a.V = _mm_sub_ps( _b.V, _c.V );
	}

	//////////////////////////////////////////////////////////////////////////
	void Sub( Vector4& _a, const Vector4& _b, const Vector4& _c )
	{
		_a.V = _mm_sub_ps( _b.V, _c.V );
	}

	//////////////////////////////////////////////////////////////////////////
	void Sub3( Vector4& _a, const Vector4& _b, const Vector4& _c )
	{
		_a.V = _mm_sub_ps( _b.V, _mm_and_ps( _c.V, XYZ_MASK ) );
	}

	//////////////////////////////////////////////////////////////////////////
	Scalar Sub( const Scalar& _a, const Scalar& _b )
	{
		return _mm_sub_ps( _a.V, _b.V );
	}

	//////////////////////////////////////////////////////////////////////////
	Vector4 Sub( const Vector4& _a, const Scalar& _b )
	{
		return Vector4( _mm_sub_ps( _a.V, _b.V ) );
	}

	//////////////////////////////////////////////////////////////////////////
	Vector4 Sub( const Vector4& _a, const Vector4& _b )
	{
		return Vector4( _mm_sub_ps( _a.V, _b.V ) );
	}

	//////////////////////////////////////////////////////////////////////////
	Vector4 Sub3( const Vector4& _a, const Vector4& _b )
	{
		return Vector4( _mm_sub_ps( _a.V, _mm_and_ps( _b.V, XYZ_MASK ) ) );
	}

	//////////////////////////////////////////////////////////////////////////
	void SetSub( Scalar& _a, const Scalar& _b )
	{
		_a.V = _mm_sub_ps( _a.V, _b.V );
	}

	void SetSub( Vector4& _a, const Scalar& _b )
	{
		_a.V = _mm_sub_ps( _a.V, _b.V );
	}

	//////////////////////////////////////////////////////////////////////////
	void SetSub( Vector4& _a, const Vector4& _b )
	{
		_a.V = _mm_sub_ps( _a.V, _b.V );
	}

	//////////////////////////////////////////////////////////////////////////
	void SetSub3( Vector4& _a, const Vector4& _b )
	{
		_a.V = _mm_sub_ps( _a.V, _mm_and_ps( _b.V, XYZ_MASK ) );
	}

	//////////////////////////////////////////////////////////////////////////
	// '*' Multiplication Functions.
	//////////////////////////////////////////////////////////////////////////

	//////////////////////////////////////////////////////////////////////////
	void Mul( Scalar& _a, const Scalar& _b, const Scalar& _c )
	{
		_a.V = _mm_mul_ps( _b.V, _c.V );
	}

	//////////////////////////////////////////////////////////////////////////
	extern void Mul( Vector4& _a, const Vector4& _b, const Scalar& _c )
	{
		_a.V = _mm_mul_ps( _b.V, _c.V );
	}

	//////////////////////////////////////////////////////////////////////////
	void Mul( Vector4& _a, const Vector4& _b, const Vector4& _c )
	{
		_a.V = _mm_mul_ps( _b.V, _c.V );
	}

	void Mul3( Vector4& _a, const Vector4& _b, const Vector4& _c )
	{
		_a.V = _mm_mul_ps( _b.V, _mm_add_ps( _mm_and_ps( _c.V, XYZ_MASK ), _mm_setr_ps( 0.0f, 0.0f, 0.0f, 1.0f ) ) );
	}

	//////////////////////////////////////////////////////////////////////////
	Scalar Mul( const Scalar& _a, const Scalar& _b )
	{
		return _mm_mul_ps( _a.V, _b.V );
	}

	//////////////////////////////////////////////////////////////////////////
	Vector4 Mul( const Vector4& _a, const Scalar& _b )
	{
		return Vector4( _mm_mul_ps( _a.V, _b.V ) );
	}

	//////////////////////////////////////////////////////////////////////////
	Vector4 Mul( const Vector4& _a, const Vector4& _b )
	{
		return Vector4( _mm_mul_ps( _a.V, _b.V ) );
	}

	//////////////////////////////////////////////////////////////////////////
	Vector4 Mul3( const Vector4& _a, const Vector4& _b )
	{
		return Vector4( _mm_mul_ps( _a.V, _mm_add_ps( _mm_and_ps( _b.V, XYZ_MASK ), _mm_setr_ps( 0.0f, 0.0f, 0.0f, 1.0f  ) ) ) );
	}

	//////////////////////////////////////////////////////////////////////////
	void SetMul( Scalar& _a, const Scalar& _b )
	{
		_a.V = _mm_mul_ps( _a.V, _b.V );
	}

	//////////////////////////////////////////////////////////////////////////
	void SetMul( Vector4& _a, const Scalar& _b )
	{
		_a.V = _mm_mul_ps( _a.V, _b.V );
	}

	//////////////////////////////////////////////////////////////////////////
	void SetMul( Vector4& _a, const Vector4& _b )
	{
		_a.V = _mm_mul_ps( _a.V, _b.V );
	}

	//////////////////////////////////////////////////////////////////////////
	void SetMul3( Vector4& _a, const Vector4& _b )
	{
		_a.V = _mm_mul_ps( _a.V, _mm_add_ps( _mm_and_ps( _b.V, XYZ_MASK ), _mm_setr_ps( 0.0f, 0.0f, 0.0f, 1.0f ) ) );
	}

	//////////////////////////////////////////////////////////////////////////
	// '/' Divisor Function
	//////////////////////////////////////////////////////////////////////////

	//////////////////////////////////////////////////////////////////////////
	void Div( Scalar& _a, const Scalar& _b, const Scalar& _c )
	{
		_a.V = _mm_div_ps( _b.V, _c.V );
	}

	//////////////////////////////////////////////////////////////////////////
	void Div( Vector4& _a, const Vector4& _b, const Scalar& _c )
	{
		_a.V = _mm_div_ps( _b.V, _c.V );
	}

	//////////////////////////////////////////////////////////////////////////
	void Div( Vector4& _a, const Vector4& _b, const Vector4& _c )
	{
		_a.V = _mm_div_ps( _b.V, _c.V );
	}

	//////////////////////////////////////////////////////////////////////////
	void Div3( Vector4& _a, const Vector4& _b, const Vector4& _c )
	{
		_a.V = _mm_div_ps( _b.V, _mm_add_ps( _mm_and_ps( _c.V, XYZ_MASK ), _mm_setr_ps( 0.0f, 0.0f, 0.0f, 1.0f ) ) );
	}

	//////////////////////////////////////////////////////////////////////////
	Scalar Div( const Scalar& _a, const Scalar& _b )
	{
		return _mm_div_ps( _a.V, _b.V );
	}

	//////////////////////////////////////////////////////////////////////////
	Vector4 Div( const Vector4& _a, const Scalar& _b )
	{
		return Vector4( _mm_div_ps( _a.V, _b.V ) );
	}

	//////////////////////////////////////////////////////////////////////////
	Vector4 Div( const Vector4& _a, const Vector4& _b )
	{
		return Vector4( _mm_div_ps( _a.V, _b.V ) );
	}

	//////////////////////////////////////////////////////////////////////////
	Vector4 Div3( const Vector4& _a, const Vector4& _b )
	{
		return Vector4( _mm_div_ps( _a.V, _mm_add_ps( _mm_and_ps( _b.V, XYZ_MASK ), _mm_setr_ps( 0.0f, 0.0f, 0.0f, 1.0f ) ) ) );
	}

	//////////////////////////////////////////////////////////////////////////
	void SetDiv( Scalar& _a, const Scalar& _b )
	{
		_a.V = _mm_div_ps( _a.V, _b.V );
	}

	//////////////////////////////////////////////////////////////////////////
	void SetDiv( Vector4& _a, const Scalar& _b )
	{
		_a.V = _mm_div_ps( _a.V, _b.V );
	}

	//////////////////////////////////////////////////////////////////////////
	void SetDiv( Vector4& _a, const Vector4& _b )
	{
		_a.V = _mm_div_ps( _a.V, _b.V );
	}

	//////////////////////////////////////////////////////////////////////////
	void SetDiv3( Vector4& _a, const Vector4& _b )
	{
		_a.V = _mm_div_ps( _a.V, _mm_add_ps( _mm_and_ps( _b.V, XYZ_MASK ), _mm_setr_ps( 0.0f, 0.0f, 0.0f, 1.0f) ) );
	}

	void Min( Vector4& _a, const Vector4& _b, const Vector4& _c )
	{
		_a.V = _mm_min_ps(_b.V, _c.V);
	}

	Vector4 Min( const Vector4& _a, const Vector4& _b )
	{
		return Vector4(_mm_min_ps(_a.V, _b.V));
	}

	RED_INLINE void Max( Vector4& _a, const Vector4& _b, const Vector4& _c )
	{
		_a.V = _mm_max_ps(_b.V, _c.V);
	}

	RED_INLINE Vector4 Max( const Vector4& _a, const Vector4& _b )
	{
		return Vector4(_mm_max_ps(_a.V, _b.V));
	}
}
