/**
* Copyright (c) 2014 CD Projekt Red. All Rights Reserved.
*/
#pragma once

#include "simdQuad.h"

namespace simd
{
	RED_ALIGNED_CLASS( Scalar, 16 )
	{
	public:
		// Constructors
		RED_INLINE Scalar();
		RED_INLINE Scalar( const red::Float _f );
		RED_INLINE Scalar( const Scalar& _v );
		RED_INLINE Scalar( const red::Float* _f );
		RED_INLINE Scalar( Quad _v );

		// Destructor
		RED_INLINE ~Scalar();

		RED_INLINE const red::Float* AsFloat() const;

		// Assignment Operator - We shouldn't allow for more than this.
		RED_INLINE Scalar& operator = ( const Scalar& _v );
		RED_INLINE operator float() const;

		// Setting
		RED_INLINE void Set( red::Float _f );
		RED_INLINE void Set( const Scalar& _v );
		RED_INLINE void Set( const red::Float* _f );

		// Special Sets
		RED_INLINE void SetZeros();
		RED_INLINE void SetOnes();

		// Manipulation Methods.
		RED_INLINE Scalar& Negate();
		RED_INLINE Scalar Negated() const;
		RED_INLINE Scalar Abs() const;

		RED_INLINE red::Bool IsZero() const;
		RED_INLINE red::Bool IsAlmostZero( const Quad _epsilon = EPSILON_VALUE ) const;
			
		union 
		{
			struct
			{
				red::Float X;
				red::Float Y;
				red::Float Z;
				red::Float W;
			};
			struct 
			{
				red::Uint32 Xi;
				red::Uint32 Yi;
				red::Uint32 Zi;
				red::Uint32 Wi;
			};
			Quad V;
		};
			
	};

} // simd

#include "simdScalar.hpp"