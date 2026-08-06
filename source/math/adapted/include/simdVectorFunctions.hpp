namespace vanguard::math::simd
{
    //////////////////////////////////////////////////////////////////////////
    void SqrRoot(Scalar& _a)
    {
        _a.V = _mm_sqrt_ss(_a.V);
        _a.V = _mm_shuffle_ps(_a.V, _a.V, _MM_SHUFFLE(0, 0, 0, 0));
    }

    //////////////////////////////////////////////////////////////////////////
    Scalar SqrRoot(const Scalar& _a)
    {
        Quad vA = _mm_sqrt_ss(_a.V);
        return _mm_shuffle_ps(vA, vA, _MM_SHUFFLE(0, 0, 0, 0));
    }

    //////////////////////////////////////////////////////////////////////////
    void SqrRoot(Vector4& _a)
    {
        _a.V = _mm_sqrt_ps(_a.V);
    }

    //////////////////////////////////////////////////////////////////////////
    Vector4 SqrRoot(const Vector4& _a)
    {
        return Vector4(_mm_sqrt_ps(_a.V));
    }

    //////////////////////////////////////////////////////////////////////////
    void SqrRoot3(Vector4& _a)
    {
        _a.V = _mm_add_ps(_mm_and_ps(_a.V, W_MASK), _mm_sqrt_ps(_mm_and_ps(_a.V, XYZ_MASK)));
    }

    //////////////////////////////////////////////////////////////////////////
    Vector4 SqrRoot3(const Vector4& _a)
    {
        return Vector4(_mm_add_ps(_mm_and_ps(_a.V, W_MASK), _mm_sqrt_ps(_mm_and_ps(_a.V, XYZ_MASK))));
    }

    //////////////////////////////////////////////////////////////////////////
    // 'RECIPROCAL SQUARE ROOT' Functions
    //////////////////////////////////////////////////////////////////////////
    void RSqrRoot(Scalar& _a)
    {
        _a.V = _mm_rsqrt_ss(_a.V);
        _a.V = _mm_shuffle_ps(_a.V, _a.V, _MM_SHUFFLE(0, 0, 0, 0));
    }

    //////////////////////////////////////////////////////////////////////////
    Scalar RSqrRoot(const Scalar& _a)
    {
        Quad vA = _mm_rsqrt_ss(_a.V);
        return _mm_shuffle_ps(vA, vA, _MM_SHUFFLE(0, 0, 0, 0));
    }

    //////////////////////////////////////////////////////////////////////////
    void RSqrRoot(Vector4& _a)
    {
        _a.V = _mm_rsqrt_ps(_a.V);
    }

    //////////////////////////////////////////////////////////////////////////
    Vector4 RSqrRoot(const Vector4& _a)
    {
        return Vector4(_mm_rsqrt_ps(_a.V));
    }

    //////////////////////////////////////////////////////////////////////////
    void RSqrRoot3(Vector4& _a)
    {
        Quad a = _mm_add_ps(_mm_and_ps(_a.V, XYZ_MASK), _mm_setr_ps(0.0f, 0.0f, 0.0f, 1.0f));
        Quad b = _mm_rsqrt_ps(a);
        Quad c = _mm_and_ps(_a.V, W_MASK);
        _a.V = _mm_add_ps(_mm_and_ps(b, XYZ_MASK), c);
    }

    //////////////////////////////////////////////////////////////////////////
    Vector4 RSqrRoot3(const Vector4& _a)
    {
        Quad a = _mm_add_ps(_mm_and_ps(_a.V, XYZ_MASK), _mm_setr_ps(0.0f, 0.0f, 0.0f, 1.0f));
        Quad b = _mm_rsqrt_ps(a);
        Quad c = _mm_and_ps(_a.V, W_MASK);
        return Vector4(_mm_add_ps(_mm_and_ps(b, XYZ_MASK), c));
    }

    RED_FORCE_INLINE __m128 RcpSqrtPrecise(const __m128& v)
    {
        const __m128 half = _mm_set1_ps(0.5f);
        const __m128 three = _mm_set1_ps(3.f);
        const __m128 approx = _mm_rsqrt_ps(v);
        const __m128 muls = _mm_mul_ps(_mm_mul_ps(v, approx), approx);
        return _mm_mul_ps(_mm_mul_ps(half, approx), _mm_sub_ps(three, muls));
    }

    //////////////////////////////////////////////////////////////////////////
    // 'NORMALIZED DOT PRODUCT' Functions
    // Ensure the input vectors are of unit length.
    //////////////////////////////////////////////////////////////////////////
    void UnitDot(Scalar& _a, const Vector4& _b, const Vector4& _c)
    {
        Scalar dotProduct = Dot(_b, _c);
        Scalar lengthSqr(Mul(_b.SquareLength4(), _c.SquareLength4()));
        SqrRoot(lengthSqr);
        if (lengthSqr.IsZero())
        {
            _a.SetZeros();
            return;
        }
        dotProduct = _mm_div_ss(dotProduct.V, lengthSqr.V);
        _a.V = _mm_shuffle_ps(dotProduct.V, dotProduct.V, _MM_SHUFFLE(0, 0, 0, 0));
    }

    //////////////////////////////////////////////////////////////////////////
    Scalar UnitDot(const Vector4& _a, const Vector4& _b)
    {
        Scalar dotProduct = Dot(_a, _b);
        Scalar lengthSqr(Mul(_a.SquareLength4(), _b.SquareLength4()));
        SqrRoot(lengthSqr);
        if (lengthSqr.IsZero())
        {
            return 0.0f;
        }
        dotProduct = _mm_div_ss(dotProduct.V, lengthSqr.V);
        return _mm_shuffle_ps(dotProduct.V, dotProduct.V, _MM_SHUFFLE(0, 0, 0, 0));
    }

    //////////////////////////////////////////////////////////////////////////
    void UnitDot3(Scalar& _a, const Vector4& _b, const Vector4& _c)
    {
        Scalar dotProduct = Dot3(_b, _c);
        Scalar lengthSqr(Mul(_b.SquareLength3(), _c.SquareLength3()));
        SqrRoot(lengthSqr);
        if (lengthSqr.IsZero())
        {
            _a.SetZeros();
            return;
        }
        dotProduct = _mm_div_ss(dotProduct.V, lengthSqr.V);
        _a.V = _mm_shuffle_ps(dotProduct.V, dotProduct.V, _MM_SHUFFLE(0, 0, 0, 0));
    }

    //////////////////////////////////////////////////////////////////////////
    Scalar UnitDot3(const Vector4& _a, const Vector4& _b)
    {
        Scalar dotProduct = Dot3(_a, _b);
        Scalar lengthSqr(Mul(_a.SquareLength3(), _b.SquareLength3()));
        SqrRoot(lengthSqr);
        if (lengthSqr.IsZero())
        {
            return 0.0f;
        }
        dotProduct = _mm_div_ss(dotProduct.V, lengthSqr.V);
        return _mm_shuffle_ps(dotProduct.V, dotProduct.V, _MM_SHUFFLE(0, 0, 0, 0));
    }

    //////////////////////////////////////////////////////////////////////////
    // 'CROSS PRODUCT' Functions
    //////////////////////////////////////////////////////////////////////////
    void Cross(Vector4& _a, const Vector4& _b, const Vector4& _c)
    {
        _a.V = _mm_sub_ps(_mm_mul_ps(_b.V, _mm_shuffle_ps(_c.V, _c.V, _MM_SHUFFLE(3, 0, 2, 1))),
                          _mm_mul_ps(_c.V, _mm_shuffle_ps(_b.V, _b.V, _MM_SHUFFLE(3, 0, 2, 1))));
        _a.V = _mm_shuffle_ps(_a.V, _a.V, _MM_SHUFFLE(3, 0, 2, 1));
        _a.W = 1.0f;
    }

    //////////////////////////////////////////////////////////////////////////
    Vector4 Cross(const Vector4& _a, const Vector4& _b)
    {
        Vector4 c;
        c.V = _mm_sub_ps(_mm_mul_ps(_a.V, _mm_shuffle_ps(_b.V, _b.V, _MM_SHUFFLE(3, 0, 2, 1))),
                         _mm_mul_ps(_b.V, _mm_shuffle_ps(_a.V, _a.V, _MM_SHUFFLE(3, 0, 2, 1))));
        c.V = _mm_shuffle_ps(c.V, c.V, _MM_SHUFFLE(3, 0, 2, 1));
        c.W = 1.0f;

        return c;
    }

    //////////////////////////////////////////////////////////////////////////
    // 'DOT PRODUCT' Functions
    //////////////////////////////////////////////////////////////////////////
    RED_INLINE void Dot(Scalar& _a, const Vector4& _b, const Vector4& _c)
    {
        __m128 r1 = _mm_mul_ps(_b.V, _c.V);
        __m128 r2 = _mm_hadd_ps(r1, r1);
        __m128 r3 = _mm_hadd_ps(r2, r2);
        _a.V = r3;
    }

    //////////////////////////////////////////////////////////////////////////
    RED_INLINE Scalar Dot(const Vector4& _a, const Vector4& _b)
    {
        __m128 r1 = _mm_mul_ps(_a.V, _b.V);
        __m128 r2 = _mm_hadd_ps(r1, r1);
        __m128 r3 = _mm_hadd_ps(r2, r2);
        return r3;
    }

    //////////////////////////////////////////////////////////////////////////
    RED_INLINE void Dot3(Scalar& _a, const Vector4& _b, const Vector4& _c)
    {
        __m128 r1 = _mm_and_ps(_mm_mul_ps(_b.V, _c.V), XYZ_MASK);
        __m128 r2 = _mm_hadd_ps(r1, r1);
        __m128 r3 = _mm_hadd_ps(r2, r2);
        _a.V = r3;
    }

    //////////////////////////////////////////////////////////////////////////
    RED_INLINE Scalar Dot3(const Vector4& _a, const Vector4& _b)
    {
        __m128 r1 = _mm_and_ps(_mm_mul_ps(_a.V, _b.V), XYZ_MASK);
        __m128 r2 = _mm_hadd_ps(r1, r1);
        __m128 r3 = _mm_hadd_ps(r2, r2);
        return r3;
    }

    //////////////////////////////////////////////////////////////////////////
    RED_INLINE void CalculatePerpendicularVector(const Vector4& _in, Vector4& _out)
    {
        int min = 0;
        int eleA = 1;
        int eleB = 2;

        float vA = vanguard::math::Abs(_in.X);
        float vB = vanguard::math::Abs(_in.Y);
        float vC = vanguard::math::Abs(_in.Z);

        if (vB < vA)
        {
            eleA = 0;
            min = 1;
            vA = vB;
        }

        if (vC < vA)
        {
            eleB = min;
            min = 2;
        }

        _out.SetZeros();
        _out.f[eleA] = _in.f[eleB];
        _out.f[eleB] = _in.f[eleA];
    }

    //////////////////////////////////////////////////////////////////////////
    RED_INLINE void AxisRotateVector(Vector4& vec, const Vector4& normAxis, float angle)
    {
        vanguard::math::Vector4 redAxis(normAxis.X, normAxis.Y, normAxis.Z, 0.0f);
        vanguard::math::Quaternion rotQuat(redAxis, angle);

        Vector4 redVec;
        Vector4 redRotated;

        redVec.Set(vec.X, vec.Y, vec.Z, 0.0f);
        redRotated.RotateDirection(rotQuat, redVec);
        vec = redRotated;
    }

} // namespace vanguard::math::simd

namespace vanguard::math::simd
{
    RED_FORCE_INLINE __m128 Lerp(const __m128& a, const __m128& b, const __m128& t)
    {
        return _mm_add_ps(a, _mm_mul_ps(_mm_sub_ps(b, a), t));
    }

    RED_FORCE_INLINE __m128 MulAdd(const __m128& a, const __m128& b, const __m128& c)
    {
        return _mm_add_ps(c, _mm_mul_ps(a, b));
    }

    RED_FORCE_INLINE __m128 Dot(const __m128& xxxx, const __m128& yyyy, const __m128& zzzz, const __m128& xxxx1, const __m128& yyyy1,
                                const __m128& zzzz1)
    {
        return MulAdd(zzzz, zzzz1, MulAdd(xxxx, xxxx1, _mm_mul_ps(yyyy, yyyy1)));
    }

    RED_FORCE_INLINE __m128 Dot(const __m128& xxxx, const __m128& yyyy, const __m128& zzzz, const __m128& wwww, const __m128& xxxx1,
                                const __m128& yyyy1, const __m128& zzzz1, const __m128& wwww1)
    {
        return MulAdd(wwww, wwww1, MulAdd(zzzz, zzzz1, MulAdd(xxxx, xxxx1, _mm_mul_ps(yyyy, yyyy1))));
    }

    RED_FORCE_INLINE void Normalize(__m128* outXXXX, __m128* outYYYY, __m128* outZZZZ, __m128* outWWWW, const __m128& xxxx,
                                    const __m128& yyyy, const __m128& zzzz, const __m128& wwww)
    {
        const __m128 lengthSqr = Dot(xxxx, yyyy, zzzz, wwww, xxxx, yyyy, zzzz, wwww);
        const __m128 lengthRcp = _mm_rsqrt_ps(lengthSqr);
        outXXXX[0] = _mm_mul_ps(xxxx, lengthRcp);
        outYYYY[0] = _mm_mul_ps(yyyy, lengthRcp);
        outZZZZ[0] = _mm_mul_ps(zzzz, lengthRcp);
        outWWWW[0] = _mm_mul_ps(wwww, lengthRcp);
    }

    // this could be used as an interpolation function for quaternions.
    // ex. in animation sampling
    RED_FORCE_INLINE void QuatNLerp(__m128* outXXXX, __m128* outYYYY, __m128* outZZZZ, __m128* outWWWW, const __m128& xxxx,
                                    const __m128& yyyy, const __m128& zzzz, const __m128& wwww, const __m128& xxxx1, const __m128& yyyy1,
                                    const __m128& zzzz1, const __m128& wwww1, const __m128& t)
    {
        const __m128 signMask = _mm_castsi128_ps(_mm_set1_epi32(0x80000000));

        const __m128 d = Dot(xxxx, yyyy, zzzz, wwww, xxxx1, yyyy1, zzzz1, wwww1);
        const __m128 lt = _mm_sub_ps(_mm_set1_ps(1.f), t);
        const __m128 rt = _mm_xor_ps(t, _mm_and_ps(d, signMask));

        const __m128 tmpXXXX = _mm_add_ps(_mm_mul_ps(xxxx, lt), _mm_mul_ps(xxxx1, rt));
        const __m128 tmpYYYY = _mm_add_ps(_mm_mul_ps(yyyy, lt), _mm_mul_ps(yyyy1, rt));
        const __m128 tmpZZZZ = _mm_add_ps(_mm_mul_ps(zzzz, lt), _mm_mul_ps(zzzz1, rt));
        const __m128 tmpWWWW = _mm_add_ps(_mm_mul_ps(wwww, lt), _mm_mul_ps(wwww1, rt));

        Normalize(outXXXX, outYYYY, outZZZZ, outWWWW, tmpXXXX, tmpYYYY, tmpZZZZ, tmpWWWW);
    }

    // functions to write computation result to data in AoS (Array of Structs) format
    RED_FORCE_INLINE void Scatter3(::vanguard::math::simd::Vector4* output0, ::vanguard::math::simd::Vector4* output1,
                                   ::vanguard::math::simd::Vector4* output2, ::vanguard::math::simd::Vector4* output3,
                                   const __m128 input[3], const __m128& w)
    {
        __m128 tmp[4] = {input[0], input[1], input[2], w};

        _MM_TRANSPOSE4_PS(tmp[0], tmp[1], tmp[2], tmp[3]);

        output0[0].V = tmp[0];
        output1[0].V = tmp[1];
        output2[0].V = tmp[2];
        output3[0].V = tmp[3];
    }

    RED_FORCE_INLINE void Scatter4(::vanguard::math::simd::Vector4* output0, ::vanguard::math::simd::Vector4* output1,
                                   ::vanguard::math::simd::Vector4* output2, ::vanguard::math::simd::Vector4* output3,
                                   const __m128 input[4])
    {
        __m128 tmp[4] = {input[0], input[1], input[2], input[3]};

        _MM_TRANSPOSE4_PS(tmp[0], tmp[1], tmp[2], tmp[3]);

        output0[0].V = tmp[0];
        output1[0].V = tmp[1];
        output2[0].V = tmp[2];
        output3[0].V = tmp[3];
    }

} // namespace vanguard::math::simd
