/**
 * Copyright (c) 2018 CDProjekt Red, Inc. All Rights Reserved.
 */

#include "build.h"
#include "random.h"
#include <random>

#ifdef RED_PLATFORM_DURANGO
#include <bcrypt.h>
#endif

namespace vanguard::math
{
    namespace prv
    {
// workaround for CYB-55026: std::random_device seems to crash on XBox
#ifdef RED_PLATFORM_DURANGO
        class RandomDevice
        {
        public:
            Uint32 operator()()
            {
                Uint32 seed = 0;
                auto result = BCryptGenRandom(nullptr, (BYTE*)&seed, sizeof(Uint32), BCRYPT_USE_SYSTEM_PREFERRED_RNG);
                return seed;
            }
        };
#else
        using RandomDevice = std::random_device;
#endif // RED_PLATFORM_DURANGO
    } // namespace prv

    Random::Random()
    {
        Seed();
    }

    Random::Random(Uint64 seed)
    {
        Seed(seed);
    }

    void Random::Seed()
    {
        prv::RandomDevice randomDevice;
        const Uint64 seed = (Uint64)randomDevice() + ((Uint64)randomDevice() << 32);
        Seed(seed);
    }

    void Random::Seed(Uint64 seed)
    {
        m_seed = seed;
        GetRaw();
    }

    Uint32 Random::GetRaw()
    {
        // PCG algorithm
        // http://www.pcg-random.org/download.html

        const Uint64 oldstate = m_seed;
        m_seed = oldstate * 6364136223846793005ull + 1;
        Uint32 xorshifted = static_cast<Uint32>(((oldstate >> 18u) ^ oldstate) >> 27u);
        Uint32 rot = static_cast<Uint32>(oldstate >> 59u);
        return (xorshifted >> rot) | (xorshifted << (31 - rot));
    }

    template <> REDMATH_API Bool Random::Get()
    {
        return GetRaw() & 0x1;
    }

    template <> REDMATH_API Uint32 Random::Get()
    {
        return GetRaw();
    }

    template <> REDMATH_API Int32 Random::Get()
    {
        return GetRaw();
    }

    template <> REDMATH_API Uint64 Random::Get()
    {
        return (Uint64)GetRaw() | ((Uint64)GetRaw() << 32);
    }

    template <> REDMATH_API Int64 Random::Get()
    {
        return (Uint64)GetRaw() | ((Uint64)GetRaw() << 32);
    }

    template <> REDMATH_API Float Random::Get()
    {
        union
        {
            Uint32 i;
            Float f;
        } bits;

        // generate float in [1.0f, 2.0f) range
        bits.i = (GetRaw() & 0x007fffff) | 0x3f800000;

        return bits.f - 1.0f;
    }

    template <> REDMATH_API Double Random::Get()
    {
        union
        {
            Uint64 i;
            Double f;
        } bits;

        // generate float in [1.0, 2.0) range
        bits.i = (Get<Uint64>() & 0x000fffffffffffffull) | 0x3ff0000000000000ull;

        return bits.f - 1.0;
    }

    template <> REDMATH_API Vector2 Random::Get()
    {
        return {Get<Float>(), Get<Float>()};
    }

    template <> REDMATH_API Vector3 Random::Get()
    {
        return {Get<Float>(), Get<Float>(), Get<Float>()};
    }

    template <> REDMATH_API Vector4 Random::Get()
    {
        return {Get<Float>(), Get<Float>(), Get<Float>(), Get<Float>()};
    }

    template <> REDMATH_API Color Random::Get()
    {
        const Uint32 value = GetRaw() | 0xFF000000;
        return Color(value);
    }

    //////////////////////////////////////////////////////////////////////////

    template <> REDMATH_API Uint32 Random::Get(const Uint32 max)
    {
        return GetRaw() % max;
    }

    template <> REDMATH_API Int32 Random::Get(const Int32 max)
    {
        return GetRaw() % max;
    }

    template <> REDMATH_API Uint64 Random::Get(const Uint64 max)
    {
        return Get<Uint64>() % max;
    }

    template <> REDMATH_API Int64 Random::Get(const Int64 max)
    {
        return Get<Int64>() % max;
    }

    template <> REDMATH_API Float Random::Get(const Float max)
    {
        return max * Get<Float>();
    }

    template <> REDMATH_API Double Random::Get(const Double max)
    {
        return max * Get<Double>();
    }

    //////////////////////////////////////////////////////////////////////////

    template <> REDMATH_API Uint32 Random::Get(const Uint32 min, const Uint32 max)
    {
        RED_FATAL_ASSERT(max > min, "Min is greater or equal max");
        return min + GetRaw() % (max - min);
    }

    template <> REDMATH_API Int32 Random::Get(const Int32 min, const Int32 max)
    {
        RED_FATAL_ASSERT(max > min, "Min is greater or equal max");
        return min + (Int32)(GetRaw() % (Uint32)(max - min));
    }

    template <> REDMATH_API Uint64 Random::Get(const Uint64 min, const Uint64 max)
    {
        RED_FATAL_ASSERT(max > min, "Min is greater or equal max");
        return min + Get<Uint64>() % (max - min);
    }

    template <> REDMATH_API Int64 Random::Get(const Int64 min, const Int64 max)
    {
        RED_FATAL_ASSERT(max > min, "Min is greater or equal max");
        return min + (Int64)(Get<Uint64>() % (Uint64)(max - min));
    }

    template <> REDMATH_API Float Random::Get(const Float min, const Float max)
    {
        return min + Get<Float>() * (max - min);
    }

    template <> REDMATH_API Double Random::Get(const Double min, const Double max)
    {
        return min + Get<Double>() * (max - min);
    }

    template <> REDMATH_API Vector2 Random::Get(const Vector2 min, const Vector2 max)
    {
        return min + Get<Vector2>() * (max - min);
    }

    template <> REDMATH_API Vector3 Random::Get(const Vector3 min, const Vector3 max)
    {
        return min + Get<Vector3>() * (max - min);
    }

    template <> REDMATH_API Vector4 Random::Get(const Vector4 min, const Vector4 max)
    {
        return min + Get<Vector4>() * (max - min);
    }

    //////////////////////////////////////////////////////////////////////////

    Random& DefaultRandom()
    {
        thread_local Random gen;
        return gen;
    }

} // namespace vanguard::math
