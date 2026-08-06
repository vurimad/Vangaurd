/**
 * Copyright (c) 2026 RED Vanguard, All Rights Reserved.
 */

#pragma once

namespace vanguard::math
{

    // 2D Point
    struct Point
    {
        Int32 x;
        Int32 y;

        // Empty point, 0,0
        constexpr Point();

        // Construct
        constexpr Point(Int32 _x, Int32 _y);

        constexpr Point operator-() const;
        constexpr Point operator+(const Point p) const;
        constexpr Point operator-(const Point p) const;
        constexpr Point operator*(const Float p) const;
        constexpr Point operator/(const Float p) const;

        constexpr Bool operator==(const Point p) const;
        constexpr Bool operator!=(const Point p) const;

        RED_INLINE Point& operator-=(const Point p);
        RED_INLINE Point& operator+=(const Point p);
        RED_INLINE Point& operator*=(const Float f);
        RED_INLINE Point& operator/=(const Float f);

        // predefined zero point
        constexpr static Point ZERO();
    };

} // namespace vanguard::math

#include "point.hpp"