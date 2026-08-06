/**
 * Copyright (c) 2018 CDProjekt Red, Inc. All Rights Reserved.
 */

#pragma once

namespace vanguard::math
{
    // modified from https://github.com/Reputeless/PerlinNoise/blob/master/PerlinNoise.hpp
    // MIT license
    class REDMATH_API PerlinNoise
    {
    private:
        Int32 p[512];

        RED_INLINE Float Fade(Float t) const
        {
            return t * t * t * (t * (t * 6.0f - 15.0f) + 10.0f);
        }

        RED_INLINE Float Grad(Int32 hash, Float x, Float y, Float z) const
        {
            const Int32 h = hash & 15;
            const Float u = h < 8 ? x : y;
            const Float v = h < 4 ? y : h == 12 || h == 14 ? x : z;
            return ((h & 1) == 0 ? u : -u) + ((h & 2) == 0 ? v : -v);
        }

    public:
        explicit PerlinNoise(Uint32 seed);

        void Seed(Uint32 seed);

        // @return value [ -1 ; 1 ]
        RED_INLINE Float Get(Float x) const
        {
            return Get(x, 0.0f, 0.0f);
        }

        // @return value [ -1 ; 1 ]
        RED_INLINE Float Get(Float x, Float y) const
        {
            return Get(x, y, 0.0f);
        }

        // @return value [ -1 ; 1 ]
        Float Get(Float x, Float y, Float z) const;

        // @return value [ -1 ; 1 ]
        Float GetOctave(Float x, Int32 octaves) const;
        Float GetOctave(Float x, Float y, Int32 octaves) const;
        Float GetOctave(Float x, Float y, Float z, Int32 octaves) const;
    };
} // namespace vanguard::math
