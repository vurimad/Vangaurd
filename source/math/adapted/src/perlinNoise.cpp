/**
 * Copyright (c) 2018 CDProjekt Red, Inc. All Rights Reserved.
 */

#include "build.h"
#include "perlinNoise.h"
#include "random.h"

vanguard::math::PerlinNoise::PerlinNoise(Uint32 seed)
{
    Seed(seed);
}

void vanguard::math::PerlinNoise::Seed(Uint32 seed)
{
    for (Int32 i = 0; i < 256; ++i)
    {
        p[i] = i;
    }

    vanguard::math::Random m_generator;
    m_generator.Seed(seed);
    for (Uint32 i = 0; i < 256; ++i)
    {
        Uint32 randI = m_generator.Get<Uint32>() % 256;
        Swap(p[i], p[randI]);
    }

    for (size_t i = 0; i < 256; ++i)
    {
        p[256 + i] = p[i];
    }
}

Float vanguard::math::PerlinNoise::Get(Float x, Float y, Float z) const
{
    const Int32 X = static_cast<Int32>(floor(x)) & 255;
    const Int32 Y = static_cast<Int32>(floor(y)) & 255;
    const Int32 Z = static_cast<Int32>(floor(z)) & 255;

    x -= floor(x);
    y -= floor(y);
    z -= floor(z);

    const Float u = Fade(x);
    const Float v = Fade(y);
    const Float w = Fade(z);

    const Int32 A = p[X] + Y, AA = p[A] + Z, AB = p[A + 1] + Z;
    const Int32 B = p[X + 1] + Y, BA = p[B] + Z, BB = p[B + 1] + Z;

    return vanguard::math::Lerp(
        w,
        vanguard::math::Lerp(v, vanguard::math::Lerp(u, Grad(p[AA], x, y, z), Grad(p[BA], x - 1, y, z)),
                             vanguard::math::Lerp(u, Grad(p[AB], x, y - 1, z), Grad(p[BB], x - 1, y - 1, z))),
        vanguard::math::Lerp(v, vanguard::math::Lerp(u, Grad(p[AA + 1], x, y, z - 1), Grad(p[BA + 1], x - 1, y, z - 1)),
                             vanguard::math::Lerp(u, Grad(p[AB + 1], x, y - 1, z - 1), Grad(p[BB + 1], x - 1, y - 1, z - 1))));
}

Float vanguard::math::PerlinNoise::GetOctave(Float x, Int32 octaves) const
{
    Float result = 0.0f;
    Float amp = 1.0f;

    for (Int32 i = 0; i < octaves; ++i)
    {
        result += Get(x) * amp;
        x *= 2.0f;
        amp *= 0.5f;
    }

    return result;
}

Float vanguard::math::PerlinNoise::GetOctave(Float x, Float y, Float z, Int32 octaves) const
{
    Float result = 0.0f;
    Float amp = 1.0f;

    for (Int32 i = 0; i < octaves; ++i)
    {
        result += Get(x, y, z) * amp;
        x *= 2.0f;
        y *= 2.0f;
        z *= 2.0f;
        amp *= 0.5f;
    }

    return result;
}

Float vanguard::math::PerlinNoise::GetOctave(Float x, Float y, Int32 octaves) const
{
    Float result = 0.0f;
    Float amp = 1.0f;

    for (Int32 i = 0; i < octaves; ++i)
    {
        result += Get(x, y) * amp;
        x *= 2.0f;
        y *= 2.0f;
        amp *= 0.5f;
    }

    return result;
}
