#include "build.h"
#include "simdQSTransform.h"

namespace vanguard::math
{
    const Matrix Matrix::IDENTITY_CONSTANT(Matrix::IDENTITY());

    Float Matrix::Det() const
    {
        Float det = 0.0f;
        det += X[0] * CoFactor(0, 0);
        det += X[1] * CoFactor(0, 1);
        det += X[2] * CoFactor(0, 2);
        det += X[3] * CoFactor(0, 3);
        return det;
    }

    Float Matrix::CoFactor(Int32 i, Int32 j) const
    {
#define M(dx, dy) operator[]((i + dx) & 3)[(j + dy) & 3]
        Float val = 0.0f;
        val += M(1, 1) * M(2, 2) * M(3, 3);
        val += M(1, 2) * M(2, 3) * M(3, 1);
        val += M(1, 3) * M(2, 1) * M(3, 2);
        val -= M(3, 1) * M(2, 2) * M(1, 3);
        val -= M(3, 2) * M(2, 3) * M(1, 1);
        val -= M(3, 3) * M(2, 1) * M(1, 2);
        val *= ((i + j) & 1) ? -1.0f : 1.0f;
        return val;
#undef M
    }

    Matrix Matrix::Transposed() const
    {
        __m128 r0 = X;
        __m128 r1 = Y;
        __m128 r2 = Z;
        __m128 r3 = W;

        _MM_TRANSPOSE4_PS(r0, r1, r2, r3);

        return {r0, r1, r2, r3};
    }

    // below algorithms taken from:
    // https://lxjk.github.io/2017/09/03/Fast-4x4-Matrix-Inverse-with-SSE-SIMD-Explained.html

#define MakeShuffleMask(x, y, z, w) (x | (y << 2) | (z << 4) | (w << 6))
#define VecSwizzleMask(vec, mask) _mm_shuffle_ps(vec, vec, mask)
#define VecSwizzle(vec, x, y, z, w) VecSwizzleMask(vec, MakeShuffleMask(x, y, z, w))
#define VecSwizzle1(vec, x) VecSwizzleMask(vec, MakeShuffleMask(x, x, x, x))
#define VecSwizzle_0022(vec) _mm_moveldup_ps(vec)
#define VecSwizzle_1133(vec) _mm_movehdup_ps(vec)
#define VecShuffle(vec1, vec2, x, y, z, w) _mm_shuffle_ps(vec1, vec2, MakeShuffleMask(x, y, z, w))
#define VecShuffle_0101(vec1, vec2) _mm_movelh_ps(vec1, vec2)
#define VecShuffle_2323(vec1, vec2) _mm_movehl_ps(vec2, vec1)

    Matrix Matrix::OrthonormInverted() const
    {
        Matrix out;

        // Transpose the 3x3 inner matrix
        __m128 t0 = VecShuffle_0101(X, Y);
        __m128 t1 = VecShuffle_2323(X, Y);
        out.X = VecShuffle(t0, Z, 0, 2, 0, 3);
        out.Y = VecShuffle(t0, Z, 1, 3, 1, 3);
        out.Z = VecShuffle(t1, Z, 0, 2, 2, 3);

        // Calculate inverted translation
        out.W = _mm_mul_ps(out.X, VecSwizzle1(W, 0));
        out.W = _mm_add_ps(out.W, _mm_mul_ps(out.Y, VecSwizzle1(W, 1)));
        out.W = _mm_add_ps(out.W, _mm_mul_ps(out.Z, VecSwizzle1(W, 2)));
        out.W = _mm_sub_ps(_mm_setr_ps(0.0f, 0.0f, 0.0f, 1.0f), out.W);

        return out;
    }

    // 2x2 row major Matrix multiply A*B
    RED_FORCE_INLINE static __m128 Mat2Mul(__m128 vec1, __m128 vec2)
    {
        return _mm_add_ps(_mm_mul_ps(vec1, VecSwizzle(vec2, 0, 3, 0, 3)),
                          _mm_mul_ps(VecSwizzle(vec1, 1, 0, 3, 2), VecSwizzle(vec2, 2, 1, 2, 1)));
    }

    // 2x2 row major Matrix adjugate multiply (A#)*B
    RED_FORCE_INLINE static __m128 Mat2AdjMul(__m128 vec1, __m128 vec2)
    {
        return _mm_sub_ps(_mm_mul_ps(VecSwizzle(vec1, 3, 3, 0, 0), vec2),
                          _mm_mul_ps(VecSwizzle(vec1, 1, 1, 2, 2), VecSwizzle(vec2, 2, 3, 0, 1)));
    }
    // 2x2 row major Matrix multiply adjugate A*(B#)
    RED_FORCE_INLINE static __m128 Mat2MulAdj(__m128 vec1, __m128 vec2)
    {
        return _mm_sub_ps(_mm_mul_ps(vec1, VecSwizzle(vec2, 3, 0, 3, 0)),
                          _mm_mul_ps(VecSwizzle(vec1, 1, 0, 3, 2), VecSwizzle(vec2, 2, 1, 2, 1)));
    }

    Matrix Matrix::FullInverted() const
    {
        // use block matrix method
        // A is a matrix, then i(A) or iA means inverse of A, A# (or A_ in code) means adjugate of A, |A| (or detA in code) is determinant,
        // tr(A) is trace

        // sub matrices
        __m128 A = VecShuffle_0101(X, Y);
        __m128 B = VecShuffle_2323(X, Y);
        __m128 C = VecShuffle_0101(Z, W);
        __m128 D = VecShuffle_2323(Z, W);

        // determinant as (|A| |B| |C| |D|)
        __m128 detSub = _mm_sub_ps(_mm_mul_ps(VecShuffle(X, Z, 0, 2, 0, 2), VecShuffle(Y, W, 1, 3, 1, 3)),
                                   _mm_mul_ps(VecShuffle(X, Z, 1, 3, 1, 3), VecShuffle(Y, W, 0, 2, 0, 2)));
        __m128 detA = VecSwizzle1(detSub, 0);
        __m128 detB = VecSwizzle1(detSub, 1);
        __m128 detC = VecSwizzle1(detSub, 2);
        __m128 detD = VecSwizzle1(detSub, 3);

        // let iM = 1/|M| * | X  Y |
        //                  | Z  W |

        // D#C
        __m128 D_C = Mat2AdjMul(D, C);
        // A#B
        __m128 A_B = Mat2AdjMul(A, B);
        // X# = |D|A - B(D#C)
        __m128 X_ = _mm_sub_ps(_mm_mul_ps(detD, A), Mat2Mul(B, D_C));
        // W# = |A|D - C(A#B)
        __m128 W_ = _mm_sub_ps(_mm_mul_ps(detA, D), Mat2Mul(C, A_B));

        // |M| = |A|*|D| + ... (continue later)
        __m128 detM = _mm_mul_ps(detA, detD);

        // Y# = |B|C - D(A#B)#
        __m128 Y_ = _mm_sub_ps(_mm_mul_ps(detB, C), Mat2MulAdj(D, A_B));
        // Z# = |C|B - A(D#C)#
        __m128 Z_ = _mm_sub_ps(_mm_mul_ps(detC, B), Mat2MulAdj(A, D_C));

        // |M| = |A|*|D| + |B|*|C| ... (continue later)
        detM = _mm_add_ps(detM, _mm_mul_ps(detB, detC));

        // tr((A#B)(D#C))
        __m128 tr = _mm_mul_ps(A_B, VecSwizzle(D_C, 0, 2, 1, 3));
        tr = _mm_hadd_ps(tr, tr);
        tr = _mm_hadd_ps(tr, tr);
        // |M| = |A|*|D| + |B|*|C| - tr((A#B)(D#C)
        detM = _mm_sub_ps(detM, tr);

        const __m128 adjSignMask = _mm_setr_ps(1.0f, -1.0f, -1.0f, 1.0f);
        // (1/|M|, -1/|M|, -1/|M|, 1/|M|)
        __m128 rDetM = _mm_div_ps(adjSignMask, detM);

        X_ = _mm_mul_ps(X_, rDetM);
        Y_ = _mm_mul_ps(Y_, rDetM);
        Z_ = _mm_mul_ps(Z_, rDetM);
        W_ = _mm_mul_ps(W_, rDetM);

        // apply adjugate and store, here we combine adjugate shuffle and store shuffle
        Matrix r;
        r.X = VecShuffle(X_, Y_, 3, 1, 3, 1);
        r.Y = VecShuffle(X_, Y_, 2, 0, 2, 0);
        r.Z = VecShuffle(Z_, W_, 3, 1, 3, 1);
        r.W = VecShuffle(Z_, W_, 2, 0, 2, 0);

        return r;
    }

    Matrix Matrix::Mul(const Matrix& a, const Matrix& b)
    {
        return Matrix(b.TransformVectorWithW(a.X), b.TransformVectorWithW(a.Y), b.TransformVectorWithW(a.Z), b.TransformVectorWithW(a.W));
    }

    Box Matrix::TransformBox(const Box& box) const
    {
        if (box.IsEmpty())
        {
            return Box::EMPTY();
        }

        const Vector4 xa = X * box.Min.X;
        const Vector4 xb = X * box.Max.X;
        const Vector4 ya = Y * box.Min.Y;
        const Vector4 yb = Y * box.Max.Y;
        const Vector4 za = Z * box.Min.Z;
        const Vector4 zb = Z * box.Max.Z;

        return Box(Vector4::Min4(xa, xb) + Vector4::Min4(ya, yb) + Vector4::Min4(za, zb) + W,
                   Vector4::Max4(xa, xb) + Vector4::Max4(ya, yb) + Vector4::Max4(za, zb) + W);
    }

    Matrix& Matrix::BuildPerspectiveLH(Float fovy, Float aspect, Float zn, Float zf)
    {
        const Float ys = 1.0f / ::tanf(fovy * 0.5f);
        const Float xs = ys / aspect;
        const Float zs = zf / (zf - zn);

        X = Vector4(xs, 0.0f, 0.0f, 0.0f);
        Y = Vector4(0.0f, ys, 0.0f, 0.0f);
        Z = Vector4(0.0f, 0.0f, zs, 1.0f);
        W = Vector4(0.0f, 0.0f, -zn * zs, 0.0f);

        return (*this);
    }

    Matrix& Matrix::BuildPerspectiveOffCenterLH(Float zn, Float zf, Float zoomX, Float zoomY, Float l, Float r, Float b, Float t)
    {
        const Float xs = 2.0f * zn / (r - l);
        const Float ys = 2.0f * zn / (t - b);
        const Float zs = zf / (zf - zn);
        const Float o1 = (l + r) / (l - r);
        const Float o2 = (t + b) / (b - t);

        X = Vector4(xs * zoomX, 0.0f, 0.0f, 0.0f);
        Y = Vector4(0.0f, ys * zoomY, 0.0f, 0.0f);
        Z = Vector4(o1, o2, zs, 1.0f);
        W = Vector4(0.0f, 0.0f, -zn * zs, 0.0f);

        return (*this);
    }

    Matrix& Matrix::BuildOrthoLH(Float w, Float h, Float zn, Float zf)
    {
        X = Vector4(2.0f / w, 0.0f, 0.0f, 0.0f);
        Y = Vector4(0.0f, 2.0f / h, 0.0f, 0.0f);
        Z = Vector4(0.0f, 0.0f, 1.0f / (zf - zn), 0.0f);
        W = Vector4(0.0f, 0.0f, -zn / (zf - zn), 1.0f);

        return (*this);
    }

    Matrix& Matrix::BuildPerspectiveLH_Z(Float fovy, Float aspect, Float zn, Float zf)
    {
        X = Vector4(zn, 0.0f, 0.0f, 0.0f);
        Y = Vector4(0.0f, zn, 0.0f, 0.0f);
        Z = Vector4(0.0f, 0.0f, zn + zf, 1.0f);
        W = Vector4(0.0f, 0.0f, -zn * zf, 0.0f);

        return (*this);
    }

    Matrix& Matrix::BuildPerspectiveLH_XY(Vector2 nearPlaneSize, Float zn, Float zf)
    {
        Float l = -0.5f * nearPlaneSize.X;
        Float r = 0.5f * nearPlaneSize.X;
        Float b = -0.5f * nearPlaneSize.Y;
        Float t = 0.5f * nearPlaneSize.Y;

        X = Vector4(2.0f / (r - l), 0.0f, 0.0f, 0.0f);
        Y = Vector4(0.0f, 2.0f / (t - b), 0.0f, 0.0f);
        Z = Vector4(0.0f, 0.0f, 1.0f / (zf - zn), 0.0f);
        W = Vector4(-(r + l) / (r - l), -(t + b) / (t - b), -zn / (zf - zn), 1.0f);

        return (*this);
    }

    Matrix& Matrix::ModifyProjectionToOblique(Vector4& clippingPlane)
    {
        Vector4 q;
        Float* matrix = X.AsFloat();
        // Calculate the clip-space corner point opposite the clipping plane
        // using Equation (5.64) and transform it into camera space by
        // multiplying it by the inverse of the projection matrix.
        q.X = (vanguard::math::Sgn(clippingPlane.X) + matrix[8]) / matrix[0];
        q.Y = (vanguard::math::Sgn(clippingPlane.Y) + matrix[9]) / matrix[5];
        q.Z = -1.0F;
        q.W = (1.0F + matrix[10]) / matrix[14];

        // Calculate the scaled plane vector using Equation (5.68)
        // and replace the third row of the projection matrix.
        Vector4 c = clippingPlane * (1.0f / clippingPlane.Dot4(q));
        matrix[2] = c.X;
        matrix[6] = c.Y;
        matrix[10] = c.Z;
        matrix[14] = c.W;

        return (*this);
    }

    Matrix& Matrix::BuildFromDirectionVector(const Vector4& dirVec, const Vector4& upVec /*= Vector4::EZ()*/)
    {
        // EY from direction, EZ pointing up
        Y = dirVec.Normalized3();
        Z = upVec.Normalized3();

        // EX as cross product of EY and EZ
        X = Vector4::Cross(Y, Z);
        if (X.Normalize3() > 1e-8f) // experimental - smaller values get very innacurate directions
        {
            // EZ as cross product of EX and EY ( to orthogonalize );
            Z = Vector4::Cross(X, Y).Normalized3();
        }
        else
        {
            SetRotX33((dirVec.Z < 0.f) ? -RED_PI / 2.f : RED_PI / 2.f);
        }

        X.W = 0.0f;
        Y.W = 0.0f;
        Z.W = 0.0f;
        W = Vector4::EW();

        return (*this);
    }

    Matrix& Matrix::BuildFromQuaternion(const Quaternion& quaternion)
    {
        *this = quaternion.ToMatrix();
        return *this;
    }

    EulerAngles Matrix::ToEulerAngles() const
    {
        EulerAngles ret;

        if (GetRow(1)[2] > 0.995f)
        {
            ret.Roll = RAD2DEG(atan2f(GetRow(2)[0], GetRow(0)[0]));
            ret.Pitch = 90.0f;
            ret.Yaw = 0.0f;
        }
        else if (GetRow(1)[2] < -0.995f)
        {
            ret.Roll = RAD2DEG(atan2f(GetRow(2)[0], GetRow(0)[0]));
            ret.Pitch = -90.0f;
            ret.Yaw = 0.0f;
        }
        else
        {
            ret.Roll = -RAD2DEG(atan2f(GetRow(0)[2], GetRow(2)[2]));
            ret.Pitch = RAD2DEG((Float)asin(GetRow(1)[2]));
            ret.Yaw = -RAD2DEG(atan2f(GetRow(1)[0], GetRow(1)[1]));
        }

        return ret;
    }

    EulerAngles Matrix::ToEulerAnglesFullChecked(Bool& isValid) const
    {
        EulerAngles ret;

        Float row1Mag = GetRow(1).Mag3();

        const Float magEps = 0.00001f;
        if (row1Mag < magEps)
        {
            isValid = false;
            return EulerAngles::ZEROS();
        }

        Float row2MagSqr = GetRow(2).SquareMag3();
        if (row2MagSqr < (magEps * magEps))
        {
            isValid = false;
            return EulerAngles::ZEROS();
        }

        Float unscaleR1 = 1.f / row1Mag;
        Float rescaleR2toR0 = ::sqrtf((GetRow(0).SquareMag3() / row2MagSqr));

        Float cell12 = GetRow(1)[2] * unscaleR1;
        if (cell12 > 0.99999f)
        {
            ret.Roll = RAD2DEG(atan2f(GetRow(2)[0] * rescaleR2toR0, GetRow(0)[0]));
            ret.Pitch = 90.0f;
            ret.Yaw = 0.0f;
        }
        else if (cell12 < -0.99999f)
        {
            ret.Roll = RAD2DEG(atan2f(GetRow(2)[0] * rescaleR2toR0, GetRow(0)[0]));
            ret.Pitch = -90.0f;
            ret.Yaw = 0.0f;
        }
        else
        {
            ret.Roll = -RAD2DEG(atan2f(GetRow(0)[2], GetRow(2)[2] * rescaleR2toR0));
            ret.Pitch = RAD2DEG((Float)asin(cell12));
            ret.Yaw = -RAD2DEG(atan2f(GetRow(1)[0], GetRow(1)[1])); // unscaling is not needed, both arguments has same scale
        }

        isValid = true;
        return ret;
    }

    EulerAngles Matrix::ToEulerAnglesFull() const
    {
        Bool isValid = false;
        EulerAngles ret = ToEulerAnglesFullChecked(isValid);
        RED_ASSERT(isValid, "Unable to convert Matrix back to Euler angles");
        return ret;
    }

    Float Matrix::GetYaw() const
    {
        // This was originally here but fucked up the camera while going through streaming doors
        // Float a2 = MAbs( GetRow(1)[2] ) * 0.001f;
        // if ( a2 > MAbs( GetRow(1)[0] ) || a2 > MAbs( GetRow(1)[1] ) )
        //{
        //	return 0.f;
        //}

        const Vector4& forward = GetRow(1);
        return -RAD2DEG(atan2f(forward[0], forward[1])); // unscaling is not needed, both arguments has same scale
    }

    void Matrix::ExtractScale(Matrix& trMatrix, Vector4& scale) const
    {
        // Just copy the matrix
        trMatrix = *this;

        Vector4 xAxis = trMatrix.GetRow(0);
        Vector4 yAxis = trMatrix.GetRow(1);
        Vector4 zAxis = trMatrix.GetRow(2);

        // Extract scale and normalize axes
        scale = Vector4(xAxis.Normalize3(), yAxis.Normalize3(), zAxis.Normalize3());
        trMatrix.SetRows(xAxis, yAxis, zAxis, Vector4::EW());
    }

    void Matrix::ToAngleVectors(Vector4* forward, Vector4* right, Vector4* up) const
    {
        Vector4 f(0, 1, 0);
        Vector4 r(1, 0, 0);
        Vector4 u(0, 0, 1);

        if (forward)
        {
            *forward = TransformVector(f);
        }

        if (right)
        {
            *right = TransformVector(r);
        }

        if (up)
        {
            *up = TransformVector(u);
        }
    }

    Quaternion Matrix::ToQuat() const
    {
        const auto row_0 = X.Normalized3();
        const auto row_1 = Y.Normalized3();
        const auto row_2 = Z.Normalized3();

        Float tr = row_0[0] + row_1[1] + row_2[2];
        Float qw;
        Float qx;
        Float qy;
        Float qz;

        if (tr > 0.f)
        {
            float S = sqrtf(tr + 1.f) * 2.f; // S=4*qw
            qw = 0.25f * S;
            qx = (row_1[2] - row_2[1]) / S;
            qy = (row_2[0] - row_0[2]) / S;
            qz = (row_0[1] - row_1[0]) / S;
        }
        else if ((row_0[0] > row_1[1]) && (row_0[0] > row_2[2]))
        {
            float S = sqrtf(1.0f + row_0[0] - row_1[1] - row_2[2]) * 2.f; // S=4*qx
            qw = (row_1[2] - row_2[1]) / S;
            qx = 0.25f * S;
            qy = (row_1[0] + row_0[1]) / S;
            qz = (row_2[0] + row_0[2]) / S;
        }
        else if (row_1[1] > row_2[2])
        {
            float S = sqrtf(1.0f + row_1[1] - row_0[0] - row_2[2]) * 2.f; // S=4*qy
            qw = (row_2[0] - row_0[2]) / S;
            qx = (row_1[0] + row_0[1]) / S;
            qy = 0.25f * S;
            qz = (row_2[1] + row_1[2]) / S;
        }
        else
        {
            float S = sqrtf(1.0f + row_2[2] - row_0[0] - row_1[1]) * 2.f; // S=4*qz
            qw = (row_0[1] - row_1[0]) / S;
            qx = (row_2[0] + row_0[2]) / S;
            qy = (row_2[1] + row_1[2]) / S;
            qz = 0.25f * S;
        }

        return vanguard::math::Quaternion(qx, qy, qz, qw).Normalized();
    }

    Transform Matrix::ToXform() const
    {
        return {GetTranslation(), ToQuat()};
    }

    Bool Matrix::SLOW_Decompose(const Matrix& m, Vector4& translation, Matrix& rotation, Vector4& scale)
    {
        Matrix copy = m;

        translation = copy.GetTranslation();
        copy.SetColumn(3, Vector4::ZERO_3D_POINT());
        copy.SetRow(3, Vector4::ZERO_3D_POINT());

        Float norm;
        Int32 count = 0;
        rotation = copy;

        do
        {
            Matrix nextRotation;
            Matrix currInvTranspose = rotation.Transposed().FullInverted();

            for (int i = 0; i < 4; ++i)
            {
                for (int j = 0; j < 4; ++j)
                {
                    nextRotation[i][j] = (rotation[i][j] + currInvTranspose[i][j]) * 0.5f;
                }
            }

            norm = 0.f;
            for (int i = 0; i < 3; i++)
            {
                Float n = fabsf(rotation[i][0] - nextRotation[i][0]) + fabsf(rotation[i][1] - nextRotation[i][1]) +
                          fabsf(rotation[i][2] - nextRotation[i][2]);

                norm = Max(norm, n);
            }
            rotation = nextRotation;
            ++count;
        } while (count < 100 && norm > 1.0e-8);

        Matrix scaleMatrix = copy * rotation.Transposed();
        scale = Vector4(scaleMatrix.X[0], scaleMatrix.Y[1], scaleMatrix.Z[2], 0.f);

        Vector4 row1(copy.GetAxisX().Normalized3());
        Vector4 row2(copy.GetAxisY().Normalized3());
        Vector4 row3(copy.GetAxisZ().Normalized3());

        Matrix nRotation(row1, row2, row3, Vector4::ZERO_3D_POINT());

        Float determinant = nRotation.Det();
        if (determinant < 0.f)
        {
            return false;
        }

        return true;
    }

    Bool Matrix::SLOW_Decompose(const Matrix& m, Vector4& translation, Quaternion& rotation, Vector4& scale)
    {
        Matrix rot;
        if (Matrix::SLOW_Decompose(m, translation, rot, scale))
        {
            rotation = rot.ToQuat();
            return true;
        }

        return false;
    }

    Bool Matrix::SLOW_Decompose(const Matrix& m, Vector3& translation, Matrix& rotation, Vector3& scale)
    {
        Vector4 trans, scl;
        if (Matrix::SLOW_Decompose(m, trans, rotation, scl))
        {
            translation = trans;
            scale = scl;
            return true;
        }

        return false;
    }

    Bool Matrix::SLOW_Decompose(const Matrix& m, Vector3& translation, Quaternion& rotation, Vector3& scale)
    {
        Vector4 trans, scl;
        Matrix rot;
        if (Matrix::SLOW_Decompose(m, trans, rot, scl))
        {
            translation = trans;
            rotation = rot.ToQuat();
            scale = scl;
            return true;
        }

        return false;
    }

    Bool Matrix::SLOW_Decompose(const Matrix& m, Vector4& translation, Quaternion& rotation)
    {
        Vector4 unused;
        return Matrix::SLOW_Decompose(m, translation, rotation, unused);
    }

    Bool Matrix::SLOW_Decompose(const Matrix& m, Vector3& translation, Quaternion& rotation)
    {
        Vector3 unused;
        return Matrix::SLOW_Decompose(m, translation, rotation, unused);
    }

    Bool Matrix::SLOW_Decompose(const Matrix& m, vanguard::math::simd::QsTransform& xform)
    {
        ///
        ///	@todo [william.mcvicar] only have SIMD version of vector4 so we don't have to waste doing this conversion
        ///
        Vector4 t, s;
        if (Matrix::SLOW_Decompose(m, t, xform.Rotation, s))
        {
            xform.Translation.Set(t.X, t.Y, t.Z, t.W);
            xform.Scale.Set(s.X, s.Y, s.Z, s.W);
            return true;
        }

        return false;
    }

    Bool Matrix::SLOW_Decompose(const Matrix& m, Transform& xform)
    {
        Vector4 p;
        Quaternion q;
        const Bool result = Matrix::SLOW_Decompose(m, p, q);
        xform.Set(p, q);
        return result;
    }

} // namespace vanguard::math