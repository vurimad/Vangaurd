/**
 * Copyright (c) 2014 CD Projekt Red. All Rights Reserved.
 */
#pragma once

namespace vanguard::math::simd
{
    typedef __m128 Quad;

    namespace prv
    {
        RED_ALIGNED_VAR(const unsigned int, 16) X_MASKDATA[4] = {0xFFFFFFFF, 0, 0, 0};
        RED_ALIGNED_VAR(const unsigned int, 16) Y_MASKDATA[4] = {0, 0xFFFFFFFF, 0, 0};
        RED_ALIGNED_VAR(const unsigned int, 16) Z_MASKDATA[4] = {0, 0, 0xFFFFFFFF, 0};
        RED_ALIGNED_VAR(const unsigned int, 16) W_MASKDATA[4] = {0, 0, 0, 0xFFFFFFFF};
        RED_ALIGNED_VAR(const unsigned int, 16) XY_MASKDATA[4] = {0xFFFFFFFF, 0xFFFFFFFF, 0, 0};
        RED_ALIGNED_VAR(const unsigned int, 16) XZ_MASKDATA[4] = {0xFFFFFFFF, 0, 0xFFFFFFFF, 0};
        RED_ALIGNED_VAR(const unsigned int, 16) XW_MASKDATA[4] = {0xFFFFFFFF, 0, 0, 0xFFFFFFFF};
        RED_ALIGNED_VAR(const unsigned int, 16) YZ_MASKDATA[4] = {0, 0xFFFFFFFF, 0xFFFFFFFF, 0};
        RED_ALIGNED_VAR(const unsigned int, 16) YW_MASKDATA[4] = {0, 0xFFFFFFFF, 0, 0xFFFFFFFF};
        RED_ALIGNED_VAR(const unsigned int, 16) ZW_MASKDATA[4] = {0, 0, 0xFFFFFFFF, 0xFFFFFFFF};
        RED_ALIGNED_VAR(const unsigned int, 16) XYZ_MASKDATA[4] = {0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0};
        RED_ALIGNED_VAR(const unsigned int, 16) YZW_MASKDATA[4] = {0, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF};
    } // namespace prv

    static const Quad SIGN_MASK = _mm_set1_ps(-0.0f);
    static const Quad X_MASK = _mm_load_ps(reinterpret_cast<const float*>(prv::X_MASKDATA));
    static const Quad Y_MASK = _mm_load_ps(reinterpret_cast<const float*>(prv::Y_MASKDATA));
    static const Quad Z_MASK = _mm_load_ps(reinterpret_cast<const float*>(prv::Z_MASKDATA));
    static const Quad W_MASK = _mm_load_ps(reinterpret_cast<const float*>(prv::W_MASKDATA));
    static const Quad XY_MASK = _mm_load_ps(reinterpret_cast<const float*>(prv::XY_MASKDATA));
    static const Quad XZ_MASK = _mm_load_ps(reinterpret_cast<const float*>(prv::XZ_MASKDATA));
    static const Quad XW_MASK = _mm_load_ps(reinterpret_cast<const float*>(prv::XW_MASKDATA));
    static const Quad YZ_MASK = _mm_load_ps(reinterpret_cast<const float*>(prv::YZ_MASKDATA));
    static const Quad YW_MASK = _mm_load_ps(reinterpret_cast<const float*>(prv::YW_MASKDATA));
    static const Quad ZW_MASK = _mm_load_ps(reinterpret_cast<const float*>(prv::ZW_MASKDATA));
    static const Quad XYZ_MASK = _mm_load_ps(reinterpret_cast<const float*>(prv::XYZ_MASKDATA));
    static const Quad YZW_MASK = _mm_load_ps(reinterpret_cast<const float*>(prv::YZW_MASKDATA));
    static const Quad EPSILON_VALUE = _mm_set1_ps(FLT_EPSILON);

} // namespace vanguard::math::simd
