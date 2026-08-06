/**
 * Copyright (c) 2026 RED Vanguard, All Rights Reserved.
 */

#pragma once

namespace vanguard::math
{
    RED_FORCE_INLINE Box::Box(const Box& rhs) : Box{rhs.Min, rhs.Max} {}

    RED_FORCE_INLINE Box::Box(const Vector4& min, const Vector4& max) : Min{min}, Max{max} {}

    RED_FORCE_INLINE Box::Box(const Vector4& center, float radius)
        : Min{center - Vector4{radius, radius, radius, 0}}, Max{center + Vector4{radius, radius, radius, 0}}
    {
    }

    RED_FORCE_INLINE Box::Box(EResetState) : Min{Vector4::PLUS_MAX()}, Max{Vector4::MINUS_MAX()} {}

    RED_FORCE_INLINE Box::Box(EMaximize) : Min{Vector4::MINUS_MAX()}, Max{Vector4::PLUS_MAX()} {}

    RED_INLINE Bool Box::operator==(const Box& box) const
    {
        return Vector4::Equal3(Min, box.Min) && Vector4::Equal3(Max, box.Max);
    }

    RED_INLINE Bool Box::operator!=(const Box& box) const
    {
        return !(Vector4::Equal3(Min, box.Min) && Vector4::Equal3(Max, box.Max));
    }

    RED_INLINE Bool Box::operator<(const Box& box) const
    {
        return Min < box.Min || (Min == box.Min && Max < box.Max);
    }

    RED_FORCE_INLINE Box Box::operator+(const Vector4& dir) const
    {
        return {Min + dir, Max + dir};
    }

    RED_FORCE_INLINE Box Box::operator-(const Vector4& dir) const
    {
        return {Min - dir, Max - dir};
    }

    RED_FORCE_INLINE Box Box::operator*(const Vector4& scale) const
    {
        return {Min * scale, Max * scale};
    }

    RED_FORCE_INLINE void Box::operator+=(const Vector4& dir)
    {
        Min += dir;
        Max += dir;
    }

    RED_FORCE_INLINE void Box::operator-=(const Vector4& dir)
    {
        Min -= dir;
        Max -= dir;
    }

    RED_FORCE_INLINE void Box::operator*=(const Vector4& scale)
    {
        Min *= scale;
        Max *= scale;
    }

    RED_FORCE_INLINE Box& Box::Clear()
    {
        Min = Vector4::PLUS_MAX();
        Max = Vector4::MINUS_MAX();
        return *this;
    }

    RED_INLINE Bool Box::Contains(const Vector4& point) const
    {
        return point.X >= Min.X && point.Y >= Min.Y && point.Z >= Min.Z && point.X <= Max.X && point.Y <= Max.Y && point.Z <= Max.Z;
    }

    RED_INLINE Bool Box::Contains2D(const Vector3& point) const
    {
        return point.X >= Min.X && point.Y >= Min.Y && point.X <= Max.X && point.Y <= Max.Y;
    }

    RED_INLINE Bool Box::Contains(const Vector3& point, Float zExt) const
    {
        const Float step = Max.Z - Min.Z;
        Vector4 v = point;

        if (step > 0.f)
        {
            for (Float z = 0.f; z <= zExt; z += step)
            {
                v.Z += z;
                if (Contains(v))
                {
                    return true;
                }
            }
        }
        else if (zExt > 0.f)
        {
            if (Contains(v))
            {
                return true;
            }
        }

        v.Z = point.Z + zExt;
        return Contains(v);
    }

    RED_INLINE Bool Box::Contains(const Box& box) const
    {
        return box.Min.X >= Min.X && box.Min.Y >= Min.Y && box.Min.Z >= Min.Z && box.Max.X <= Max.X && box.Max.Y <= Max.Y &&
               box.Max.Z <= Max.Z;
    }

    RED_INLINE Bool Box::ContainsExcludeEdges(const Box& box) const
    {
        return box.Min.X > Min.X && box.Min.Y > Min.Y && box.Min.Z > Min.Z && box.Max.X < Max.X && box.Max.Y < Max.Y && box.Max.Z < Max.Z;
    }

    RED_INLINE Bool Box::Contains2D(const Box& box) const
    {
        return box.Min.X >= Min.X && box.Min.Y >= Min.Y && box.Max.X <= Max.X && box.Max.Y <= Max.Y;
    }

    RED_INLINE Bool Box::ContainsExcludeEdges(const Vector4& point) const
    {
        if (point.X > Min.X && point.Y > Min.Y && point.Z > Min.Z && point.X < Max.X && point.Y < Max.Y && point.Z < Max.Z)
        {
            return true;
        }

        return false;
    }

    RED_INLINE Bool Box::Touches(const Box& box) const
    {
        return box.Max.X >= Min.X && box.Max.Y >= Min.Y && box.Max.Z >= Min.Z && box.Min.X <= Max.X && box.Min.Y <= Max.Y &&
               box.Min.Z <= Max.Z;
    }

    RED_INLINE Bool Box::Touches(const Vector3& bMin, const Vector3& bMax) const
    {
        return bMax.X >= Min.X && bMax.Y >= Min.Y && bMax.Z >= Min.Z && bMin.X <= Max.X && bMin.Y <= Max.Y && bMin.Z <= Max.Z;
    }

    RED_INLINE Bool Box::Touches2D(const Box& box) const
    {
        return box.Max.X >= Min.X && box.Max.Y >= Min.Y && box.Min.X <= Max.X && box.Min.Y <= Max.Y;
    }

    RED_INLINE Box& Box::AddPoint(const Vector4& point)
    {
        Min = Vector4::Min4(Min, point);
        Max = Vector4::Max4(Max, point);
        return *this;
    }

    RED_INLINE Box& Box::AddPoint(const Vector3& point)
    {
        Min.X = vanguard::math::Min(Min.X, point.X);
        Min.Y = vanguard::math::Min(Min.Y, point.Y);
        Min.Z = vanguard::math::Min(Min.Z, point.Z);
        Min.W = 1.f;

        Max.X = vanguard::math::Max(Max.X, point.X);
        Max.Y = vanguard::math::Max(Max.Y, point.Y);
        Max.Z = vanguard::math::Max(Max.Z, point.Z);
        Max.W = 1.f;

        return *this;
    }

    RED_INLINE Box& Box::AddBox(const Box& box)
    {
        Min = Vector4::Min4(Min, box.Min);
        Max = Vector4::Max4(Max, box.Max);
        return *this;
    }

    RED_INLINE Bool Box::IsEmpty() const
    {
        return (Max.X < Min.X) || (Max.Y < Min.Y) || (Max.Z < Min.Z);
    }

    RED_FORCE_INLINE Bool Box::IsOk() const
    {
        return Min.IsOk() && Max.IsOk();
    }

    RED_INLINE void Box::CalcCorners(Vector4* corners) const
    {
        corners[0] = Vector4{Min.X, Min.Y, Min.Z};
        corners[1] = Vector4{Max.X, Min.Y, Min.Z};
        corners[2] = Vector4{Min.X, Max.Y, Min.Z};
        corners[3] = Vector4{Max.X, Max.Y, Min.Z};
        corners[4] = Vector4{Min.X, Min.Y, Max.Z};
        corners[5] = Vector4{Max.X, Min.Y, Max.Z};
        corners[6] = Vector4{Min.X, Max.Y, Max.Z};
        corners[7] = Vector4{Max.X, Max.Y, Max.Z};
    }

    RED_INLINE Vector4 Box::CalcCorner(ECorner corner) const
    {
        switch (corner)
        {
        case xyz:
            return Vector4{Min.X, Min.Y, Min.Z};
        case xYz:
            return Vector4{Min.X, Max.Y, Min.Z};
        case XYz:
            return Vector4{Max.X, Max.Y, Min.Z};
        case Xyz:
            return Vector4{Max.X, Min.Y, Min.Z};
        case xyZ:
            return Vector4{Min.X, Min.Y, Max.Z};
        case xYZ:
            return Vector4{Min.X, Max.Y, Max.Z};
        case XYZ:
            return Vector4{Max.X, Max.Y, Max.Z};
        case XyZ:
            return Vector4{Max.X, Min.Y, Max.Z};
        default:
        {
            RED_FATAL("Invalid ECorner passed to Box::CalcCorner.");
            return Vector4::PLUS_INF();
        }
        }
    }

    RED_FORCE_INLINE Vector4 Box::CalcCenter() const
    {
        return (Max + Min) * 0.5f;
    }

    RED_FORCE_INLINE Vector4 Box::CalcExtents() const
    {
        return (Max - Min) * 0.5f;
    }

    RED_FORCE_INLINE Vector4 Box::CalcSize() const
    {
        return (Max - Min);
    }

    RED_FORCE_INLINE Float Box::CalcVolume() const
    {
        const Vector4 size = CalcSize();
        return size.X * size.Y * size.Z;
    }

    RED_INLINE Box& Box::Extrude(const Vector4& dir)
    {
        Max += dir;
        Min -= dir;
        return *this;
    }

    RED_INLINE Box& Box::Extrude(const Float value)
    {
        Max += value;
        Min -= value;
        return *this;
    }

    RED_INLINE Box& Box::Expand(const Vector4& dir)
    {
        const auto center = CalcCenter();
        *this -= center;
        Extrude(dir);
        *this += center;
        return *this;
    }

    RED_INLINE Box& Box::Expand(const Float value)
    {
        const auto center = CalcCenter();
        *this -= center;
        Extrude(value);
        *this += center;
        return *this;
    }

    RED_INLINE void Box::Crop(const Box& box)
    {
        Min = Vector4::Min4(Vector4::Max4(Min, box.Min), box.Max);
        Max = Vector4::Min4(Vector4::Max4(Max, box.Min), box.Max);
    }

    RED_INLINE Box& Box::Normalize(const Box& unitBox)
    {
        RED_ASSERT(!unitBox.IsEmpty());

        Vector4 size = Vector4::ONES() / unitBox.CalcSize();
        Min = (Min - unitBox.Min) * size;
        Max = (Max - unitBox.Min) * size;
        return *this;
    }

    RED_INLINE Float Box::Distance(const Vector4& pos) const
    {
        Float sqrsum = 0;
        if (pos.X < Min.X)
            sqrsum += (Min.X - pos.X) * (Min.X - pos.X);
        if (pos.X > Max.X)
            sqrsum += (Max.X - pos.X) * (Max.X - pos.X);
        if (pos.Y < Min.Y)
            sqrsum += (Min.Y - pos.Y) * (Min.Y - pos.Y);
        if (pos.Y > Max.Y)
            sqrsum += (Max.Y - pos.Y) * (Max.Y - pos.Y);
        if (pos.Z < Min.Z)
            sqrsum += (Min.Z - pos.Z) * (Min.Z - pos.Z);
        if (pos.Z > Max.Z)
            sqrsum += (Max.Z - pos.Z) * (Max.Z - pos.Z);
        return (Float)sqrt(sqrsum);
    }

    RED_INLINE Float Box::SquaredDistance(const Vector4& pos) const
    {
        Float sqrsum = 0.0f;
        if (pos.X < Min.X)
            sqrsum += (Min.X - pos.X) * (Min.X - pos.X);
        if (pos.X > Max.X)
            sqrsum += (Max.X - pos.X) * (Max.X - pos.X);
        if (pos.Y < Min.Y)
            sqrsum += (Min.Y - pos.Y) * (Min.Y - pos.Y);
        if (pos.Y > Max.Y)
            sqrsum += (Max.Y - pos.Y) * (Max.Y - pos.Y);
        if (pos.Z < Min.Z)
            sqrsum += (Min.Z - pos.Z) * (Min.Z - pos.Z);
        if (pos.Z > Max.Z)
            sqrsum += (Max.Z - pos.Z) * (Max.Z - pos.Z);
        return sqrsum;
    }

    RED_INLINE Float Box::FarthestPointDistance(const Vector4& pos) const
    {
        Float sqrsum = 0;
        if (pos.X < Min.X)
            sqrsum += (Max.X - pos.X) * (Max.X - pos.X);
        if (pos.X > Max.X)
            sqrsum += (Min.X - pos.X) * (Min.X - pos.X);
        if (pos.Y < Min.Y)
            sqrsum += (Max.Y - pos.Y) * (Max.Y - pos.Y);
        if (pos.Y > Max.Y)
            sqrsum += (Min.Y - pos.Y) * (Min.Y - pos.Y);
        if (pos.Z < Min.Z)
            sqrsum += (Max.Z - pos.Z) * (Max.Z - pos.Z);
        if (pos.Z > Max.Z)
            sqrsum += (Min.Z - pos.Z) * (Min.Z - pos.Z);
        return (Float)sqrt(sqrsum);
    }

    RED_INLINE Float Box::FarthestPointDistanceSquared(const Vector4& pos) const
    {
        Float sqrsum = 0.0f;
        if (pos.X < Min.X)
            sqrsum += (Max.X - pos.X) * (Max.X - pos.X);
        if (pos.X > Max.X)
            sqrsum += (Min.X - pos.X) * (Min.X - pos.X);
        if (pos.Y < Min.Y)
            sqrsum += (Max.Y - pos.Y) * (Max.Y - pos.Y);
        if (pos.Y > Max.Y)
            sqrsum += (Min.Y - pos.Y) * (Min.Y - pos.Y);
        if (pos.Z < Min.Z)
            sqrsum += (Max.Z - pos.Z) * (Max.Z - pos.Z);
        if (pos.Z > Max.Z)
            sqrsum += (Min.Z - pos.Z) * (Min.Z - pos.Z);
        return sqrsum;
    }

    RED_FORCE_INLINE Box Box::UNIT()
    {
        return {Vector4::ZEROS(), Vector4::ONES()};
    }

    RED_FORCE_INLINE Box Box::EMPTY()
    {
        return {RESET_STATE};
    }

    RED_FORCE_INLINE Box Box::FULL()
    {
        return {MAXIMIZE};
    }

    RED_INLINE Float Box::SquaredDistance2D(const Vector4& pos) const
    {
        Float sqrsum = 0.0f;
        if (pos.X < Min.X)
            sqrsum += (Min.X - pos.X) * (Min.X - pos.X);
        if (pos.X > Max.X)
            sqrsum += (Max.X - pos.X) * (Max.X - pos.X);
        if (pos.Y < Min.Y)
            sqrsum += (Min.Y - pos.Y) * (Min.Y - pos.Y);
        if (pos.Y > Max.Y)
            sqrsum += (Max.Y - pos.Y) * (Max.Y - pos.Y);
        return sqrsum;
    }

    RED_INLINE Float Box::SquaredDistance(const Box& box) const
    {
        Float accumulator = 0.f; // accumulate squared root distance

        // for every dimension
        for (Uint32 i = 0; i < 3; ++i)
        {
            // check 1d distance boxes have at given dimension
            if (box.Max[i] < Min[i])
            {
                Float d = Min[i] - box.Max[i];
                accumulator += d * d;
            }
            else if (box.Min[i] > Max[i])
            {
                Float d = box.Min[i] - Max[i];
                accumulator += d * d;
            }
        }

        return accumulator;
    }

} // namespace vanguard::math