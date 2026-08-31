/**
 * Copyright (c) 2013 CD Projekt Red. All Rights Reserved.
 */

namespace vanguard::math::simd
{
    namespace QuadHelper
    {
        Quad Negate(Quad v)
        {
            return _mm_xor_ps(v, SIGN_MASK);
        }

        Quad Dot(Quad _a, Quad _b)
        {
            Quad r1 = _mm_mul_ps(_a, _b);
            Quad r2 = _mm_hadd_ps(r1, r1);
            Quad r3 = _mm_hadd_ps(r2, r2);
            return r3;
        }

        Quad Cross(Quad _a, Quad _b)
        {
            Quad c =
                _mm_sub_ps(_mm_mul_ps(_a, _mm_shuffle_ps(_b, _b, _MM_SHUFFLE(3, 0, 2, 1))), _mm_mul_ps(_b, _mm_shuffle_ps(_a, _a, _MM_SHUFFLE(3, 0, 2, 1))));
            return _mm_shuffle_ps(c, c, _MM_SHUFFLE(3, 0, 2, 1));
        }

        Quad RotateDirectionUnsafe(Quad quat, Quad vec)
        {
            Quad t = Cross(quat, vec);
            t = _mm_add_ps(t, t);
            return _mm_add_ps(vec, _mm_add_ps(_mm_mul_ps(t, _mm_shuffle_ps(quat, quat, _MM_SHUFFLE(3, 3, 3, 3))), Cross(quat, t)));
        }

        Quad Normalize4(Quad v)
        {
            Quad vA = _mm_mul_ps(v, v);
            vA = _mm_add_ss(_mm_add_ss(_mm_shuffle_ps(vA, vA, _MM_SHUFFLE(0, 0, 0, 0)), _mm_shuffle_ps(vA, vA, _MM_SHUFFLE(1, 1, 1, 1))),
                            _mm_add_ss(_mm_shuffle_ps(vA, vA, _MM_SHUFFLE(2, 2, 2, 2)), _mm_shuffle_ps(vA, vA, _MM_SHUFFLE(3, 3, 3, 3))));
            Quad length = _mm_sqrt_ss(vA);
            length = _mm_shuffle_ps(length, length, _MM_SHUFFLE(0, 0, 0, 0));
            Quad hasLength = _mm_cmpeq_ss(length, _mm_setzero_ps());
            hasLength = _mm_shuffle_ps(hasLength, hasLength, _MM_SHUFFLE(0, 0, 0, 0));
            Quad unitLength = _mm_div_ps(_mm_set1_ps(1.0f), length);
            return _mm_andnot_ps(hasLength, _mm_mul_ps(v, unitLength));
        }

        Quad RotateDirection(Quad quat, Quad vec)
        {
            Quad normalizedQuat = Normalize4(quat);
            return RotateDirectionUnsafe(normalizedQuat, vec);
        }

        Quad QuaternionMultiplication(Quad xyzw, Quad abcd)
        {
            // Based on https://stackoverflow.com/questions/18542894/how-to-multiply-two-quaternions-with-minimal-instructions

            /* The product of two quaternions is:                                 */
            /* (X,Y,Z,W) = (xd+yc-zb+wa, -xc+yd+za+wb, xb-ya+zd+wc, -xa-yb-zc+wd) */

            Quad wzyx = _mm_shuffle_ps(xyzw, xyzw, _MM_SHUFFLE(0, 1, 2, 3));
            Quad baba = _mm_shuffle_ps(abcd, abcd, _MM_SHUFFLE(0, 1, 0, 1));
            Quad dcdc = _mm_shuffle_ps(abcd, abcd, _MM_SHUFFLE(2, 3, 2, 3));

            /* variable names below are for parts of componens of result (X,Y,Z,W) */
            /* nX stands for -X and similarly for the other components             */

            /* znxwy  = (xb - ya, zb - wa, wd - zc, yd - xc) */
            Quad ZnXWY = _mm_hsub_ps(_mm_mul_ps(xyzw, baba), _mm_mul_ps(wzyx, dcdc));

            /* xzynw  = (xd + yc, zd + wc, wb + za, yb + xa) */
            Quad XZYnW = _mm_hadd_ps(_mm_mul_ps(xyzw, dcdc), _mm_mul_ps(wzyx, baba));

            /* _mm_shuffle_ps(XZYnW, ZnXWY, _MM_SHUFFLE(3,2,1,0)) */
            /*      = (xd + yc, zd + wc, wd - zc, yd - xc)        */
            /* _mm_shuffle_ps(ZnXWY, XZYnW, _MM_SHUFFLE(2,3,0,1)) */
            /*      = (zb - wa, xb - ya, yb + xa, wb + za)        */

            /* _mm_addsub_ps adds elements 1 and 3 and subtracts elements 0 and 2, so we get: */
            /* _mm_addsub_ps(*, *) = (xd+yc-zb+wa, xb-ya+zd+wc, wd-zc+yb+xa, yd-xc+wb+za)     */

            Quad XZWY = _mm_addsub_ps(_mm_shuffle_ps(XZYnW, ZnXWY, _MM_SHUFFLE(3, 2, 1, 0)), _mm_shuffle_ps(ZnXWY, XZYnW, _MM_SHUFFLE(2, 3, 0, 1)));

            /* now we only need to shuffle the components in place and return the result      */
            return _mm_shuffle_ps(XZWY, XZWY, _MM_SHUFFLE(2, 1, 3, 0));

            /* operations: 6 shuffles, 4 multiplications, 3 compound additions/subtractions   */
        }

        Quad QuaternionLerpWithoutShortestPathCheck(Quad q1, Quad q2, Float t)
        {
            return Normalize4(_mm_add_ps(_mm_mul_ps(q1, _mm_set_ps1(1 - t)), _mm_mul_ps(q2, _mm_set_ps1(t))));
        }

        Quad QuaternionLerp(Quad q1, Quad q2, Float t)
        {
            ///
            ///	Ensure the interpolation follows the shortest path
            ///
            const Quad cosTheta = Dot(q1, q2);
            const Quad mask = _mm_and_ps(cosTheta, SIGN_MASK);
            q2 = _mm_xor_ps(q2, mask);
            return QuaternionLerpWithoutShortestPathCheck(q1, q2, t);
        }

        Quad QuaternionSlerpWithoutShortestPathCheck(Quad q1, Quad q2, Float t, Quad cosTheta, Float tolerance)
        {
            ///
            ///	If the angle between two quaternions is minuscule, go back to a standard LERP, or else we'll divide
            ///	by zero here.
            ///
            const Float scalarCosTheta = Scalar(cosTheta).X;
            if (scalarCosTheta > tolerance)
            {
                return QuaternionLerpWithoutShortestPathCheck(q1, q2, t);
            }
            else
            {
                const Float theta = MAcos_safe(scalarCosTheta);
                const Float sin_theta = MSin(theta);
                const Float q1_modifier = MSin((1.f - t) * theta);
                const Float q2_modifier = MSin(t * theta);

                return _mm_div_ps(_mm_add_ps(_mm_mul_ps(q1, _mm_set_ps1(q1_modifier)), _mm_mul_ps(q2, _mm_set_ps1(q2_modifier))), _mm_set_ps1(sin_theta));
            }
        }

        Quad QuaternionSlerp(Quad q1, Quad q2, Float t, Float tolerance)
        {
            ///
            ///	Ensure the interpolation follows the shortest path
            ///
            Quad cosTheta = Dot(q1, q2);
            const Quad mask = _mm_and_ps(cosTheta, SIGN_MASK);
            q2 = _mm_xor_ps(q2, mask);
            cosTheta = _mm_xor_ps(cosTheta, mask);
            return QuaternionSlerpWithoutShortestPathCheck(q1, q2, t, cosTheta, tolerance);
        }

        Quad TransformVector(Quad translation, Quad rotation, Quad scale, Quad vectorToTransform)
        {
            const Quad extra = QuadHelper::RotateDirection(rotation, _mm_mul_ps(vectorToTransform, scale));
            return _mm_add_ps(translation, extra);
        }

        Quad TransformVectorUnsafe(Quad translation, Quad rotation, Quad scale, Quad vectorToTransform)
        {
            const Quad extra = QuadHelper::RotateDirectionUnsafe(rotation, _mm_mul_ps(vectorToTransform, scale));
            return _mm_add_ps(translation, extra);
        }

        Quad Conjugate(Quad q)
        {
            static const Quad mask = _mm_set_ps(0.f, -0.f, -0.f, -0.f);
            return _mm_xor_ps(q, mask);
        }
    } // namespace QuadHelper

    RED_INLINE QsTransform::QsTransform() : Scale(Vector4::ONES()) // Translation and Rotation use default initialization
    {
    }

    RED_INLINE QsTransform::QsTransform(const QsTransform& xform) : Translation(xform.Translation), Rotation(xform.Rotation), Scale(xform.Scale) {}

    RED_INLINE QsTransform::QsTransform(const Vector4& _translation, const vanguard::math::Quaternion& _rotation, const Vector4& _scale)
        : Translation(_translation), Rotation(_rotation), Scale(_scale)
    {
        RED_MATH_PARANOID_SANITY_CHECK(IsOk());
    }

    RED_INLINE QsTransform::QsTransform(const Vector4& _translation, const vanguard::math::Quaternion& _rotation)
        : QsTransform{_translation, _rotation, Vector4::ONES()}
    {
        RED_MATH_PARANOID_SANITY_CHECK(IsOk());
    }

    void QsTransform::operator=(const QsTransform& _other)
    {
        Translation = _other.Translation;
        Rotation = _other.Rotation;
        Scale = _other.Scale;

        RED_MATH_PARANOID_SANITY_CHECK(IsOk());
    }

    void QsTransform::Set(const Vector4& _translation, const vanguard::math::Quaternion& _rotation, const Vector4& _scale)
    {
        Translation = _translation;
        Rotation = _rotation.Normalized();
        Scale = _scale;
        RED_MATH_PARANOID_SANITY_CHECK(IsOk());
    }

    void QsTransform::Set(const vanguard::math::Matrix& _m)
    {
        Translation = vanguard::math::simd::Vector4(_m.GetTranslation().AsFloat());
        Scale = vanguard::math::simd::Vector4(_m.GetScale33().AsFloat());
        Rotation = _m.ToQuat();
        RED_MATH_PARANOID_SANITY_CHECK(IsOk());
    }

    void QsTransform::SetIdentity()
    {
        Translation.SetZeros();
        Rotation.SetIdentity();
        Scale.SetOnes();
    }

    void QsTransform::SetZero()
    {
        Translation.SetZeros();
        Rotation.SetZeros();
        Scale.SetZeros();
    }

    void QsTransform::SetTranslation(const Vector4& _v)
    {
        Translation = _v;
        RED_MATH_PARANOID_SANITY_CHECK(IsOk());
    }

    void QsTransform::SetRotation(const vanguard::math::Quaternion& _r)
    {
        Rotation = _r.Normalized();
        RED_MATH_PARANOID_SANITY_CHECK(IsOk());
    }

    void QsTransform::SetScale(const Vector4& _s)
    {
        Scale = _s;
        RED_MATH_PARANOID_SANITY_CHECK(IsOk());
    }

    const Vector4& QsTransform::GetTranslation() const
    {
        return Translation;
    }

    const vanguard::math::Quaternion& QsTransform::GetRotation() const
    {
        return Rotation;
    }

    const Vector4& QsTransform::GetScale() const
    {
        return Scale;
    }

    void QsTransform::Lerp(const QsTransform& _a, const QsTransform& _b, float _t)
    {
        Scale = Vector4::Lerp(_a.Scale, _b.Scale, _t);
        Translation = Vector4::Lerp(_a.Translation, _b.Translation, _t);
        Rotation = vanguard::math::Quaternion::Lerp(_a.Rotation, _b.Rotation, _t);
        RED_MATH_PARANOID_SANITY_CHECK(IsOk());
    }

    void QsTransform::Slerp(const QsTransform& _a, const QsTransform& _b, float _t)
    {
        Scale = Vector4::Lerp(_a.Scale, _b.Scale, _t);
        Translation = Vector4::Lerp(_a.Translation, _b.Translation, _t);
        Rotation = vanguard::math::Quaternion::Slerp(_a.Rotation, _b.Rotation, _t);
        RED_MATH_PARANOID_SANITY_CHECK(IsOk());
    }

    RED_INLINE vanguard::math::Matrix QsTransform::ConvertToMatrix() const
    {

        vanguard::math::Vector4 q = Rotation.AsVector();

        float x2 = q.X + q.X;
        float y2 = q.Y + q.Y;
        float z2 = q.Z + q.Z;
        float xx = q.X * x2;
        float xy = q.X * y2;
        float xz = q.X * z2;
        float yy = q.Y * y2;
        float yz = q.Y * z2;
        float zz = q.Z * z2;
        float wx = q.W * x2;
        float wy = q.W * y2;
        float wz = q.W * z2;

        return vanguard::math::Matrix(vanguard::math::Vector4((1.0f - (yy + zz)) * Scale.X, (xy + wz) * Scale.X, (xz - wy) * Scale.X, 0.0f),
                                      vanguard::math::Vector4((xy - wz) * Scale.Y, (1.0f - (xx + zz)) * Scale.Y, (yz + wx) * Scale.Y, 0.0f),
                                      vanguard::math::Vector4((xz + wy) * Scale.Z, (yz - wx) * Scale.Z, (1.0f - (xx + yy)) * Scale.Z, 0.0f),
                                      vanguard::math::Vector4(Translation.X, Translation.Y, Translation.Z, 1.0f));
    }

    RED_INLINE void QsTransform::ConvertToMatrix(vanguard::math::Matrix& m) const
    {
        const float x2 = Rotation.i + Rotation.i;
        const float y2 = Rotation.j + Rotation.j;
        const float z2 = Rotation.k + Rotation.k;
        const float xx = Rotation.i * x2;
        const float xy = Rotation.i * y2;
        const float xz = Rotation.i * z2;
        const float yy = Rotation.j * y2;
        const float yz = Rotation.j * z2;
        const float zz = Rotation.k * z2;
        const float wx = Rotation.r * x2;
        const float wy = Rotation.r * y2;
        const float wz = Rotation.r * z2;

        m.X.X = (1.0f - (yy + zz)) * Scale.X;
        m.X.Y = (xy + wz) * Scale.X;
        m.X.Z = (xz - wy) * Scale.X;
        m.X.W = 0.0f;

        m.Y.X = (xy - wz) * Scale.Y;
        m.Y.Y = (1.0f - (xx + zz)) * Scale.Y;
        m.Y.Z = (yz + wx) * Scale.Y;
        m.Y.W = 0.0f;

        m.Z.X = (xz + wy) * Scale.Z;
        m.Z.Y = (yz - wx) * Scale.Z;
        m.Z.Z = (1.0f - (xx + yy)) * Scale.Z;
        m.Z.W = 0.0f;

        m.W.X = Translation.X;
        m.W.Y = Translation.Y;
        m.W.Z = Translation.Z;
        m.W.W = 1.0f;
    }

    RED_INLINE void QsTransform::ConvertToMatrixFast(vanguard::math::Matrix& m) const
    {
        // Based on http://www.mrelusive.com/publications/papers/SIMD-From-Quaternion-to-Matrix-and-Back.pdf

        Quad xmm0 = Rotation.vec.vec; // xmm0 = q.x, q.y, q.z, q.w

        Quad xmm1 = _mm_add_ps(xmm0, xmm0); // xmm1 = x2, y2, z2, w2

        Quad xmm2 = _mm_shuffle_ps(xmm0, xmm0, _MM_SHUFFLE(1, 0, 0, 1)); // xmm2 = y, x, x, y
        Quad xmm3 = _mm_shuffle_ps(xmm1, xmm1, _MM_SHUFFLE(2, 2, 1, 1)); // xmm3 = y2, y2, z2, z2
        xmm2 = _mm_mul_ps(xmm2, xmm3);                                   // xmm2 = yy2, xy2, xz2, yz2

        Quad xmm4 = _mm_shuffle_ps(xmm0, xmm0, _MM_SHUFFLE(3, 3, 3, 2)); // xmm4 = z, w, w, w
        Quad xmm5 = _mm_shuffle_ps(xmm1, xmm1, _MM_SHUFFLE(0, 1, 2, 2)); // xmm5 = z2, z2, y2, x2
        xmm4 = _mm_mul_ps(xmm4, xmm5);                                   // xmm4 = zz2, wz2, wy2, wx2

        xmm0 = _mm_mul_ss(xmm0, xmm1); // xmm0 = xx2, y2, z2, w2

        // calculate the last two elements of the third row
        const Quad one = _mm_set_ss(1.f); // one = 1, 0, 0, 0
        Quad xmm7 = one;                  // xmm7 = 1, 0, 0, 0
        xmm7 = _mm_sub_ss(xmm7, xmm0);    // xmm7 = -xx2+1, 0, 0, 0
        xmm7 = _mm_sub_ss(xmm7, xmm2);    // xmm7 = -xx2-yy2+1, 0, 0, 0

        // calculate first row
        const Quad yzw_mask = _mm_set_ps(-0.f, -0.f, -0.f, 0.f);
        const Quad xzw_mask = _mm_set_ps(-0.f, -0.f, 0.f, -0.f);
        xmm2 = _mm_xor_ps(xmm2, yzw_mask);                              // xmm2 = yy2, -xy2, -xz2, -yz2
        xmm4 = _mm_xor_ps(xmm4, xzw_mask);                              // xmm4 = -zz2, wz2, -wy2, -wx2
        xmm4 = _mm_add_ss(xmm4, one);                                   // xmm4 = -zz2+1, wz2, -wy2, -wx2
        xmm3 = xmm4;                                                    // xmm3 = -zz2+1, wz2, -wy2, -wx2
        xmm3 = _mm_sub_ps(xmm3, xmm2);                                  // xmm3 = -yy2-zz2+1, xy2+wz2, xz2-wy2, yz2-wx2
        Quad row0 = xmm3;                                               // row0 = -yy2-zz2+1, xy2+wz2, xz2-wy2, yz2-wx2
        const Quad scaleX = _mm_set_ps(0.f, Scale.X, Scale.X, Scale.X); // scaleX = Scale.X, Scale.X, Scale.X, 0
        row0 = _mm_mul_ps(row0, scaleX);                                // row0 = (-yy2-zz2+1)*Scale.X, (xy2+wz2)*Scale.X, (xz2-wy2)*Scale.X, 0
        m.X.vec = row0;

        // calculate second row
        xmm2 = _mm_move_ss(xmm2, xmm0);                                 // xmm2 = xx2, -xy2, -xz2, -yz2
        xmm4 = _mm_xor_ps(xmm4, yzw_mask);                              // xmm4 = -zz2+1, -wz2, wy2, wx2
        xmm4 = _mm_sub_ps(xmm4, xmm2);                                  // xmm4 = -xx2-zz2+1, xy2-wz2, xz2+wy2, yz2+wx2
        xmm4 = _mm_shuffle_ps(xmm4, xmm4, _MM_SHUFFLE(2, 3, 0, 1));     // xmm4 = xy2-wz2, -xx2-zz2+1, yz2+wx2, xz2+wy2
        Quad row1 = xmm4;                                               // row1 = xy2-wz2, -xx2-zz2+1, yz2+wx2, xz2+wy2
        const Quad scaleY = _mm_set_ps(0.f, Scale.Y, Scale.Y, Scale.Y); // scaleY = Scale.Y, Scale.Y, Scale.Y, 0
        row1 = _mm_mul_ps(row1, scaleY);                                // row1 = (xy2-wz2)*Scale.Y, (-xx2-zz2+1)*Scale.Y, (yz2+wx2)*Scale.Y, 0
        m.Y.vec = row1;

        // calculate third row
        xmm3 = _mm_movehl_ps(xmm3, xmm4);                           // xmm3 = yz2+wx2, xz2+wy2, xz2-wy2, yz2-wx2
        xmm3 = _mm_shuffle_ps(xmm3, xmm7, _MM_SHUFFLE(2, 0, 3, 1)); // xmm3 = xz2+wy2, yz2-wx2, -xx2-yy2+1, 0.f
        Quad row2 = xmm3;                                           // row2 = xz2+wy2, yz2-wx2, -xx2-yy2+1, 0.f
        const Quad scaleZ = _mm_set_ps1(Scale.Z);                   // scaleZ = Scale.Z, Scale.Z, Scale.Z, Scale.Z
        row2 = _mm_mul_ps(row2, scaleZ);                            // row2 = (xz2+wy2)*Scale.Z, (yz2-wx2)*Scale.Z, (-xx2-yy2+1)*Scale.Z, 0.f
        m.Z.vec = row2;

        m.W.vec = Translation.V;
        m.W.W = 1.f;
    }

    RED_INLINE vanguard::math::Matrix QsTransform::ConvertToMatrixNormalized() const
    {
        // TODO: Chris I know this isn't the nicest bit of code - I need to refactor this further
        vanguard::math::Vector4 q = Rotation.AsVector();
        q.Normalize4();

        float x2 = q.X + q.X;
        float y2 = q.Y + q.Y;
        float z2 = q.Z + q.Z;
        float xx = q.X * x2;
        float xy = q.X * y2;
        float xz = q.X * z2;
        float yy = q.Y * y2;
        float yz = q.Y * z2;
        float zz = q.Z * z2;
        float wx = q.W * x2;
        float wy = q.W * y2;
        float wz = q.W * z2;
        return vanguard::math::Matrix(vanguard::math::Vector4((1.0f - (yy + zz)) * Scale.X, (xy + wz) * Scale.X, (xz - wy) * Scale.X, 0.0f),
                                      vanguard::math::Vector4((xy - wz) * Scale.Y, (1.0f - (xx + zz)) * Scale.Y, (yz + wx) * Scale.Y, 0.0f),
                                      vanguard::math::Vector4((xz + wy) * Scale.Z, (yz - wx) * Scale.Z, (1.0f - (xx + yy)) * Scale.Z, 0.0f),
                                      vanguard::math::Vector4(Translation.X, Translation.Y, Translation.Z, 1.0f));
    }

    void QsTransform::Inverted(Quad& resultTranslation, Quad& resultRotation, Quad& resultScale) const
    {
        RED_MATH_PARANOID_SANITY_CHECK(MAbs(Rotation.MagnitudeSq() - 1.f) <= 0.00001f);

        resultRotation = QuadHelper::Conjugate(Rotation.vec.vec);
        const Quad negatedTranslation = QuadHelper::Negate(Translation.V);
        resultTranslation = QuadHelper::RotateDirection(resultRotation, negatedTranslation);
        resultScale = _mm_div_ps(_mm_set1_ps(1.f), Scale.V);
    }

    void QsTransform::InvertedUnsafe(Quad& resultTranslation, Quad& resultRotation, Quad& resultScale) const
    {
        RED_MATH_PARANOID_SANITY_CHECK(MAbs(Rotation.MagnitudeSq() - 1.f) <= 0.00001f);

        resultRotation = QuadHelper::Conjugate(Rotation.vec.vec);
        const Quad negatedTranslation = QuadHelper::Negate(Translation.V);
        resultTranslation = QuadHelper::RotateDirectionUnsafe(resultRotation, negatedTranslation);
        resultScale = _mm_div_ps(_mm_set1_ps(1.f), Scale.V);
    }

    void QsTransform::SetInverse(const QsTransform& _t)
    {
        _t.Inverted(Translation.V, Rotation.vec.vec, Scale.V);
        RED_MATH_PARANOID_SANITY_CHECK(IsOk());
    }

    void QsTransform::SetInverseUnsafe(const QsTransform& _t)
    {
        _t.InvertedUnsafe(Translation.V, Rotation.vec.vec, Scale.V);
        RED_MATH_PARANOID_SANITY_CHECK(IsOk());
    }

    void QsTransform::SetMul(const QsTransform& _a, const QsTransform& _b)
    {
        RED_MATH_PARANOID_SANITY_CHECK(MAbs(_a.Rotation.MagnitudeSq() - 1.f) <= 0.00001f);
        SetMul(_a.Translation.V, _a.Rotation.vec.vec, _a.Scale.V, _b.Translation.V, _b.Rotation.vec.vec, _b.Scale.V);
        RED_MATH_PARANOID_SANITY_CHECK(IsOk());
    }

    void QsTransform::SetMulUnsafe(const QsTransform& _a, const QsTransform& _b)
    {
        RED_MATH_PARANOID_SANITY_CHECK(MAbs(_a.Rotation.MagnitudeSq() - 1.f) <= 0.00001f);
        SetMulUnsafe(_a.Translation.V, _a.Rotation.vec.vec, _a.Scale.V, _b.Translation.V, _b.Rotation.vec.vec, _b.Scale.V);
        RED_MATH_PARANOID_SANITY_CHECK(IsOk());
    }

    void QsTransform::SetMul(Quad _aTranslation, Quad _aRotation, Quad _aScale, Quad _bTranslation, Quad _bRotation, Quad _bScale)
    {
        Translation.V = QuadHelper::TransformVector(_aTranslation, _aRotation, _aScale, _bTranslation);
        Rotation.vec.vec = QuadHelper::QuaternionMultiplication(_aRotation, _bRotation);
        Scale.V = _mm_mul_ps(_aScale, _bScale);
    }

    void QsTransform::SetMulUnsafe(Quad _aTranslation, Quad _aRotation, Quad _aScale, Quad _bTranslation, Quad _bRotation, Quad _bScale)
    {
        Translation.V = QuadHelper::TransformVectorUnsafe(_aTranslation, _aRotation, _aScale, _bTranslation);
        Rotation.vec.vec = QuadHelper::QuaternionMultiplication(_aRotation, _bRotation);
        Scale.V = _mm_mul_ps(_aScale, _bScale);
    }

    void QsTransform::SetMulInverseMul(const QsTransform& _a, const QsTransform& _b)
    {
        Quad invertedTranslation, invertedRotation, invertedScale;
        _a.Inverted(invertedTranslation, invertedRotation, invertedScale);
        SetMul(invertedTranslation, invertedRotation, invertedScale, _b.Translation.V, _b.Rotation.vec.vec, _b.Scale.V);
    }

    void QsTransform::SetMulInverseMulUnsafe(const QsTransform& _a, const QsTransform& _b)
    {
        Quad invertedTranslation, invertedRotation, invertedScale;
        _a.InvertedUnsafe(invertedTranslation, invertedRotation, invertedScale);
        SetMulUnsafe(invertedTranslation, invertedRotation, invertedScale, _b.Translation.V, _b.Rotation.vec.vec, _b.Scale.V);
    }

    void QsTransform::SetMulMulInverse(const QsTransform& _a, const QsTransform& _b)
    {
        Quad invertedTranslation, invertedRotation, invertedScale;
        _b.Inverted(invertedTranslation, invertedRotation, invertedScale);
        SetMul(_a.Translation.V, _a.Rotation.vec.vec, _a.Scale.V, invertedTranslation, invertedRotation, invertedScale);
    }

    void QsTransform::SetMulMulInverseUnsafe(const QsTransform& _a, const QsTransform& _b)
    {
        Quad invertedTranslation, invertedRotation, invertedScale;
        _b.InvertedUnsafe(invertedTranslation, invertedRotation, invertedScale);
        SetMulUnsafe(_a.Translation.V, _a.Rotation.vec.vec, _a.Scale.V, invertedTranslation, invertedRotation, invertedScale);
    }

    void QsTransform::SetMulEq(const QsTransform& _t)
    {
        // Rotation and position go together
        Vector4 extra;
        Vector4 scaledTrans = Mul(Translation, _t.Scale);
        vanguard::math::simd::SetMul(Scale, _t.Scale);
        extra.RotateDirection(Rotation, scaledTrans);
        SetAdd(Translation, extra);
        Rotation = Rotation * _t.Rotation;

        // Scale goes apart
        vanguard::math::simd::SetMul(Scale, _t.Scale);
        RED_MATH_PARANOID_SANITY_CHECK(IsOk());
    }

    bool QsTransform::IsOk() const
    {
        return (Translation.IsOk() && Rotation.IsOk() && Scale.IsOk() && Scale.W != 0.f);
    }

    bool QsTransform::IsAlmostEqual(const QsTransform& _other, float _epsilon) const
    {
        return Translation.IsAlmostEqual(_other.Translation, _epsilon) && Rotation.IsAlmostEqual(_other.Rotation, _epsilon) &&
               Scale.IsAlmostEqual(_other.Scale, _epsilon);
    }

    void QsTransform::BlendAddMul(const QsTransform& _other, float _weight)
    {
        Scalar weight(_weight);
        SetAdd(Translation, Mul(_other.Translation, weight));
        SetAdd(Scale, Mul(_other.Scale, weight));

        const float signedWeight = (Rotation.Dot(_other.Rotation) < 0.0f) ? -_weight : _weight;

        Rotation = (Rotation + (_other.Rotation * signedWeight));
        RED_MATH_PARANOID_SANITY_CHECK(IsOk());
    }

    void QsTransform::BlendNormalize(float _totalWeight)
    {
        if (vanguard::math::Abs(_totalWeight) < FLT_EPSILON)
        {
            SetIdentity();
            return;
        }

        {
            const Scalar invWeight(1.0f / _totalWeight);

            vanguard::math::simd::SetMul(Translation, invWeight);
            vanguard::math::simd::SetMul(Scale, invWeight);
        }

        {
            const float length = Rotation.MagnitudeSq();
            if (length < FLT_EPSILON)
            {
                Rotation.SetIdentity();
            }
            else
            {
                Rotation.Normalize();
            }
        }

        {
            const float scaleNorm = Scale.SquareLength3();
            if (scaleNorm < FLT_EPSILON)
            {
                Scale.SetOnes();
            }
        }
        RED_MATH_PARANOID_SANITY_CHECK(IsOk());
    }

    void QsTransform::FastRenormalize(float _totalWeight)
    {
        const Scalar invWeight(1.0f / _totalWeight);

        vanguard::math::simd::SetMul(Translation, invWeight);
        vanguard::math::simd::SetMul(Scale, invWeight);
        Rotation.Normalize();
        RED_MATH_PARANOID_SANITY_CHECK(IsOk());
    }

    //
    void QsTransform::FastRenormalizeBatch(QsTransform* _poseOut, float* _weight, unsigned int _numTransforms)
    {
        for (unsigned int i = 0; i < _numTransforms; ++i)
        {
            const Scalar invWeight(1.0f / _weight[i]);
            vanguard::math::simd::SetMul(_poseOut[i].Translation, invWeight);
            vanguard::math::simd::SetMul(_poseOut[i].Scale, invWeight);
        }

        // now normalize 4 quaternions at once
        QsTransform* blockStart = _poseOut;
        unsigned int numTransformsOver4 = _numTransforms / 4;
        for (unsigned int i = 0; i < numTransformsOver4; ++i)
        {
            Vector4 dots;
            dots.X = blockStart[0].Rotation.Dot(blockStart[0].Rotation);
            dots.Y = blockStart[1].Rotation.Dot(blockStart[1].Rotation);
            dots.Z = blockStart[2].Rotation.Dot(blockStart[2].Rotation);
            dots.W = blockStart[3].Rotation.Dot(blockStart[3].Rotation);

            Vector4 inverseSqrtDots;
            inverseSqrtDots.X = MRsqrt(dots.X);
            inverseSqrtDots.Y = MRsqrt(dots.Y);
            inverseSqrtDots.Z = MRsqrt(dots.Z);
            inverseSqrtDots.W = MRsqrt(dots.W);

            blockStart[0].Rotation = blockStart[0].Rotation * inverseSqrtDots.X;
            blockStart[1].Rotation = blockStart[1].Rotation * inverseSqrtDots.Y;
            blockStart[2].Rotation = blockStart[2].Rotation * inverseSqrtDots.Z;
            blockStart[3].Rotation = blockStart[3].Rotation * inverseSqrtDots.W;

            RED_MATH_PARANOID_SANITY_CHECK(blockStart[0].IsOk());
            RED_MATH_PARANOID_SANITY_CHECK(blockStart[1].IsOk());
            RED_MATH_PARANOID_SANITY_CHECK(blockStart[2].IsOk());
            RED_MATH_PARANOID_SANITY_CHECK(blockStart[3].IsOk());

            blockStart += 4;
        }

        unsigned int remainders = _numTransforms % 4;
        for (unsigned int i = 0; i < remainders; ++i)
        {
            blockStart[i].Rotation.Normalize();
        }
    }

    RED_INLINE QsTransform QsTransform::IDENTITY()
    {
        return {Vector4::ZERO_3D_POINT(), vanguard::math::Quaternion::IDENTITY(), Vector4::ONES()};
    }
} // namespace vanguard::math::simd
