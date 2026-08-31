/**
 * Copyright (c) 2026 RED Vanguard, All Rights Reserved.
 */

#pragma once

namespace vanguard::math
{

    struct GammaToLinearLUT
    {
        static constexpr Uint32 Size = 256;

        Half halfLut[Size];
        Float floatLut[Size];

        GammaToLinearLUT();
    };

    REDMATH_API extern GammaToLinearLUT g_gammaToLinearLUT;

    RED_INLINE Color::Color(Uint8 r, Uint8 g, Uint8 b, Uint8 a /*=255*/) : R(r), G(g), B(b), A(a) {}

    RED_INLINE Color::Color(const Vector4& x)
        : vanguard::math::Color{(Uint8)vanguard::math::Clamp(x.X * 255.0f, 0.0f, 255.0f), (Uint8)vanguard::math::Clamp(x.Y * 255.0f, 0.0f, 255.0f),
                                (Uint8)vanguard::math::Clamp(x.Z * 255.0f, 0.0f, 255.0f), (Uint8)vanguard::math::Clamp(x.W * 255.0f, 0.0f, 255.0f)}
    {
    }

    RED_INLINE Color::Color(const Float f[4])
        : vanguard::math::Color{(Uint8)vanguard::math::Clamp(f[0] * 255.0f, 0.0f, 255.0f), (Uint8)vanguard::math::Clamp(f[1] * 255.0f, 0.0f, 255.0f),
                                (Uint8)vanguard::math::Clamp(f[2] * 255.0f, 0.0f, 255.0f), (Uint8)vanguard::math::Clamp(f[3] * 255.0f, 0.0f, 255.0f)}
    {
    }

    RED_FORCE_INLINE Color::Color(Uint32 x) : Color{(Uint8)(x & 0xff), (Uint8)((x >> 8) & 0xff), (Uint8)((x >> 16) & 0xff), (Uint8)((x >> 24) & 0xff)} {}

    RED_INLINE Bool Color::operator==(const Color& other) const
    {
        return (R == other.R) && (G == other.G) && (B == other.B) && (A == other.A);
    }

    RED_INLINE Vector4 Color::ToVector() const
    {
        return {R / 255.0f, G / 255.0f, B / 255.0f, A / 255.0f};
    }

    RED_INLINE Float ToGamma(Float linearCol)
    {
        return powf(linearCol, 2.2f);
    }

    RED_INLINE Vector3 ToGamma(const Vector3& linearCol)
    {
        return {ToGamma(linearCol.X), ToGamma(linearCol.Y), ToGamma(linearCol.Z)};
    }

    RED_INLINE Vector4 ToGamma(const Vector4& linearCol)
    {
        return {ToGamma(linearCol.X), ToGamma(linearCol.Y), ToGamma(linearCol.Z), linearCol.W};
    }

    RED_INLINE Float ToLinear(Float gammaCol)
    {
        return powf(gammaCol, 1.0f / 2.2f);
    }

    RED_INLINE Vector3 ToLinear(const Vector3& gammaCol)
    {
        return {ToLinear(gammaCol.X), ToLinear(gammaCol.Y), ToLinear(gammaCol.Z)};
    }

    RED_INLINE Vector4 ToLinear(const Vector4& gammaCol)
    {
        return {ToLinear(gammaCol.X), ToLinear(gammaCol.Y), ToLinear(gammaCol.Z), gammaCol.W};
    }

    RED_INLINE Float ToLinearAccu(const Float sRGBCol)
    {
        const Float linearRGBLo = sRGBCol / 12.92f;
        const Float linearRGBHi = powf((sRGBCol + 0.055f) / 1.055f, 2.4f);
        const Float linearRGB = (sRGBCol <= 0.04045f) ? linearRGBLo : linearRGBHi;
        return linearRGB;
    }

    RED_INLINE Float ToGammaAccu(Float linearCol)
    {
        const Float sRGBLo = linearCol * 12.92f;
        const Float sRGBHi = (powf(fabsf(linearCol), 1.0f / 2.4f) * 1.055f) - 0.055f;
        const Float sRGB = (linearCol <= 0.0031308f) ? sRGBLo : sRGBHi;
        return sRGB;
    }

    RED_INLINE Vector4 ToGammaAccu(const Vector4& linearCol)
    {
        return {ToGammaAccu(linearCol.X), ToGammaAccu(linearCol.Y), ToGammaAccu(linearCol.Z), linearCol.W};
    }

    RED_FORCE_INLINE Vector4 Color::ToVectorLinearAccu() const
    {
        return {g_gammaToLinearLUT.floatLut[R], g_gammaToLinearLUT.floatLut[G], g_gammaToLinearLUT.floatLut[B], A / 255.0f};
    }

    RED_FORCE_INLINE Half4 Color::ToHalfLinearAccu() const
    {
        return {g_gammaToLinearLUT.halfLut[R], g_gammaToLinearLUT.halfLut[G], g_gammaToLinearLUT.halfLut[B], Half(A / 255.0f)};
    }

    Color Color::FromVectorLinear(const Vector4& linearColor)
    {
        return {(Uint8)vanguard::math::Clamp(ToGamma(linearColor.X) * 255.0f, 0.0f, 255.0f),
                (Uint8)vanguard::math::Clamp(ToGamma(linearColor.Y) * 255.0f, 0.0f, 255.0f),
                (Uint8)vanguard::math::Clamp(ToGamma(linearColor.Z) * 255.0f, 0.0f, 255.0f),
                (Uint8)vanguard::math::Clamp(linearColor.W * 255.0f, 0.0f, 255.0f)};
    }

    Color Color::FromVectorLinearAccu(const Vector4& linearColor)
    {
        return {(Uint8)vanguard::math::Clamp(ToGammaAccu(linearColor.X) * 255.0f, 0.0f, 255.0f),
                (Uint8)vanguard::math::Clamp(ToGammaAccu(linearColor.Y) * 255.0f, 0.0f, 255.0f),
                (Uint8)vanguard::math::Clamp(ToGammaAccu(linearColor.Z) * 255.0f, 0.0f, 255.0f),
                (Uint8)vanguard::math::Clamp(linearColor.W * 255.0f, 0.0f, 255.0f)};
    }

    RED_INLINE Color Color::Mul3(const Color& a, Float b)
    {
        RED_ASSERT(b >= 0.0f && b <= 1.0f);
        Color out;
        out.R = (Uint8)vanguard::math::Clamp<Float>(a.R * b, 0.0f, 255.0f);
        out.G = (Uint8)vanguard::math::Clamp<Float>(a.G * b, 0.0f, 255.0f);
        out.B = (Uint8)vanguard::math::Clamp<Float>(a.B * b, 0.0f, 255.0f);
        out.A = a.A;
        return out;
    }

    RED_INLINE Color Color::Mul4(const Color& a, Float b)
    {
        RED_ASSERT(b >= 0.0f && b <= 1.0f);
        Color out;
        out.R = (Uint8)vanguard::math::Clamp<Float>(a.R * b, 0.0f, 255.0f);
        out.G = (Uint8)vanguard::math::Clamp<Float>(a.G * b, 0.0f, 255.0f);
        out.B = (Uint8)vanguard::math::Clamp<Float>(a.B * b, 0.0f, 255.0f);
        out.A = (Uint8)vanguard::math::Clamp<Float>(a.A * b, 0.0f, 255.0f);
        return out;
    }

    RED_INLINE void Color::Mul3(Float b)
    {
        RED_ASSERT(b >= 0.0f && b <= 1.0f);
        R = (Uint8)vanguard::math::Clamp<Float>(R * b, 0.0f, 255.0f);
        G = (Uint8)vanguard::math::Clamp<Float>(G * b, 0.0f, 255.0f);
        B = (Uint8)vanguard::math::Clamp<Float>(B * b, 0.0f, 255.0f);
    }

    RED_INLINE void Color::Mul4(Float b)
    {
        RED_ASSERT(b >= 0.0f && b <= 1.0f);
        R = (Uint8)vanguard::math::Clamp<Float>(R * b, 0.0f, 255.0f);
        G = (Uint8)vanguard::math::Clamp<Float>(G * b, 0.0f, 255.0f);
        B = (Uint8)vanguard::math::Clamp<Float>(B * b, 0.0f, 255.0f);
        A = (Uint8)vanguard::math::Clamp<Float>(A * b, 0.0f, 255.0f);
    }

    RED_INLINE Color Color::Lerp(Float coef, const Color& a, const Color& b)
    {
        RED_ASSERT(coef >= 0.0f && coef <= 1.0f);
        Float one_minut_coef = 1.0f - coef;
        Color out;
        out.R = (Uint8)vanguard::math::Clamp<Float>(a.R * one_minut_coef + b.R * coef, 0.0f, 255.0f);
        out.G = (Uint8)vanguard::math::Clamp<Float>(a.G * one_minut_coef + b.G * coef, 0.0f, 255.0f);
        out.B = (Uint8)vanguard::math::Clamp<Float>(a.B * one_minut_coef + b.B * coef, 0.0f, 255.0f);
        out.A = (Uint8)vanguard::math::Clamp<Float>(a.A * one_minut_coef + b.A * coef, 0.0f, 255.0f);
        return out;
    }

    RED_INLINE Color Color::operator*(Float f) const
    {
        Color result = *this;
        result.Mul4(f);
        return result;
    }

    RED_INLINE Color Color::operator+(const Color& c) const
    {
        Color result = *this;

        result.R = vanguard::math::Clamp<Uint8>(R + c.R, 0, 255);
        result.G = vanguard::math::Clamp<Uint8>(G + c.G, 0, 255);
        result.B = vanguard::math::Clamp<Uint8>(B + c.B, 0, 255);
        result.A = vanguard::math::Clamp<Uint8>(A + c.A, 0, 255);

        return result;
    }

    RED_INLINE Uint32 Color::ToUint32() const
    {
        return ((Uint32)R << 0) | ((Uint32)G << 8) | ((Uint32)B << 16) | ((Uint32)A << 24);
    }

    RED_INLINE Color Color::FullAlpha() const
    {
        return {R, G, B, 255};
    }

    RED_INLINE Color Color::BLACK(Uint8 alpha)
    {
        return {0, 0, 0, alpha};
    }

    RED_INLINE Color Color::WHITE(Uint8 alpha)
    {
        return {255, 255, 255, alpha};
    }

    RED_INLINE Color Color::RED(Uint8 alpha)
    {
        return {255, 0, 0, alpha};
    }

    RED_INLINE Color Color::GREEN(Uint8 alpha)
    {
        return {0, 255, 0, alpha};
    }

    RED_INLINE Color Color::BLUE(Uint8 alpha)
    {
        return {0, 0, 255, alpha};
    }

    RED_INLINE Color Color::YELLOW(Uint8 alpha)
    {
        return {255, 255, 0, alpha};
    }

    RED_INLINE Color Color::CYAN(Uint8 alpha)
    {
        return {0, 255, 255, alpha};
    }

    RED_INLINE Color Color::MAGENTA(Uint8 alpha)
    {
        return {255, 0, 255, alpha};
    }

    RED_INLINE Color Color::ORANGE(Uint8 alpha)
    {
        return {255, 165, 0, alpha};
    }

    RED_INLINE Color Color::LIGHT_RED(Uint8 alpha)
    {
        return {255, 127, 127, alpha};
    }

    RED_INLINE Color Color::LIGHT_GREEN(Uint8 alpha)
    {
        return {127, 255, 127, alpha};
    }

    RED_INLINE Color Color::LIGHT_BLUE(Uint8 alpha)
    {
        return {127, 127, 255, alpha};
    }

    RED_INLINE Color Color::LIGHT_YELLOW(Uint8 alpha)
    {
        return {255, 255, 127, alpha};
    }

    RED_INLINE Color Color::LIGHT_CYAN(Uint8 alpha)
    {
        return {127, 255, 255, alpha};
    }

    RED_INLINE Color Color::LIGHT_MAGENTA(Uint8 alpha)
    {
        return {255, 127, 255, alpha};
    }

    RED_INLINE Color Color::LIGHT_GRAY(Uint8 alpha)
    {
        return {192, 192, 192, alpha};
    }

    RED_INLINE Color Color::LIGHT_ORANGE(Uint8 alpha)
    {
        return {255, 178, 102, alpha};
    }

    RED_INLINE Color Color::DARK_RED(Uint8 alpha)
    {
        return {127, 0, 0, alpha};
    }

    RED_INLINE Color Color::DARK_GREEN(Uint8 alpha)
    {
        return {0, 127, 0, alpha};
    }

    RED_INLINE Color Color::DARK_BLUE(Uint8 alpha)
    {
        return {0, 0, 127, alpha};
    }

    RED_INLINE Color Color::DARK_YELLOW(Uint8 alpha)
    {
        return {127, 127, 0, alpha};
    }

    RED_INLINE Color Color::DARK_CYAN(Uint8 alpha)
    {
        return {0, 127, 127, alpha};
    }

    RED_INLINE Color Color::DARK_MAGENTA(Uint8 alpha)
    {
        return {127, 0, 127, alpha};
    }

    RED_INLINE Color Color::DARK_GRAY(Uint8 alpha)
    {
        return {92, 92, 92, alpha};
    }

    RED_INLINE Color Color::DARK_ORANGE(Uint8 alpha)
    {
        return {255, 140, 0, alpha};
    }

    RED_INLINE Color Color::BROWN(Uint8 alpha)
    {
        return {139, 69, 19, alpha};
    }

    RED_INLINE Color Color::GRAY(Uint8 alpha)
    {
        return {127, 127, 127, alpha};
    }

    RED_INLINE Color Color::NORMAL(Uint8 alpha)
    {
        return {127, 127, 255, alpha};
    }

    RED_INLINE Color Color::CLEAR()
    {
        return {0, 0, 0, 0};
    }

} // namespace vanguard::math