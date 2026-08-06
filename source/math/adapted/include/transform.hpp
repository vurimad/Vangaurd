#pragma once

namespace vanguard::math
{
    RED_FORCE_INLINE Transform::Transform() : m_position{Vector4::ZEROS()}, m_orientation{Quaternion::IDENTITY()} {}

    RED_FORCE_INLINE Transform::Transform(EIdentity) : m_position{Vector4::ZEROS()}, m_orientation{Quaternion::IDENTITY()} {}

    RED_FORCE_INLINE Transform& Transform::operator=(const Transform& xform)
    {
        m_position = xform.m_position;
        m_orientation = xform.m_orientation;
        return *this;
    }

    RED_FORCE_INLINE Bool Transform::operator==(const Transform& xform) const
    {
        return m_position == xform.m_position && m_orientation == xform.m_orientation;
    }

    RED_FORCE_INLINE Bool Transform::operator!=(const Transform& xform) const
    {
        return !operator==(xform);
    }

    RED_INLINE Bool Transform::operator<(const Transform& xform) const
    {
        if (m_position == xform.m_position)
        {
            return m_orientation.AsVector() < xform.m_orientation.AsVector();
        }
        return m_position < xform.m_position;
    }

    RED_FORCE_INLINE Bool Transform::IsOk() const
    {
        return m_position.IsOk() && m_orientation.IsOk();
    }

    RED_INLINE Transform::Transform(const Vector3& position, const Quaternion& orientation)
        : m_position(position.X, position.Y, position.Z, 0.f), m_orientation(orientation.Normalized())
    {
    }

    RED_INLINE Transform::Transform(const Vector4& position, const Quaternion& orientation)
        : m_position(position.X, position.Y, position.Z, 0.f), m_orientation(orientation.Normalized())
    {
    }

    RED_INLINE Transform::Transform(const Quaternion& q) : m_position(Vector4::ZEROS()), m_orientation(q.Normalized()) {}

    RED_INLINE Transform::Transform(const Transform& xform) : m_position(xform.m_position), m_orientation(xform.m_orientation) {}

    RED_INLINE Vector4 Transform::operator*(const Vector4& v) const
    {
        RED_ASSERT(v.W == 0.f || v.W == 1.f, "Error: Vector W component doesn't contain 1 or 0 could be a bug");
        return Vector4(m_orientation.TransformUnsafe(v), v.W) + m_position * v.W;
    }

    RED_INLINE Vector3 Transform::operator*(const Vector3& v) const
    {
        return m_orientation.TransformUnsafe(v) + m_position;
    }

    RED_INLINE Transform Transform::operator*(const Transform& xform) const
    {
        const auto orientation = (xform.m_orientation * m_orientation).Normalized();
        const auto position = xform.m_orientation.Transform(m_position) + xform.m_position;

        return {position, orientation};
    }

    RED_INLINE Vector3 Transform::TransformPoint(const Vector3& v) const
    {
        return m_orientation.TransformUnsafe(v) + m_position;
    }

    RED_INLINE Vector3 Transform::TransformInvPoint(const Vector3& v) const
    {
        return m_orientation.TransformInverse(v - m_position);
    }

    RED_INLINE Vector4 Transform::TransformPoint(const Vector4& v) const
    {
        const auto result = m_orientation.TransformUnsafe(v) + m_position;
        return {result.X, result.Y, result.Z, 1.f};
    }

    RED_INLINE Vector3 Transform::TransformVector(const Vector3& v) const
    {
        return m_orientation.TransformUnsafe(v);
    }

    RED_INLINE Vector4 Transform::TransformVector(const Vector4& v) const
    {
        const auto result = m_orientation.TransformUnsafe(v);
        return {result.X, result.Y, result.Z, 0.f};
    }

    RED_INLINE Box Transform::TransformBox(const Box& box) const
    {
        if (box.IsEmpty())
        {
            return Box::EMPTY();
        }

        const Vector3 axisX = m_orientation.GetXAxis3();
        const Vector3 axisY = m_orientation.GetYAxis3();
        const Vector3 axisZ = m_orientation.GetZAxis3();

        const Vector3 xa = axisX * box.Min.X;
        const Vector3 xb = axisX * box.Max.X;
        const Vector3 ya = axisY * box.Min.Y;
        const Vector3 yb = axisY * box.Max.Y;
        const Vector3 za = axisZ * box.Min.Z;
        const Vector3 zb = axisZ * box.Max.Z;

        return Box(Vector3::Min(xa, xb) + Vector3::Min(ya, yb) + Vector3::Min(za, zb) + m_position,
                   Vector3::Max(xa, xb) + Vector3::Max(ya, yb) + Vector3::Max(za, zb) + m_position);
    }

    RED_INLINE EulerAngles Transform::ToEulerAngles() const
    {
        return m_orientation.ToEulerAngles();
    }

    RED_INLINE Matrix Transform::ToMatrix() const
    {
        Matrix m = m_orientation.ToMatrix();
        m.SetTranslation(m_position);
        return m;
    }

    RED_INLINE Matrix Transform::ToInvMatrix() const
    {
        return GetInverse().ToMatrix();
    }

    RED_INLINE Vector3 Transform::GetForward() const
    {
        return m_orientation.GetYAxis3();
    }

    RED_INLINE Vector3 Transform::GetRight() const
    {
        return m_orientation.GetXAxis3();
    }

    RED_INLINE Vector3 Transform::GetUp() const
    {
        return m_orientation.GetZAxis3();
    }

    RED_INLINE Float Transform::GetPitch() const
    {
        return m_orientation.GetPitch();
    }

    RED_INLINE Float Transform::GetYaw() const
    {
        return m_orientation.GetYaw();
    }

    RED_INLINE Float Transform::GetRoll() const
    {
        return m_orientation.GetRoll();
    }

    RED_INLINE void Transform::SetIdentity()
    {
        m_position.SetZeros();
        m_orientation.SetIdentity();
    }

    RED_INLINE void Transform::SetInverse()
    {
        m_orientation.SetConjugate();
        m_position = m_orientation.Transform(-m_position);
    }

    RED_INLINE Transform Transform::GetInverse() const
    {
        const auto conjugate = m_orientation.Conjugate();
        return {conjugate.Transform(-m_position), conjugate};
    }

    RED_INLINE Transform& Transform::Invert()
    {
        SetInverse();
        return *this;
    }

    RED_INLINE const Vector4& Transform::GetPosition() const
    {
        return m_position;
    }

    RED_INLINE const Vector3& Transform::GetPosition3() const
    {
        return m_position.AsVector3();
    }

    RED_INLINE const Quaternion& Transform::GetOrientation() const
    {
        return m_orientation;
    }

    RED_INLINE void Transform::SetPosition(const Vector3& v)
    {
        m_position = v;
    }

    RED_INLINE void Transform::SetPosition(const Vector4& v)
    {
        m_position = Vector4(v.X, v.Y, v.Z, 0.f);
    }

    RED_INLINE void Transform::SetOrientation(const Quaternion& q)
    {
        m_orientation = q.Normalized();
    }

    RED_INLINE void Transform::SetOrientation(const EulerAngles& e)
    {
        m_orientation = e.ToQuat();
    }

    RED_INLINE void Transform::SetOrientation(const Vector3& direction)
    {
        m_orientation.BuildFromDirectionVector(Vector4{direction});
        m_orientation.Normalize();
    }

    RED_INLINE void Transform::SetOrientation(const Vector4& direction)
    {
        m_orientation.BuildFromDirectionVector(direction);
        m_orientation.Normalize();
    }

    RED_INLINE void Transform::Set(const Vector3& position, const Quaternion& orientation)
    {
        SetPosition(position);
        SetOrientation(orientation);
    }

    RED_INLINE void Transform::Set(const Vector3& position, const EulerAngles& orientation)
    {
        SetPosition(position);
        SetOrientation(orientation);
    }

    RED_INLINE void Transform::Set(const Vector3& position, const Vector4& direction)
    {
        SetPosition(position);
        SetOrientation(direction);
    }

    RED_INLINE void Transform::Set(const Vector4& position, const Quaternion& orientation)
    {
        SetPosition(position);
        SetOrientation(orientation);
    }

    RED_INLINE void Transform::Set(const Vector4& position, const EulerAngles& orientation)
    {
        SetPosition(position);
        SetOrientation(orientation);
    }

    RED_INLINE void Transform::Set(const Vector4& position, const Vector4& direction)
    {
        SetPosition(position);
        SetOrientation(direction);
    }

    RED_FORCE_INLINE Transform Transform::IDENTITY()
    {
        return {EIDENTITY};
    }
} // namespace vanguard::math