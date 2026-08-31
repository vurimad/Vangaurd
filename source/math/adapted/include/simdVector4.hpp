namespace vanguard::math::simd
{
    //////////////////////////////////////////////////////////////////////////
    RED_INLINE Vector4::Vector4() : V(_mm_setzero_ps()) {}

    //////////////////////////////////////////////////////////////////////////
    RED_INLINE constexpr Vector4::Vector4(const Scalar& _v) : V(_v.V) {}

    //////////////////////////////////////////////////////////////////////////
    RED_INLINE constexpr Vector4::Vector4(const Vector4& _v) : V(_v.V) {}

    //////////////////////////////////////////////////////////////////////////
    RED_INLINE Vector4::Vector4(const red::Float* _f) : V(_mm_load_ps(_f)) {}

    //////////////////////////////////////////////////////////////////////////
    RED_INLINE Vector4::Vector4(red::Float _x, red::Float _y, red::Float _z, red::Float _w) : V(_mm_setr_ps(_x, _y, _z, _w)) {}

    //////////////////////////////////////////////////////////////////////////
    RED_INLINE constexpr Vector4::Vector4(Quad _v) : V(_v) {}

    //////////////////////////////////////////////////////////////////////////
    RED_INLINE Vector4& Vector4::operator=(const Scalar& _v)
    {
        V = _v.V;
        return (*this);
    }

    RED_INLINE const red::Float* Vector4::AsFloat() const
    {
        return &X;
    }

    RED_INLINE void Vector4::Set(const Scalar& _v)
    {
        V = _v.V;
    }

    RED_INLINE void Vector4::Set(const Vector4& _v)
    {
        V = _v.V;
    }

    RED_INLINE void Vector4::Set(red::Float _x, red::Float _y, red::Float _z, red::Float _w)
    {
        V = _mm_setr_ps(_x, _y, _z, _w);
    }

    RED_INLINE void Vector4::Set(const red::Float* _f)
    {
        V = _mm_load_ps(_f);
    }

    RED_INLINE void Vector4::SetZeros()
    {
        V = _mm_setzero_ps();
    }

    //////////////////////////////////////////////////////////////////////////
    RED_INLINE void Vector4::SetOnes()
    {
        V = _mm_set1_ps(1.0f);
    }

    //////////////////////////////////////////////////////////////////////////
    RED_INLINE Vector4& Vector4::Negate()
    {
        V = _mm_xor_ps(V, SIGN_MASK);
        return (*this);
    }

    //////////////////////////////////////////////////////////////////////////
    RED_INLINE Vector4 Vector4::Negated() const
    {
        Quad negVal = _mm_xor_ps(V, SIGN_MASK);
        return Vector4(negVal);
    }

    //////////////////////////////////////////////////////////////////////////
    RED_INLINE Vector4 Vector4::Abs() const
    {
        Quad absVal = _mm_max_ps(_mm_sub_ps(_mm_setzero_ps(), V), V);
        return Vector4(absVal);
    }

    //////////////////////////////////////////////////////////////////////////
    RED_INLINE Scalar Vector4::Sum3() const
    {
        return _mm_add_ss(_mm_add_ss(_mm_shuffle_ps(V, V, _MM_SHUFFLE(3, 0, 0, 0)), _mm_shuffle_ps(V, V, _MM_SHUFFLE(3, 1, 1, 1))),
                          _mm_shuffle_ps(V, V, _MM_SHUFFLE(3, 2, 2, 2)));
    }

    //////////////////////////////////////////////////////////////////////////
    RED_INLINE Scalar Vector4::Sum4() const
    {
        Quad shuf = _mm_movehdup_ps(V);
        Quad sums = _mm_add_ps(V, shuf);

        shuf = _mm_movehl_ps(shuf, sums);
        sums = _mm_add_ss(sums, shuf);
        return _mm_cvtss_f32(sums);

        /*_mm_hadd_ps()

        return _mm_add_ss(
        _mm_add_ss( _mm_shuffle_ps( V, V, _MM_SHUFFLE( 0, 0, 0, 0 ) ),
        _mm_shuffle_ps( V, V, _MM_SHUFFLE( 1, 1, 1, 1 ) ) ),
        _mm_add_ss( _mm_shuffle_ps( V, V, _MM_SHUFFLE( 2, 2, 2, 2 ) ),
        _mm_shuffle_ps( V, V, _MM_SHUFFLE( 3, 3, 3, 3 ) ) )
        );*/
    }

    //////////////////////////////////////////////////////////////////////////
    RED_INLINE Scalar Vector4::Length3() const
    {
        Quad vA = _mm_mul_ps(V, V);
        Quad vB = _mm_add_ss(_mm_add_ss(_mm_shuffle_ps(vA, vA, _MM_SHUFFLE(3, 0, 0, 0)), _mm_shuffle_ps(vA, vA, _MM_SHUFFLE(3, 1, 1, 1))),
                             _mm_shuffle_ps(vA, vA, _MM_SHUFFLE(3, 2, 2, 2)));
        vB = _mm_sqrt_ss(vB);
        return _mm_shuffle_ps(vB, vB, _MM_SHUFFLE(0, 0, 0, 0));
    }

    //////////////////////////////////////////////////////////////////////////
    RED_INLINE Scalar Vector4::Length4() const
    {
        Quad vA = _mm_mul_ps(V, V);
        Quad vB = _mm_add_ss(_mm_add_ss(_mm_shuffle_ps(vA, vA, _MM_SHUFFLE(0, 0, 0, 0)), _mm_shuffle_ps(vA, vA, _MM_SHUFFLE(1, 1, 1, 1))),
                             _mm_add_ss(_mm_shuffle_ps(vA, vA, _MM_SHUFFLE(2, 2, 2, 2)), _mm_shuffle_ps(vA, vA, _MM_SHUFFLE(3, 3, 3, 3))));
        vB = _mm_sqrt_ss(vB);
        return _mm_shuffle_ps(vB, vB, _MM_SHUFFLE(0, 0, 0, 0));
    }

    //////////////////////////////////////////////////////////////////////////
    RED_INLINE Scalar Vector4::SquareLength3() const
    {
        Quad vA = _mm_mul_ps(V, V);
        Quad vB = _mm_add_ss(_mm_add_ss(_mm_shuffle_ps(vA, vA, _MM_SHUFFLE(0, 0, 0, 0)), _mm_shuffle_ps(vA, vA, _MM_SHUFFLE(1, 1, 1, 1))),
                             _mm_shuffle_ps(vA, vA, _MM_SHUFFLE(2, 2, 2, 2)));
        return _mm_shuffle_ps(vB, vB, _MM_SHUFFLE(0, 0, 0, 0));
    }

    //////////////////////////////////////////////////////////////////////////
    RED_INLINE Scalar Vector4::SquareLength4() const
    {
        Quad vA = _mm_mul_ps(V, V);
        Quad vB = _mm_add_ss(_mm_add_ss(_mm_shuffle_ps(vA, vA, _MM_SHUFFLE(0, 0, 0, 0)), _mm_shuffle_ps(vA, vA, _MM_SHUFFLE(1, 1, 1, 1))),
                             _mm_add_ss(_mm_shuffle_ps(vA, vA, _MM_SHUFFLE(2, 2, 2, 2)), _mm_shuffle_ps(vA, vA, _MM_SHUFFLE(3, 3, 3, 3))));
        return _mm_shuffle_ps(vB, vB, _MM_SHUFFLE(0, 0, 0, 0));
    }

    //////////////////////////////////////////////////////////////////////////
    RED_INLINE Vector4& Vector4::Normalize4()
    {
        Quad vA = _mm_mul_ps(V, V);
        vA = _mm_add_ss(_mm_add_ss(_mm_shuffle_ps(vA, vA, _MM_SHUFFLE(0, 0, 0, 0)), _mm_shuffle_ps(vA, vA, _MM_SHUFFLE(1, 1, 1, 1))),
                        _mm_add_ss(_mm_shuffle_ps(vA, vA, _MM_SHUFFLE(2, 2, 2, 2)), _mm_shuffle_ps(vA, vA, _MM_SHUFFLE(3, 3, 3, 3))));
        Quad length = _mm_sqrt_ss(vA);
        length = _mm_shuffle_ps(length, length, _MM_SHUFFLE(0, 0, 0, 0));
        Quad hasLength = _mm_cmpeq_ss(length, _mm_setzero_ps());
        hasLength = _mm_shuffle_ps(hasLength, hasLength, _MM_SHUFFLE(0, 0, 0, 0));
        Quad unitLength = _mm_div_ps(_mm_set1_ps(1.0f), length);
        V = _mm_andnot_ps(hasLength, _mm_mul_ps(V, unitLength));
        return (*this);
    }

    //////////////////////////////////////////////////////////////////////////
    RED_INLINE Vector4 Vector4::Normalized4() const
    {
        Quad vA = _mm_mul_ps(V, V);
        vA = _mm_add_ss(_mm_add_ss(_mm_shuffle_ps(vA, vA, _MM_SHUFFLE(0, 0, 0, 0)), _mm_shuffle_ps(vA, vA, _MM_SHUFFLE(1, 1, 1, 1))),
                        _mm_add_ss(_mm_shuffle_ps(vA, vA, _MM_SHUFFLE(2, 2, 2, 2)), _mm_shuffle_ps(vA, vA, _MM_SHUFFLE(3, 3, 3, 3))));
        Quad length = _mm_sqrt_ss(vA);
        length = _mm_shuffle_ps(length, length, _MM_SHUFFLE(0, 0, 0, 0));
        Quad hasLength = _mm_cmpeq_ss(length, _mm_setzero_ps());
        hasLength = _mm_shuffle_ps(hasLength, hasLength, _MM_SHUFFLE(0, 0, 0, 0));
        Quad unitLength = _mm_div_ps(_mm_set1_ps(1.0f), length);
        return Vector4(_mm_andnot_ps(hasLength, _mm_mul_ps(V, unitLength)));
    }

    //////////////////////////////////////////////////////////////////////////
    RED_INLINE Vector4& Vector4::Normalize3()
    {
        Quad vA = _mm_mul_ps(V, V);
        vA = _mm_add_ss(_mm_add_ss(_mm_shuffle_ps(vA, vA, _MM_SHUFFLE(0, 0, 0, 0)), _mm_shuffle_ps(vA, vA, _MM_SHUFFLE(1, 1, 1, 1))),
                        _mm_shuffle_ps(vA, vA, _MM_SHUFFLE(2, 2, 2, 2)));
        Quad length = _mm_sqrt_ss(vA);
        length = _mm_shuffle_ps(length, length, _MM_SHUFFLE(0, 0, 0, 0));
        Quad hasLength = _mm_cmpeq_ss(length, _mm_setzero_ps());
        hasLength = _mm_shuffle_ps(hasLength, hasLength, _MM_SHUFFLE(0, 0, 0, 0));
        Quad unitLength = _mm_div_ps(_mm_set1_ps(1.0f), length);
        V = _mm_andnot_ps(hasLength, _mm_mul_ps(_mm_and_ps(V, XYZ_MASK), unitLength));
        return (*this);
    }

    //////////////////////////////////////////////////////////////////////////
    RED_INLINE Vector4 Vector4::Normalized3() const
    {
        Quad vA = _mm_mul_ps(V, V);
        vA = _mm_add_ss(_mm_add_ss(_mm_shuffle_ps(vA, vA, _MM_SHUFFLE(0, 0, 0, 0)), _mm_shuffle_ps(vA, vA, _MM_SHUFFLE(1, 1, 1, 1))),
                        _mm_shuffle_ps(vA, vA, _MM_SHUFFLE(2, 2, 2, 2)));
        Quad length = _mm_sqrt_ss(vA);
        length = _mm_shuffle_ps(length, length, _MM_SHUFFLE(0, 0, 0, 0));
        Quad hasLength = _mm_cmpeq_ss(length, _mm_setzero_ps());
        hasLength = _mm_shuffle_ps(hasLength, hasLength, _MM_SHUFFLE(0, 0, 0, 0));
        Quad unitLength = _mm_div_ps(_mm_set1_ps(1.0f), length);
        return Vector4(_mm_andnot_ps(hasLength, _mm_mul_ps(_mm_and_ps(V, XYZ_MASK), unitLength)));
    }

    //////////////////////////////////////////////////////////////////////////
    RED_INLINE Vector4& Vector4::NormalizeFast3()
    {
        Quad vA = _mm_mul_ps(V, V);
        vA = _mm_add_ss(_mm_add_ss(_mm_shuffle_ps(vA, vA, _MM_SHUFFLE(0, 0, 0, 0)), _mm_shuffle_ps(vA, vA, _MM_SHUFFLE(1, 1, 1, 1))),
                        _mm_shuffle_ps(vA, vA, _MM_SHUFFLE(2, 2, 2, 2)));
        Quad hasLength = _mm_cmpeq_ss(vA, _mm_setzero_ps());
        vA = _mm_rsqrt_ss(vA);
        hasLength = _mm_shuffle_ps(hasLength, hasLength, _MM_SHUFFLE(0, 0, 0, 0));
        V = _mm_andnot_ps(hasLength, _mm_mul_ps(_mm_and_ps(V, XYZ_MASK), _mm_shuffle_ps(vA, vA, _MM_SHUFFLE(0, 0, 0, 0))));
        return (*this);
    }

    //////////////////////////////////////////////////////////////////////////
    RED_INLINE Vector4 Vector4::NormalizedFast3() const
    {
        Quad vA = _mm_mul_ps(V, V);
        vA = _mm_add_ss(_mm_add_ss(_mm_shuffle_ps(vA, vA, _MM_SHUFFLE(0, 0, 0, 0)), _mm_shuffle_ps(vA, vA, _MM_SHUFFLE(1, 1, 1, 1))),
                        _mm_shuffle_ps(vA, vA, _MM_SHUFFLE(2, 2, 2, 2)));
        Quad hasLength = _mm_cmpeq_ss(vA, _mm_setzero_ps());
        vA = _mm_rsqrt_ss(vA);
        hasLength = _mm_shuffle_ps(hasLength, hasLength, _MM_SHUFFLE(0, 0, 0, 0));
        return Vector4(_mm_andnot_ps(hasLength, _mm_mul_ps(_mm_and_ps(V, XYZ_MASK), _mm_shuffle_ps(vA, vA, _MM_SHUFFLE(0, 0, 0, 0)))));
    }

    //////////////////////////////////////////////////////////////////////////
    RED_INLINE Vector4& Vector4::NormalizeFast4()
    {
        Quad vA = _mm_mul_ps(V, V);
        vA = _mm_add_ss(_mm_add_ss(_mm_shuffle_ps(vA, vA, _MM_SHUFFLE(0, 0, 0, 0)), _mm_shuffle_ps(vA, vA, _MM_SHUFFLE(1, 1, 1, 1))),
                        _mm_add_ss(_mm_shuffle_ps(vA, vA, _MM_SHUFFLE(2, 2, 2, 2)), _mm_shuffle_ps(vA, vA, _MM_SHUFFLE(3, 3, 3, 3))));
        Quad hasLength = _mm_cmpeq_ss(vA, _mm_setzero_ps());
        vA = _mm_rsqrt_ss(vA);
        hasLength = _mm_shuffle_ps(hasLength, hasLength, _MM_SHUFFLE(0, 0, 0, 0));
        V = _mm_andnot_ps(hasLength, _mm_mul_ps(V, _mm_shuffle_ps(vA, vA, _MM_SHUFFLE(0, 0, 0, 0))));
        return (*this);
    }

    //////////////////////////////////////////////////////////////////////////
    RED_INLINE Vector4 Vector4::NormalizedFast4() const
    {
        Quad vA = _mm_mul_ps(V, V);
        vA = _mm_add_ss(_mm_add_ss(_mm_shuffle_ps(vA, vA, _MM_SHUFFLE(0, 0, 0, 0)), _mm_shuffle_ps(vA, vA, _MM_SHUFFLE(1, 1, 1, 1))),
                        _mm_add_ss(_mm_shuffle_ps(vA, vA, _MM_SHUFFLE(2, 2, 2, 2)), _mm_shuffle_ps(vA, vA, _MM_SHUFFLE(3, 3, 3, 3))));
        Quad hasLength = _mm_cmpeq_ss(vA, _mm_setzero_ps());
        vA = _mm_rsqrt_ss(vA);
        hasLength = _mm_shuffle_ps(hasLength, hasLength, _MM_SHUFFLE(0, 0, 0, 0));
        return Vector4(_mm_andnot_ps(hasLength, _mm_mul_ps(V, _mm_shuffle_ps(vA, vA, _MM_SHUFFLE(0, 0, 0, 0)))));
    }

    //////////////////////////////////////////////////////////////////////////
    RED_INLINE red::Bool Vector4::IsNormalized4(const Quad _epsilon) const
    {
        Quad vA = _mm_mul_ps(V, V);
        vA = _mm_add_ss(_mm_add_ss(_mm_shuffle_ps(vA, vA, _MM_SHUFFLE(0, 0, 0, 0)), _mm_shuffle_ps(vA, vA, _MM_SHUFFLE(1, 1, 1, 1))),
                        _mm_add_ss(_mm_shuffle_ps(vA, vA, _MM_SHUFFLE(2, 2, 2, 2)), _mm_shuffle_ps(vA, vA, _MM_SHUFFLE(3, 3, 3, 3))));
        vA = _mm_sub_ss(vA, EX().V);
        vA = _mm_max_ss(_mm_sub_ss(_mm_setzero_ps(), vA), vA);
        vA = _mm_cmple_ss(vA, _epsilon);
        return (*((red::Uint32*)&vA) != 0);
    }

    //////////////////////////////////////////////////////////////////////////
    RED_INLINE red::Bool Vector4::IsNormalized3(const Quad _epsilon) const
    {
        Quad vA = _mm_mul_ps(V, V);
        vA = _mm_add_ss(_mm_add_ss(_mm_shuffle_ps(vA, vA, _MM_SHUFFLE(0, 0, 0, 0)), _mm_shuffle_ps(vA, vA, _MM_SHUFFLE(1, 1, 1, 1))),
                        _mm_shuffle_ps(vA, vA, _MM_SHUFFLE(2, 2, 2, 2)));
        vA = _mm_sub_ss(vA, EX().V);
        vA = _mm_max_ss(_mm_sub_ss(_mm_setzero_ps(), vA), vA);
        vA = _mm_cmple_ss(vA, _epsilon);
        return (*((red::Uint32*)&vA) != 0);
    }

    //////////////////////////////////////////////////////////////////////////
    RED_INLINE red::Bool Vector4::IsNormalized3(float _epsilon) const
    {
        Quad e = _mm_set_ps1(_epsilon);
        return IsNormalized3(e);
    }

    //////////////////////////////////////////////////////////////////////////
    RED_INLINE red::Bool Vector4::IsNormalized4(float _epsilon) const
    {
        Quad e = _mm_set_ps1(_epsilon);
        return IsNormalized4(e);
    }

    //////////////////////////////////////////////////////////////////////////
    RED_INLINE red::Bool Vector4::IsAlmostEqual(const Vector4& _v, float _epsilon) const
    {
        Quad e = _mm_set_ps1(_epsilon);
        return IsAlmostEqual(_v, e);
    }

    //////////////////////////////////////////////////////////////////////////
    RED_INLINE red::Bool Vector4::IsAlmostEqual(const Vector4& _v, const Quad _epsilon) const
    {
        Quad vA = _mm_sub_ps(V, _v.V);
        Quad vB = _mm_max_ps(_mm_sub_ps(_mm_setzero_ps(), vA), vA);
        Vector4 test(_mm_cmple_ps(vB, _epsilon));
        return (test.Xi != 0 && test.Yi != 0 && test.Zi != 0 && test.Wi != 0);
    }

    //////////////////////////////////////////////////////////////////////////
    RED_INLINE red::Bool Vector4::IsAlmostZero(const Quad _epsilon) const
    {
        Quad absVal = _mm_max_ps(_mm_sub_ps(_mm_setzero_ps(), V), V);
        Vector4 test(_mm_cmple_ps(absVal, _epsilon));
        return (test.Xi != 0 && test.Yi != 0 && test.Zi != 0 && test.Wi != 0);
    }

    //////////////////////////////////////////////////////////////////////////
    RED_INLINE red::Bool Vector4::IsZero() const
    {
        Vector4 test(_mm_cmpeq_ps(_mm_setzero_ps(), V));
        return (test.Xi != 0 && test.Yi != 0 && test.Zi != 0 && test.Wi != 0);
    }

    //////////////////////////////////////////////////////////////////////////
    RED_INLINE Scalar Vector4::Upper3() const
    {
        Quad vA = _mm_max_ss(_mm_shuffle_ps(V, V, _MM_SHUFFLE(0, 0, 0, 0)),
                             _mm_max_ss(_mm_shuffle_ps(V, V, _MM_SHUFFLE(1, 1, 1, 1)), _mm_shuffle_ps(V, V, _MM_SHUFFLE(2, 2, 2, 2))));
        return _mm_shuffle_ps(vA, vA, _MM_SHUFFLE(0, 0, 0, 0));
    }

    //////////////////////////////////////////////////////////////////////////
    RED_INLINE Scalar Vector4::Upper4() const
    {
        Quad vA = _mm_max_ss(_mm_max_ss(_mm_shuffle_ps(V, V, _MM_SHUFFLE(0, 0, 0, 0)), _mm_shuffle_ps(V, V, _MM_SHUFFLE(1, 1, 1, 1))),
                             _mm_max_ss(_mm_shuffle_ps(V, V, _MM_SHUFFLE(2, 2, 2, 2)), _mm_shuffle_ps(V, V, _MM_SHUFFLE(3, 3, 3, 3))));
        return _mm_shuffle_ps(vA, vA, _MM_SHUFFLE(0, 0, 0, 0));
    }

    //////////////////////////////////////////////////////////////////////////
    RED_INLINE Scalar Vector4::Lower3() const
    {
        Quad vA = _mm_min_ss(_mm_shuffle_ps(V, V, _MM_SHUFFLE(0, 0, 0, 0)),
                             _mm_min_ss(_mm_shuffle_ps(V, V, _MM_SHUFFLE(1, 1, 1, 1)), _mm_shuffle_ps(V, V, _MM_SHUFFLE(2, 2, 2, 2))));
        return _mm_shuffle_ps(vA, vA, _MM_SHUFFLE(0, 0, 0, 0));
    }

    //////////////////////////////////////////////////////////////////////////
    RED_INLINE Scalar Vector4::Lower4() const
    {
        Quad vA = _mm_min_ss(_mm_min_ss(_mm_shuffle_ps(V, V, _MM_SHUFFLE(0, 0, 0, 0)), _mm_shuffle_ps(V, V, _MM_SHUFFLE(1, 1, 1, 1))),
                             _mm_min_ss(_mm_shuffle_ps(V, V, _MM_SHUFFLE(2, 2, 2, 2)), _mm_shuffle_ps(V, V, _MM_SHUFFLE(3, 3, 3, 3))));
        return _mm_shuffle_ps(vA, vA, _MM_SHUFFLE(0, 0, 0, 0));
    }

    //////////////////////////////////////////////////////////////////////////
    RED_INLINE Vector4 Vector4::ZeroElement(red::Uint32 _i) const
    {
        Vector4 res(*this);
        res.f[_i] = 0.0f;
        return res;
    }

    //////////////////////////////////////////////////////////////////////////
    RED_INLINE Scalar Vector4::AsScalar(red::Uint32 _i) const
    {
        return f[_i];
    }

    //////////////////////////////////////////////////////////////////////////
    RED_INLINE red::Bool Vector4::IsOk() const
    {
        return ((Xi & 0x7f800000) != 0x7f800000 && (Yi & 0x7f800000) != 0x7f800000 && (Zi & 0x7f800000) != 0x7f800000 && (Wi & 0x7f800000) != 0x7f800000);
    }

    //////////////////////////////////////////////////////////////////////////
    RED_INLINE void Vector4::Store(red::Float* _f) const
    {
        _mm_store_ps(_f, V);
    }

    // TODO: optimize
    //////////////////////////////////////////////////////////////////////////
    RED_INLINE void Vector4::InverseRotateDirection(const vanguard::math::Quaternion& _quat, const Vector4& _v)
    {
        vanguard::math::Vector4 v(_v.X, _v.Y, _v.Z, _v.W);
        RED_MATH_PARANOID_SANITY_CHECK(MAbs(_quat.MagnitudeSq() - 1.f) <= 0.00001f);
        vanguard::math::Vector3 ret = _quat.Conjugate().Transform(v);

        /*Scalar qReal( _quat.r );
        Scalar q2minus1( Sub( Mul ( qReal, qReal ),  0.5f ).V );

        Vector4 ret;
        ret = Mul ( _v, q2minus1 );

        Scalar imagDotDir( Dot3(_quat.GetImaginary(), _v ) );
        SetAdd( ret, Mul( _quat.GetImaginary(), imagDotDir ) );

        Vector4 imagCrossDir;
        imagCrossDir = Cross( _v, _quat.GetImaginary() );
        SetAdd( ret, Mul ( imagCrossDir, qReal ) );

        SetAdd( ret, ret );
        Set(ret);*/
        Set(ret.X, ret.Y, ret.Z, 0.f);
    }

    //////////////////////////////////////////////////////////////////////////
    RED_INLINE void Vector4::RotateDirection(const vanguard::math::Quaternion& _quat, const Vector4& _direction)
    {
        vanguard::math::Vector4 v(_direction.X, _direction.Y, _direction.Z, _direction.W);
        RED_MATH_PARANOID_SANITY_CHECK(MAbs(_quat.MagnitudeSq() - 1.f) <= 0.00001f);
        vanguard::math::Vector3 ret = _quat.Transform(v);

        /*Scalar qreal( _quat.r );
        Scalar q2minus1( Sub( Mul( qreal, qreal ), 0.5f ) );

        Vector4 ret;
        ret = Mul( _direction, q2minus1 );

        Scalar imagDotDir = Dot3(_quat.GetImaginary(), _direction );
        SetAdd( ret, Mul( _quat.GetImaginary(), imagDotDir ) );

        Vector4 imagCrossDir = Cross( _quat.GetImaginary(), _direction );
        SetAdd( ret,  Mul( imagCrossDir, qreal ) );

        SetAdd( ret, ret);
        Set(ret);*/

        Set(ret.X, ret.Y, ret.Z, 0.f);
    }

    RED_INLINE void Vector4::RotateDirectionUnsafe(const vanguard::math::Quaternion& _quat, const Vector4& _direction)
    {
        vanguard::math::Vector4 v(_direction.X, _direction.Y, _direction.Z, _direction.W);
        RED_MATH_PARANOID_SANITY_CHECK(MAbs(_quat.MagnitudeSq() - 1.f) <= 0.00001f);
        vanguard::math::Vector3 ret = _quat.TransformUnsafe(v);

        /*Scalar qreal( _quat.r );
        Scalar q2minus1( Sub( Mul( qreal, qreal ), 0.5f ) );

        Vector4 ret;
        ret = Mul( _direction, q2minus1 );

        Scalar imagDotDir = Dot3(_quat.GetImaginary(), _direction );
        SetAdd( ret, Mul( _quat.GetImaginary(), imagDotDir ) );

        Vector4 imagCrossDir = Cross( _quat.GetImaginary(), _direction );
        SetAdd( ret,  Mul( imagCrossDir, qreal ) );

        SetAdd( ret, ret);
        Set(ret);*/

        Set(ret.X, ret.Y, ret.Z, 0.f);
    }

    //////////////////////////////////////////////////////////////////////////
    RED_INLINE Vector4 Vector4::ZEROS()
    {
        return {0.f, 0.f, 0.f, 0.f};
    }

    RED_INLINE Vector4 Vector4::ZERO_3D_POINT()
    {
        return {0.f, 0.f, 0.f, 1.f};
    }

    RED_INLINE Vector4 Vector4::ONES()
    {
        return {1.f, 1.f, 1.f, 1.f};
    }

    RED_INLINE Vector4 Vector4::EX()
    {
        return {1.f, 0.f, 0.f, 0.f};
    }

    RED_INLINE Vector4 Vector4::EY()
    {
        return {0.f, 1.f, 0.f, 0.f};
    }

    RED_INLINE Vector4 Vector4::EZ()
    {
        return {0.f, 0.f, 1.f, 0.f};
    }

    RED_INLINE Vector4 Vector4::EW()
    {
        return {0.f, 0.f, 0.f, 1.f};
    }

    //////////////////////////////////////////////////////////////////////////
    RED_INLINE ComparisonResult operator==(const Vector4& a, const Vector4& b)
    {
        return ComparisonResult(_mm_cmpeq_ps(a.V, b.V));
    }

    RED_INLINE ComparisonResult operator!=(const Vector4& a, const Vector4& b)
    {
        return ComparisonResult(_mm_cmpneq_ps(a.V, b.V));
    }

    RED_INLINE ComparisonResult operator>(const Vector4& a, const Vector4& b)
    {
        return ComparisonResult(_mm_cmpgt_ps(a.V, b.V));
    }

    RED_INLINE ComparisonResult operator>=(const Vector4& a, const Vector4& b)
    {
        return ComparisonResult(_mm_cmpge_ps(a.V, b.V));
    }

    RED_INLINE ComparisonResult operator<(const Vector4& a, const Vector4& b)
    {
        return ComparisonResult(_mm_cmplt_ps(a.V, b.V));
    }

    RED_INLINE ComparisonResult operator<=(const Vector4& a, const Vector4& b)
    {
        return ComparisonResult(_mm_cmple_ps(a.V, b.V));
    }
} // namespace vanguard::math::simd
