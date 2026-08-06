/**
* Copyright (c) 2014 CDProjekt Red, Inc. All Rights Reserved.
*/

#pragma once

namespace math
{

	// pseudo random number generator
	class REDMATH_API Random
	{
	public:
		// initialize using non-deterministic seed
		Random();

		// initialize using user-defined seed
		explicit Random( Uint64 seed );

		// reseed using non-deterministic seed value
		void Seed();

		// reseed using custom seed value
		void Seed( Uint64 seed );

		// get raw 4 bytes
		Uint32 GetRaw();

		// Returns value in whole type range with uniform distribution (in case of integers)
		// Returns value in [0.0, 1.0) range with uniform distribution (in case of floats)
		template< typename T >
		T Get();

		// Returns value in range [0, max) with uniform distribution
		template< typename T >
		T Get( const T max );

		// Returns value in range [min, max) with uniform distribution
		template< typename T >
		T Get( const T min, const T max );


		template< Uint32 min, Uint32 max >
		Uint32 Get()
		{
			static_assert( max > min, "Min is greater or equal max" );
			return min + GetRaw() % ( max - min );
		}

	private:
		Uint64 m_seed;
	};

	// get global(shared) random number generator
	REDMATH_API Random& DefaultRandom();

} // math
