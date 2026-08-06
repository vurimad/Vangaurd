/**
 * Copyright (c) 2026 RED Vanguard, All Rights Reserved.
 */

#pragma once

class Float16Compressor
{
protected:
    union Bits
    {
        red::Float f;
        red::Int32 si;
        red::Uint32 ui;
    };

    static red::Int32 const shift = 13;
    static red::Int32 const shiftSign = 16;

    static red::Int32 const infN = 0x7F800000;  // flt32 infinity
    static red::Int32 const maxN = 0x477FE000;  // max flt16 normal as a flt32
    static red::Int32 const minN = 0x38800000;  // min flt16 normal as a flt32
    static red::Int32 const signN = 0x80000000; // flt32 sign bit

    static red::Int32 const infC = infN >> shift;
    static red::Int32 const nanN = (infC + 1) << shift; // minimum flt16 nan as a flt32
    static red::Int32 const maxC = maxN >> shift;
    static red::Int32 const minC = minN >> shift;
    static red::Int32 const signC = static_cast<Int32>(static_cast<Uint32>(signN) >> shiftSign); // flt16 sign bit

    static red::Int32 const mulN = 0x52000000; // (1 << 23) / minN
    static red::Int32 const mulC = 0x33800000; // minN / (1 << (23 - shift))

    static red::Int32 const subC = 0x003FF; // max flt32 subnormal down shifted
    static red::Int32 const norC = 0x00400; // min flt32 normal down shifted

    static red::Int32 const maxD = infC - maxC - 1;
    static red::Int32 const minD = minC - subC - 1;

public:
    //! Compress float to float16
    static red::Uint16 Compress(red::Float value)
    {
#ifdef RED_PLATFORM_CONSOLE
        __m128 v1 = _mm_set_ss(value);
        __m128i v2 = _mm_cvtps_ph(v1, 0);
        return static_cast<Uint16>(_mm_cvtsi128_si32(v2));
#else  // RED_PLATFORM_CONSOLE
        Bits v, s;
        v.f = value;
        red::Uint32 sign = v.si & signN;
        v.si ^= sign;
        sign >>= shiftSign; // logical shift
        s.si = mulN;
        s.si = (red::Int32)(s.f * v.f); // correct subnormals
        v.si ^= (s.si ^ v.si) & -(minN > v.si);
        v.si ^= (infN ^ v.si) & -((infN > v.si) & (v.si > maxN));
        v.si ^= (nanN ^ v.si) & -((nanN > v.si) & (v.si > infN));
        v.ui >>= shift; // logical shift
        v.si ^= ((v.si - maxD) ^ v.si) & -(v.si > maxC);
        v.si ^= ((v.si - minD) ^ v.si) & -(v.si > subC);
        return (red::Uint16)(v.ui | sign);
#endif // RED_PLATFORM_CONSOLE
    }

    //! Decompress float16 from float
    static red::Float Decompress(red::Uint16 value)
    {
#ifdef RED_PLATFORM_CONSOLE
        const __m128i v = _mm_cvtsi32_si128(static_cast<Int32>(value));
        return _mm_cvtss_f32(_mm_cvtph_ps(v));
#else  // RED_PLATFORM_CONSOLE
        Bits v;
        v.ui = value;
        red::Int32 sign = v.si & signC;
        v.si ^= sign;
        sign <<= shiftSign;
        v.si ^= ((v.si + minD) ^ v.si) & -(v.si > subC);
        v.si ^= ((v.si + maxD) ^ v.si) & -(v.si > maxC);
        Bits s;
        s.si = mulC;
        s.f *= v.si;
        red::Int32 mask = -(norC > v.si);
        v.si <<= shift;
        v.si ^= (s.si ^ v.si) & mask;
        v.si |= sign;
        return v.f;
#endif // RED_PLATFORM_CONSOLE
    }

    // main part taken from https://gist.github.com/rygorous/2156668
    static RED_INLINE __m128i CompressSSE(__m128 f)
    {
#ifdef RED_PLATFORM_CONSOLE
        return _mm_cvtps_ph(f, _MM_FROUND_CUR_DIRECTION);
#else
        __m128i mask_sign = _mm_set1_epi32(0x80000000u);
        __m128i mask_round = _mm_set1_epi32(~0xfffu);
        __m128i c_f32infty = _mm_set1_epi32(255 << 23);
        __m128i c_magic = _mm_set1_epi32(15 << 23);
        __m128i c_nanbit = _mm_set1_epi32(0x200);
        __m128i c_infty_as_fp16 = _mm_set1_epi32(0x7c00);
        __m128i c_clamp = _mm_set1_epi32((31 << 23) - 0x1000);

        __m128 msign = _mm_castsi128_ps(mask_sign);
        __m128 justsign = _mm_and_ps(msign, f);
        __m128i f32infty = c_f32infty;
        __m128 absf = _mm_xor_ps(f, justsign);
        __m128 mround = _mm_castsi128_ps(mask_round);
        __m128i absf_int = _mm_castps_si128(absf); // pseudo-op, but val needs to be copied once so count as mov
        __m128i b_isnan = _mm_cmpgt_epi32(absf_int, f32infty);
        __m128i b_isnormal = _mm_cmpgt_epi32(f32infty, _mm_castps_si128(absf));
        __m128i nanbit = _mm_and_si128(b_isnan, c_nanbit);
        __m128i inf_or_nan = _mm_or_si128(nanbit, c_infty_as_fp16);

        __m128 fnosticky = _mm_and_ps(absf, mround);
        __m128 scaled = _mm_mul_ps(fnosticky, _mm_castsi128_ps(c_magic));
        __m128 clamped =
            _mm_min_ps(scaled, _mm_castsi128_ps(c_clamp)); // logically, we want PMINSD on "biased", but this should gen better code
        __m128i biased = _mm_sub_epi32(_mm_castps_si128(clamped), _mm_castps_si128(mround));
        __m128i shifted = _mm_srli_epi32(biased, 13);
        __m128i normal = _mm_and_si128(shifted, b_isnormal);
        __m128i not_normal = _mm_andnot_si128(b_isnormal, inf_or_nan);
        __m128i joined = _mm_or_si128(normal, not_normal);

        __m128i sign_shift = _mm_srli_epi32(_mm_castps_si128(justsign), 16);
        __m128i final = _mm_or_si128(joined, sign_shift);

        // make output the same layout as _mm_cvtps_ph
        __m128i tmp_lo = _mm_shufflelo_epi16(final, _MM_SHUFFLE(2, 0, 2, 0));
        __m128i tmp_hi = _mm_shufflehi_epi16(tmp_lo, _MM_SHUFFLE(2, 0, 2, 0));

        __m128i output = _mm_and_si128(_mm_shuffle_epi32(tmp_hi, _MM_SHUFFLE(0, 0, 2, 0)), _mm_setr_epi32(0xFFFFFFFF, 0xFFFFFFFF, 0, 0));

        return output;
#endif
    }
};
