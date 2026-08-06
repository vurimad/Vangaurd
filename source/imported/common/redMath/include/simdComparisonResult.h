/**
* Copyright (c) 2016 CD Projekt Red. All Rights Reserved.
*/

#pragma once

#include "simdQuad.h"

namespace simd
{

struct ComparisonMask
{
	enum Mask
	{
		W_COMPONENT = 3,
		Z_COMPONENT = 2,
		Y_COMPONENT = 1,
		X_COMPONENT = 0,

		NONE_MASK	= 0x0,
		X_MASK      = 1 << X_COMPONENT,
		Y_MASK	    = 1 << Y_COMPONENT,
		Z_MASK	    = 1 << Z_COMPONENT,
		W_MASK	    = 1 << W_COMPONENT,

		XY_MASK		= X_MASK  | Y_MASK,
		XZ_MASK		= X_MASK  | Z_MASK,
		XW_MASK		= X_MASK  | W_MASK,

		YZ_MASK		= Y_MASK  | Z_MASK,
		YW_MASK		= Y_MASK  | W_MASK,
		ZW_MASK		= Z_MASK  | W_MASK,

		XYZ_MASK	= XY_MASK | Z_MASK,
		XYW_MASK	= XY_MASK | W_MASK,
		XZW_MASK	= XZ_MASK | W_MASK,
		YZW_MASK	= YZ_MASK | W_MASK,

		ALL_MASK	= XYZ_MASK| W_MASK
	};
};

class ComparisonResult
{
	simd::Quad m_mask;

public:

	typedef ComparisonMask::Mask Mask;

	RED_INLINE ComparisonResult( simd::Quad f): m_mask(f){}

	RED_INLINE void SetAnd( const ComparisonResult& a, const ComparisonResult& b );
	RED_INLINE void SetOr( const ComparisonResult& a, const ComparisonResult& b );
	RED_INLINE void SetXOR( const ComparisonResult& a, const ComparisonResult& b );
	RED_INLINE void SetNot( const ComparisonResult& a );
	RED_INLINE void SetAndNot( const ComparisonResult& a, const ComparisonResult& b );

	RED_INLINE ComparisonResult  operator&( const ComparisonResult& c ) const;
	RED_INLINE ComparisonResult  operator|( const ComparisonResult& c ) const;
	RED_INLINE ComparisonResult  operator^( const ComparisonResult& c ) const;
	RED_INLINE ComparisonResult& operator!();
	RED_INLINE ComparisonResult& operator~();

	RED_INLINE Mask GetMask();
	RED_INLINE Int32 GetMaski();

	RED_INLINE Bool AreAllSet() const;
	RED_INLINE Bool IsAnySet() const;
};

}

#include "simdComparisonResult.hpp"