/**
 * Copyright (c) 2016 CD Projekt Red. All Rights Reserved.
 */

#pragma once

namespace vanguard::math::simd
{
    RED_INLINE void ComparisonResult::SetAnd(const ComparisonResult& a, const ComparisonResult& b)
    {
        m_mask = _mm_and_ps(a.m_mask, b.m_mask);
    }

    RED_INLINE void ComparisonResult::SetOr(const ComparisonResult& a, const ComparisonResult& b)
    {
        m_mask = _mm_or_ps(a.m_mask, b.m_mask);
    }

    RED_INLINE void ComparisonResult::SetXOR(const ComparisonResult& a, const ComparisonResult& b)
    {
        m_mask = _mm_xor_ps(a.m_mask, b.m_mask);
    }

    RED_INLINE void ComparisonResult::SetNot(const ComparisonResult& a)
    {
        m_mask = _mm_castsi128_ps(_mm_cmpeq_epi32(_mm_setzero_si128(), _mm_castps_si128(a.m_mask)));
    }

    RED_INLINE void ComparisonResult::SetAndNot(const ComparisonResult& a, const ComparisonResult& b)
    {
        m_mask = _mm_andnot_ps(a.m_mask, b.m_mask);
    }

    RED_INLINE ComparisonResult ComparisonResult::operator&(const ComparisonResult& c) const
    {
        return ComparisonResult(_mm_and_ps(m_mask, c.m_mask));
    }

    RED_INLINE ComparisonResult ComparisonResult::operator|(const ComparisonResult& c) const
    {
        return ComparisonResult(_mm_or_ps(m_mask, c.m_mask));
    }

    RED_INLINE ComparisonResult ComparisonResult::operator^(const ComparisonResult& c) const
    {
        return ComparisonResult(_mm_xor_ps(m_mask, c.m_mask));
    }

    RED_INLINE ComparisonResult& ComparisonResult::operator!()
    {
        SetNot(*this);
        return *this;
    }

    RED_INLINE ComparisonResult& ComparisonResult::operator~()
    {
        SetNot(*this);
        return *this;
    }

    RED_INLINE ComparisonMask::Mask ComparisonResult::GetMask()
    {
        return (ComparisonMask::Mask)_mm_movemask_ps(m_mask);
    }

    RED_INLINE Int32 ComparisonResult::GetMaski()
    {
        return _mm_movemask_ps(m_mask);
    }

    RED_INLINE Bool ComparisonResult::AreAllSet() const
    {
        return (_mm_movemask_ps(m_mask) & 0xF) > 0;
    }

    RED_INLINE Bool ComparisonResult::IsAnySet() const
    {
        return _mm_movemask_ps(m_mask) > 0;
    }
} // namespace vanguard::math::simd