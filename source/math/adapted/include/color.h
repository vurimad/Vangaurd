/**
 * Copyright (c) 2026 RED Vanguard, All Rights Reserved.
 */

#pragma once

#include "vector4.h"
#include "half.h"

namespace vanguard::math
{
    struct Color
    {
        union
        {
            struct
            {
                Uint8 R, G, B, A;
            };

            Uint8 RGBA[4];
        };

        // Empty, uninitialized color
        Color() = default;

        // Create from RGB channels + optional Alpha
        Color(Uint8 r, Uint8 g, Uint8 b, Uint8 a = 255);

        // Create from HDR color (NOTE: HDR values are clamped)
        Color(const Vector4& x);
        Color(const Float f[4]);

        // Create directly from uint32 color
        explicit Color(Uint32 x);

        // Copy
        Bool operator==(const Color& other) const;

        // Inequality
        Bool operator!=(const Color& other) const
        {
            return R != other.R || G != other.G || B != other.B || A != other.A;
        }

        // Convert to Vector
        Vector4 ToVector() const;

        // Convert to Vector with gamma->linear conversion
        RED_INLINE Vector4 ToVectorLinearAccu() const;
        RED_INLINE Half4 ToHalfLinearAccu() const;

        // Convert from Vector with linear->gamma conversion
        RED_INLINE static Color FromVectorLinear(const Vector4& linearColor);
        RED_INLINE static Color FromVectorLinearAccu(const Vector4& linearColor);

        // Convert to Uint32
        Uint32 ToUint32() const;

        // Get color with no transparency
        Color FullAlpha() const;

        // Multiply RGB part with scalar, keep Alpha path
        RED_INLINE static Color Mul3(const Color& a, Float b);

        // Multiply all parts by scalar
        RED_INLINE static Color Mul4(const Color& a, Float b);

        // Multiply RGB part with scalar, keep Alpha path
        RED_INLINE void Mul3(Float b);

        // Multiply all parts by scalar
        RED_INLINE void Mul4(Float b);

        // Interpolate two colors
        RED_INLINE static Color Lerp(Float coef, const Color& a, const Color& b);

        RED_INLINE Color operator*(Float f) const;
        RED_INLINE Color operator+(const Color& c) const;

        // Some predefined colors
        static Color BLACK(Uint8 alpha = 255);
        static Color WHITE(Uint8 alpha = 255);
        static Color RED(Uint8 alpha = 255);
        static Color GREEN(Uint8 alpha = 255);
        static Color BLUE(Uint8 alpha = 255);
        static Color YELLOW(Uint8 alpha = 255);
        static Color CYAN(Uint8 alpha = 255);
        static Color MAGENTA(Uint8 alpha = 255);
        static Color ORANGE(Uint8 alpha = 255);
        static Color LIGHT_RED(Uint8 alpha = 255);
        static Color LIGHT_GREEN(Uint8 alpha = 255);
        static Color LIGHT_BLUE(Uint8 alpha = 255);
        static Color LIGHT_YELLOW(Uint8 alpha = 255);
        static Color LIGHT_CYAN(Uint8 alpha = 255);
        static Color LIGHT_MAGENTA(Uint8 alpha = 255);
        static Color LIGHT_GRAY(Uint8 alpha = 255);
        static Color LIGHT_ORANGE(Uint8 alpha = 255);
        static Color DARK_RED(Uint8 alpha = 255);
        static Color DARK_GREEN(Uint8 alpha = 255);
        static Color DARK_BLUE(Uint8 alpha = 255);
        static Color DARK_YELLOW(Uint8 alpha = 255);
        static Color DARK_CYAN(Uint8 alpha = 255);
        static Color DARK_MAGENTA(Uint8 alpha = 255);
        static Color DARK_GRAY(Uint8 alpha = 255);
        static Color DARK_ORANGE(Uint8 alpha = 255);
        static Color BROWN(Uint8 alpha = 255);
        static Color GRAY(Uint8 alpha = 255);
        static Color NORMAL(Uint8 alpha = 255);
        static Color CLEAR(); // name TRANSPARENT doesn't compile yourself
    };

} // namespace vanguard::math

// Color definition macro
#define COLOR_UINT32(R, G, B) (((Uint32)R << 0) | ((Uint32)G << 8) | ((Uint32)B << 16))

#include "color.hpp"
