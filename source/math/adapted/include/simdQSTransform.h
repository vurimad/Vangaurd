/**
 * Copyright (c) 2013 CD Projekt Red. All Rights Reserved.
 */

#pragma once

#include "simdVectorFunctions.h"

namespace vanguard::math::simd
{
    namespace QuadHelper
    {
        RED_INLINE Quad Negate(Quad v);
        RED_INLINE Quad Dot(Quad _a, Quad _b);
        RED_INLINE Quad Cross(Quad _a, Quad _b);
        RED_INLINE Quad RotateDirectionUnsafe(Quad quat, Quad vec);
        RED_INLINE Quad Normalize4(Quad v);
        RED_INLINE Quad RotateDirection(Quad quat, Quad vec);
        RED_INLINE Quad QuaternionMultiplication(Quad xyzw, Quad abcd);
        RED_INLINE Quad QuaternionLerpWithoutShortestPathCheck(Quad q1, Quad q2, Float t);
        RED_INLINE Quad QuaternionLerp(Quad q1, Quad q2, Float t);
        RED_INLINE Quad QuaternionSlerpWithoutShortestPathCheck(Quad q1, Quad q2, Float t, Quad cosTheta, Float tolerance);
        RED_INLINE Quad QuaternionSlerp(Quad q1, Quad q2, Float t, Float tolerance = 0.99f);
        RED_INLINE Quad TransformVector(Quad translation, Quad rotation, Quad scale, Quad vectorToTransform);
        RED_INLINE Quad TransformVectorUnsafe(Quad translation, Quad rotation, Quad scale, Quad vectorToTransform);
        RED_INLINE Quad Conjugate(Quad q);
    } // namespace QuadHelper

    class QsTransform
    {
    public:
        Vector4 Translation;
        vanguard::math::Quaternion Rotation;
        Vector4 Scale;

        // Constructors
        RED_INLINE QsTransform();
        RED_INLINE QsTransform(const QsTransform& _other);
        RED_INLINE QsTransform(const Vector4& _translation, const vanguard::math::Quaternion& _rotation, const Vector4& _scale);
        RED_INLINE QsTransform(const Vector4& _translation, const vanguard::math::Quaternion& _rotation);

        // Operators
        RED_INLINE void operator=(const QsTransform& _other);

        RED_INLINE void Set(const Vector4& _translation, const vanguard::math::Quaternion& _rotation, const Vector4& _scale);
        RED_INLINE void Set(const vanguard::math::Matrix& _m);

        RED_INLINE void SetIdentity();
        RED_INLINE void SetZero();

        RED_INLINE void SetTranslation(const Vector4& _v);
        RED_INLINE void SetRotation(const vanguard::math::Quaternion& _r);
        RED_INLINE void SetScale(const Vector4& _s);

        RED_INLINE const Vector4& GetTranslation() const;
        RED_INLINE const vanguard::math::Quaternion& GetRotation() const;
        RED_INLINE const Vector4& GetScale() const;

        RED_INLINE void Lerp(const QsTransform& _a, const QsTransform& _b, float _t);
        RED_INLINE void Slerp(const QsTransform& _a, const QsTransform& _b, float _t);

        RED_INLINE vanguard::math::Matrix ConvertToMatrix() const;
        RED_INLINE void ConvertToMatrix(vanguard::math::Matrix& m) const;
        RED_INLINE void ConvertToMatrixFast(vanguard::math::Matrix& m) const;
        RED_INLINE vanguard::math::Matrix ConvertToMatrixNormalized() const;

        RED_INLINE void Inverted(Quad& resultTranslation, Quad& resultRotation, Quad& resultScale) const;
        RED_INLINE void InvertedUnsafe(Quad& resultTranslation, Quad& resultRotation, Quad& resultScale) const;

        RED_INLINE void SetInverse(const QsTransform& _t);
        RED_INLINE void SetInverseUnsafe(const QsTransform& _t);
        RED_INLINE void SetMul(const QsTransform& _a, const QsTransform& _b);
        RED_INLINE void SetMulUnsafe(const QsTransform& _a, const QsTransform& _b);
        RED_INLINE void SetMul(Quad _aTranslation, Quad _aRotation, Quad _aScale, Quad _bTranslation, Quad _bRotation, Quad _bScale);
        RED_INLINE void SetMulUnsafe(Quad _aTranslation, Quad _aRotation, Quad _aScale, Quad _bTranslation, Quad _bRotation, Quad _bScale);
        RED_INLINE void SetMulInverseMul(const QsTransform& _a, const QsTransform& _b);
        RED_INLINE void SetMulInverseMulUnsafe(const QsTransform& _a, const QsTransform& _b);
        RED_INLINE void SetMulMulInverse(const QsTransform& _a, const QsTransform& _b);
        RED_INLINE void SetMulMulInverseUnsafe(const QsTransform& _a, const QsTransform& _b);
        RED_INLINE void SetMulEq(const QsTransform& _t);
        RED_INLINE bool IsOk() const;
        RED_INLINE bool IsAlmostEqual(const QsTransform& _other, float _epsilon = FLT_EPSILON) const;

        RED_INLINE void BlendAddMul(const QsTransform& _other, float _weight = 1.0f);
        RED_INLINE void BlendNormalize(float _totalWeight = 1.0f);
        RED_INLINE void FastRenormalize(float _totalWeight = 1.0f);
        static RED_INLINE void FastRenormalizeBatch(QsTransform* _poseOut, float* _weight, unsigned int _numTransforms);

        RED_INLINE static QsTransform IDENTITY();
    };

} // namespace vanguard::math::simd

#include "simdQSTransform.hpp"