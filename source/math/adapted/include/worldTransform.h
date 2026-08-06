/**
 * Copyright (c) 2017 CD Projekt Red. All Rights Reserved.
 */

#pragma once

#include "worldPosition.h"

namespace vanguard::math
{
    // ---
    struct WorldTransform
    {
        enum EIdentity
        {
            EIDENTITY
        };

        RED_INLINE WorldTransform();

        WorldTransform(EIdentity);
        WorldTransform(const WorldPosition& p);
        WorldTransform(const WorldTransform& xform);

        RED_INLINE WorldTransform(const WorldPosition& p, const Quaternion& q);
        RED_INLINE explicit WorldTransform(const Quaternion& q);
        RED_INLINE explicit WorldTransform(const Transform& xform);
        RED_INLINE explicit WorldTransform(const Vector3& p);
        RED_INLINE explicit WorldTransform(const Vector3& p, const Quaternion& q);

        RED_INLINE Bool operator==(const WorldTransform& v) const;
        RED_INLINE Bool operator!=(const WorldTransform& v) const;

        RED_INLINE void SetPosition(const WorldPosition& value);
        RED_INLINE void SetPosition(const Vector3& value);
        RED_INLINE void SetOrientation(const Quaternion& value);

        RED_INLINE const WorldPosition& GetPosition() const;
        RED_INLINE const Quaternion& GetOrientation() const;

        // Transforms concatenation.
        // In 'matrix notation':
        //   - column major -> this * xform
        //   - row major -> xform * this
        RED_INLINE WorldTransform TransformXForm(const Transform& xform) const;
        RED_INLINE WorldTransform TransformXForm(const WorldTransform& xform) const;

        // Transform point.
        // In 'matrix notation':
        //   - column major -> this * pt
        //   - row major -> pt * this
        RED_INLINE WorldPosition TransformPoint(const Vector3& pt) const;
        RED_INLINE WorldPosition TransformPoint(const Vector4& pt) const;
        RED_INLINE WorldPosition TransformPoint(const WorldPosition& pt) const;

        // Transforms xform/point to this transform space.
        // In 'matrix notation':
        //   - column major -> inverse( this ) * xform
        //   - row major -> xform * inverse( this )
        RED_INLINE Transform TransformInv(const WorldTransform& xform) const;
        RED_INLINE Transform TransformInv(const Transform& xform) const;
        RED_INLINE Vector3 TransformInvPoint(const WorldPosition& pt) const;

        RED_INLINE WorldTransform GetInverse() const;

        RED_INLINE Vector3 GetForward() const;
        RED_INLINE Vector3 GetRight() const;
        RED_INLINE Vector3 GetUp() const;

        RED_INLINE Box TransformBox(const Box& box) const;

        static WorldTransform IDENTITY();

        RED_INLINE Bool IsOk() const;

        RED_INLINE Matrix ToMatrix() const;
        RED_INLINE Matrix ToMatrixUnsafe() const;

        // Remember: in most cases simple conversion to Transform type is not right thing to do.
        // If you really need, then ok, but it probably will not solve your problem.
        RED_INLINE Transform _ToXForm() const;

    protected:
        WorldPosition m_position = WorldPosition::ZEROS();
        Quaternion m_orientation = Quaternion::IDENTITY();
    };

    RED_INLINE Float DistanceSquared(const WorldPosition& a, const WorldPosition& b);
    RED_INLINE Float DistanceSquared(const WorldPosition& a, const Vector3& b);
} // namespace vanguard::math

#include "worldTransform.inl"