/**
* Copyright (c) 2013 CDProjekt Red, Inc. All Rights Reserved.
*/
#ifndef RED_SYSTEM_BIT_UTILS_H
#define RED_SYSTEM_BIT_UTILS_H
#pragma once

/////////////////////////////////////////////////////////////////
// MS-Specific Compiler Intrinsics
#ifdef RED_COMPILER_MSC
	// We must do the switch between the debug / normal malloc here as intrin pulls in malloc.h somewhere down the line
	#ifdef _DEBUG
		#define _CRTDBG_MAP_ALLOC
	#endif
	#include <intrin.h>
	#pragma	intrinsic( _BitScanForward )
	#pragma	intrinsic( _BitScanReverse )

#ifdef _DEBUG
	#undef _CRTDBG_MAP_ALLOC
#endif

#else
	#include <x86intrin.h>
#endif


namespace red
{
		// Deprecated in C++17
		namespace prv
		{
			template<class _Ty>
			struct identity
			{	// map _Ty to type unchanged
				typedef _Ty type;

				const _Ty& operator()(const _Ty& _Left) const
				{	// apply identity operator to operand
					return (_Left);
				}
			};
		}

		namespace BitUtils
		{

		template< class MaskType >
		RED_INLINE Uint32 PopulationCount( typename prv::identity< MaskType >::type mask );

		template<>
		RED_INLINE Uint32 PopulationCount<Uint32>( typename prv::identity< Uint32 >::type mask )
		{
#ifdef RED_COMPILER_MSC
			return (Uint32)__popcnt( mask );
#else
			return (Uint32)__builtin_popcount( mask );
#endif
		}

		//#tbd: or return MaskType
		template<>
		RED_INLINE Uint32 PopulationCount<Uint64>( typename prv::identity< Uint64 >::type mask )
		{
#ifdef RED_COMPILER_MSC
			return (Uint32)__popcnt64( mask );
#else
			return (Uint32)__builtin_popcountl( mask );
#endif
		}
		
			/////////////////////////////////////////////////////////////////////
			// BitScanForward
			//	Return the index of the first bit set (from LSB -> MSB)
			//	Similar to ctz
			template< class MaskType >
			RED_INLINE MaskType BitScanForward( MaskType mask )
			{
				RED_UNUSED( mask );
				RED_ASSERT( 0, "Type not supported!" );
				return 0;
			}

			//////////////////////////////////////////////////////////////////////
			// BitScanReverse
			//	Return the index of the last set bit (from MSB -> LSB)
			//	Note, the result is an absolute bit index (not relative to MSB)
			template< class MaskType >
			RED_INLINE MaskType BitScanReverse( MaskType mask )
			{
				RED_UNUSED( mask );
				RED_ASSERT( 0, "Type not supported!" );
				return 0;
			}

			//////////////////////////////////////////////////////////////////////
			// CountLeadingZeros
			template< class MaskType >
			RED_INLINE MaskType CountLeadingZeros( MaskType mask )
			{
				RED_UNUSED( mask );
				RED_ASSERT( 0, "Type not supported!" );
				return 0;
			}

			//////////////////////////////////////////////////////////////////////
			// Log2
			//	Fast integer log2
			template< class ValueType > 
			RED_INLINE ValueType Log2( ValueType value )
			{
				return BitScanReverse< ValueType >( value );
			}

			/////////////////////////////////////////////////////////////////////
			// BitScanForward <Uint32>
			//	
			template<> RED_INLINE Uint32 BitScanForward<Uint32>( Uint32 mask )
			{
#ifdef RED_COMPILER_MSC
				if( mask != 0 )		// Result is undefined if mask = 0
				{
					DWORD theIndex = 0;
					::_BitScanForward( &theIndex, mask );
					return static_cast<Uint32>( theIndex );	
				}
				else
				{
					return 0;
				}
#else
				return mask == 0 ? 0 : __tzcnt_u32( mask );
#endif
			}

#ifdef RED_ARCH_X64
			/////////////////////////////////////////////////////////////////////
			// CountTrailingZeros <Uint64>
			//	
			template <> RED_INLINE Uint64 BitScanForward<Uint64>( Uint64 mask )
			{
#if defined( RED_COMPILER_MSC )
				if( mask != 0 )
				{
					DWORD theIndex = 0;
					::_BitScanForward64( &theIndex, mask );
					return static_cast<Uint64>( theIndex );		// Returns 0 if mask is 0
				}
				else
				{
					return 0;
				}
#else
				return mask == 0 ? 0 : __tzcnt_u64( mask );
#endif
			}
#endif

			//////////////////////////////////////////////////////////////////////
			// BitScanReverse <Uint32>
			//	
			template <> RED_INLINE Uint32 BitScanReverse<Uint32>( Uint32 mask )
			{
#ifdef RED_COMPILER_MSC
				if( mask != 0 )
				{
					DWORD theIndex = 0;
					::_BitScanReverse( &theIndex, mask );
					return static_cast<Uint32>( theIndex );
				}
				else
				{
					return 0;
				}
#else
				return mask == 0 ? 0 : ( 31 - __lzcnt32( mask ) );
#endif
			}

#ifdef RED_ARCH_X64
			//////////////////////////////////////////////////////////////////////
			// BitScanReverse <Uint64>
			//	
			template <> RED_INLINE Uint64 BitScanReverse<Uint64>( Uint64 mask )
			{
#if defined( RED_COMPILER_MSC )
				if( mask != 0 )
				{
					DWORD theIndex = 0;
					::_BitScanReverse64( &theIndex, mask );
					return static_cast<Uint64>( theIndex );
				}
				else
				{
					return 0;
				}
#else
				return mask == 0 ? 0 : ( 63 - __lzcnt64( mask ) );
#endif
			}

#endif	//	RED_ARCH_X64

			template<> RED_INLINE Uint32 CountLeadingZeros<Uint32>( Uint32 mask )
			{
				if ( mask != 0 )
				{
					Uint32 index = BitScanReverse( mask );
					return 31 - index;
				}
				return 0;
			}

#ifdef RED_ARCH_X64
			template<> RED_INLINE Uint64 CountLeadingZeros<Uint64>( Uint64 mask )
			{
				if ( mask != 0 )
				{
					Uint64 index = BitScanReverse( mask );
					return 63 - index;
				}
				return 0;
			}
#endif	//	RED_ARCH_X64

			RED_INLINE Uint64 RotateLeft( Uint64 value, Int32 shift )
			{
#if defined( RED_COMPILER_MSC )
				return _rotl64( value, shift );
#else
				return ( value << shift ) | ( value >> ( 64 - shift ));
#endif // RED_COMPILER_MSC
			}
	}
}

#endif
