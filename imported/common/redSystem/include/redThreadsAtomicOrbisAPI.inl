/**
* Copyright (c) 2013 CD Projekt Red. All Rights Reserved.
*/
#ifndef RED_THREADS_ATOMIC_ORBISAPI_INL
#define RED_THREADS_ATOMIC_ORBISAPI_INL
#pragma once

#include <sce_atomic.h>

// To ensure consistency with other SCE atomic functions
#ifdef _SCE_ATOMIC_DEFAULT_ACQREL
#	define INTERNAL_MEM_BARRIER_ACQUIRE() do { sceAtomicMemoryBarrier() }while((void)0,0)
#	define INTERNAL_MEM_BARRIER_RELEASE() do { sceAtomicMemoryBarrier() }while((void)0,0)
#else
#	define INTERNAL_MEM_BARRIER_ACQUIRE()
#	define INTERNAL_MEM_BARRIER_RELEASE()
#endif

namespace red { namespace OrbisAPI {

	/*!
		Special cased to return the same value as Windows InterlockedIncrement/Decrement.
		sceAtomicIncrement/Decrement() return the initial value with __sync_fetch_and_add,
		whereas InterlockdIncrement/Decrement() return the new value.
	*/

	static RED_FORCE_INLINE int8_t redThreadsAtomicIncrement8( volatile int8_t* ptr )
	{
		INTERNAL_MEM_BARRIER_ACQUIRE();
		return __atomic_add_fetch( ptr, 1, __ATOMIC_RELAXED );
		INTERNAL_MEM_BARRIER_RELEASE();
	}

	static RED_FORCE_INLINE int8_t redThreadsAtomicDecrement8( volatile int8_t* ptr )
	{
		INTERNAL_MEM_BARRIER_ACQUIRE();
		return __atomic_add_fetch( ptr, -1, __ATOMIC_RELAXED );
		INTERNAL_MEM_BARRIER_RELEASE();
	}

	static RED_FORCE_INLINE int16_t redThreadsAtomicIncrement16(volatile int16_t* ptr)
	{ 
		INTERNAL_MEM_BARRIER_ACQUIRE();
		return __atomic_add_fetch( ptr, 1, __ATOMIC_RELAXED );
		INTERNAL_MEM_BARRIER_RELEASE();
	}

	static RED_FORCE_INLINE int16_t redThreadsAtomicDecrement16(volatile int16_t* ptr)
	{ 
		INTERNAL_MEM_BARRIER_ACQUIRE();
		return __atomic_add_fetch( ptr, -1, __ATOMIC_RELAXED );
		INTERNAL_MEM_BARRIER_RELEASE();
	}

	struct AtomicOps8
	{
		typedef int8_t TAtomic8;

		RED_FORCE_INLINE static TAtomic8		Increment( TAtomic8 volatile* addend ) { return redThreadsAtomicIncrement8( addend ); }
		RED_FORCE_INLINE static TAtomic8		Decrement( TAtomic8 volatile* addend ) { return redThreadsAtomicDecrement8( addend ); }
		RED_FORCE_INLINE static TAtomic8		Exchange( TAtomic8 volatile* target, TAtomic8 value ) { return ::sceAtomicExchange8( target, value ); }
		RED_FORCE_INLINE static TAtomic8		CompareExchange( TAtomic8 volatile* destination, TAtomic8 exchange, TAtomic8 comparand ) { return ::sceAtomicCompareAndSwap8( destination, comparand, exchange ); }
		RED_FORCE_INLINE static TAtomic8		ExchangeAdd( TAtomic8 volatile* addend, TAtomic8 value ) { return ::sceAtomicAdd8( addend, value ); }
		RED_FORCE_INLINE static TAtomic8		Or( TAtomic8 volatile* destination, TAtomic8 value ) { return ::sceAtomicOr8( destination, value ); }
		RED_FORCE_INLINE static TAtomic8		And( TAtomic8 volatile* destination, TAtomic8 value ) { return ::sceAtomicAnd8( destination, value ); }
		RED_FORCE_INLINE static TAtomic8		FetchValue( TAtomic8 volatile* destination ) { return *(volatile TAtomic8*)( destination ); }
	};

	struct AtomicOps16
	{
		//FIXME: redSystem align macro... alignas isn't a good match for MSVC __declspec(align).
		typedef int16_t TAtomic16 __attribute__((aligned (2) ));

		RED_FORCE_INLINE static TAtomic16		Increment( TAtomic16 volatile* addend ) { return redThreadsAtomicIncrement16( addend ); }
		RED_FORCE_INLINE static TAtomic16		Decrement( TAtomic16 volatile* addend ) { return redThreadsAtomicDecrement16( addend ); }
		RED_FORCE_INLINE static TAtomic16		Exchange( TAtomic16 volatile* target, TAtomic16 value ) { return ::sceAtomicExchange16( target, value ); }
		RED_FORCE_INLINE static TAtomic16		CompareExchange( TAtomic16 volatile* destination, TAtomic16 exchange, TAtomic16 comparand ) { return ::sceAtomicCompareAndSwap16( destination, comparand, exchange ); }
		RED_FORCE_INLINE static TAtomic16		ExchangeAdd( TAtomic16 volatile* addend, TAtomic16 value ) { return ::sceAtomicAdd16( addend, value ); }
		RED_FORCE_INLINE static TAtomic16		Or( TAtomic16 volatile* destination, TAtomic16 value ) { return ::sceAtomicOr16( destination, value ); }
		RED_FORCE_INLINE static TAtomic16		And( TAtomic16 volatile* destination, TAtomic16 value ) { return ::sceAtomicAnd16( destination, value ); }
		RED_FORCE_INLINE static TAtomic16		FetchValue( TAtomic16 volatile* destination) { return *(volatile TAtomic16*)(destination); }
	};

	/*!
		Special cased to return the same value as Windows InterlockedIncrement/Decrement.
		sceAtomicIncrement/Decrement() return the initial value with __sync_fetch_and_add,
		whereas InterlockdIncrement/Decrement() return the new value.
	*/

	static RED_FORCE_INLINE int32_t redThreadsAtomicIncrement32(volatile int32_t* ptr)
	{ 
		INTERNAL_MEM_BARRIER_ACQUIRE();
		return __atomic_add_fetch( ptr, 1, __ATOMIC_RELAXED );
		INTERNAL_MEM_BARRIER_RELEASE();
	}

	static RED_FORCE_INLINE int32_t redThreadsAtomicDecrement32(volatile int32_t* ptr)
	{ 
		INTERNAL_MEM_BARRIER_ACQUIRE();
		return __atomic_add_fetch( ptr, -1, __ATOMIC_RELAXED );
		INTERNAL_MEM_BARRIER_RELEASE();
	}

	struct AtomicOps32
	{
		//FIXME: redSystem align macro... alignas isn't a good match for MSVC __declspec(align).
		typedef int32_t TAtomic32 __attribute__((aligned (4) ));

		RED_FORCE_INLINE static TAtomic32		Increment( TAtomic32 volatile* addend ) { return redThreadsAtomicIncrement32( addend ); }
		RED_FORCE_INLINE static TAtomic32		Decrement( TAtomic32 volatile* addend ) { return redThreadsAtomicDecrement32( addend ); }
		RED_FORCE_INLINE static TAtomic32		Exchange( TAtomic32 volatile* target, TAtomic32 value ) { return ::sceAtomicExchange32( target, value ); }
		RED_FORCE_INLINE static TAtomic32		CompareExchange( TAtomic32 volatile* destination, TAtomic32 exchange, TAtomic32 comparand ) { return ::sceAtomicCompareAndSwap32( destination, comparand, exchange ); }
		RED_FORCE_INLINE static TAtomic32		ExchangeAdd( TAtomic32 volatile* addend, TAtomic32 value ) { return ::sceAtomicAdd32( addend, value ); }
		RED_FORCE_INLINE static TAtomic32		Or( TAtomic32 volatile* destination, TAtomic32 value ) { return ::sceAtomicOr32( destination, value ); }
		RED_FORCE_INLINE static TAtomic32		And( TAtomic32 volatile* destination, TAtomic32 value ) { return ::sceAtomicAnd32( destination, value ); }
		RED_FORCE_INLINE static TAtomic32		FetchValue( TAtomic32 volatile* destination) { return *(volatile TAtomic32*)(destination); }
	};

	/*!
		Special cased to return the same value as Windows InterlockedIncrement/Decrement.
		sceAtomicIncrement/Decrement() return the initial value with __sync_fetch_and_add,
		whereas InterlockdIncrement/Decrement() return the new value.
	*/
	static RED_FORCE_INLINE int64_t redThreadsAtomicIncrement64(volatile int64_t* ptr)
	{
		INTERNAL_MEM_BARRIER_ACQUIRE();
		return __atomic_add_fetch( ptr, 1, __ATOMIC_RELAXED );
		INTERNAL_MEM_BARRIER_RELEASE();
	}

	static RED_FORCE_INLINE int64_t redThreadsAtomicDecrement64(volatile int64_t* ptr)
	{ 
		INTERNAL_MEM_BARRIER_ACQUIRE();
		return __atomic_add_fetch( ptr, -1, __ATOMIC_RELAXED );
		INTERNAL_MEM_BARRIER_RELEASE();
	}

	struct AtomicOps64
	{
		//FIXME: redSystem align macro... alignas isn't a good match for MSVC __declspec(align).
		typedef int64_t TAtomic64 __attribute__((aligned (8) ));

		RED_FORCE_INLINE static TAtomic64		Increment( TAtomic64 volatile* addend ) { return redThreadsAtomicIncrement64( addend ); }
		RED_FORCE_INLINE static TAtomic64		Decrement( TAtomic64 volatile* addend ) { return redThreadsAtomicDecrement64( addend ); }
		RED_FORCE_INLINE static TAtomic64		Exchange( TAtomic64 volatile* target, TAtomic64 value ) { return ::sceAtomicExchange64( target, value ); }
		RED_FORCE_INLINE static TAtomic64		CompareExchange( TAtomic64 volatile* destination, TAtomic64 exchange, TAtomic64 comparand ) { return ::sceAtomicCompareAndSwap64( destination, comparand, exchange ); }
		RED_FORCE_INLINE static TAtomic64		ExchangeAdd( TAtomic64 volatile* addend, TAtomic64 value ) { return ::sceAtomicAdd64( addend, value ); }
		RED_FORCE_INLINE static TAtomic64		Or( TAtomic64 volatile* destination, TAtomic64 value ) { return ::sceAtomicOr64( destination, value ); }
		RED_FORCE_INLINE static TAtomic64		And( TAtomic64 volatile* destination, TAtomic64 value ) { return ::sceAtomicAnd64( destination, value ); }		
		RED_FORCE_INLINE static TAtomic64		FetchValue( TAtomic64 volatile* destination ) { return *(volatile TAtomic64*)(destination); }
	};

	struct AtomicOpsPtr
	{
		typedef void* TAtomicPtr __attribute__((aligned (8) ));

		RED_FORCE_INLINE static TAtomicPtr Exchange( TAtomicPtr volatile* target, TAtomicPtr value )
		{ 
			const AtomicOps64::TAtomic64 retval =
			AtomicOps64::Exchange
				(
					reinterpret_cast< AtomicOps64::TAtomic64 volatile * >( target ),
					reinterpret_cast< AtomicOps64::TAtomic64 >( value )
				);

			return reinterpret_cast< TAtomicPtr >( retval );
		}

		RED_FORCE_INLINE static TAtomicPtr CompareExchange( TAtomicPtr volatile* destination, TAtomicPtr exchange, TAtomicPtr comparand )
		{
			const AtomicOps64::TAtomic64 retval =
			AtomicOps64::CompareExchange
				( 
					reinterpret_cast< AtomicOps64::TAtomic64 volatile * >( destination ),
					reinterpret_cast< AtomicOps64::TAtomic64 >( exchange ),
					reinterpret_cast< AtomicOps64::TAtomic64 >( comparand )
				);
			return reinterpret_cast< TAtomicPtr >( retval );
		}

		RED_FORCE_INLINE static TAtomicPtr FetchValue( TAtomicPtr volatile* destination )
		{
			return *(volatile TAtomicPtr*)(destination);
		}

	};

	template <size_t Size>
	class AtomicIntBase
	{
	};

	template<>
	class AtomicIntBase<1U>
	{
	protected:
		typedef AtomicOps8::TAtomic8 TAtomic;
		typedef AtomicOps8 AtomicOps;
	};

	template <>
	class AtomicIntBase<2U>
	{
	protected:
		typedef AtomicOps16::TAtomic16 TAtomic;
		typedef AtomicOps16 AtomicOps;
	};

	template <>
	class AtomicIntBase<4U>
	{
	protected:
		typedef AtomicOps32::TAtomic32 TAtomic;
		typedef AtomicOps32 AtomicOps;
	};

	template <>
	class AtomicIntBase<8U>
	{
	protected:
		typedef AtomicOps64::TAtomic64 TAtomic;
		typedef AtomicOps64 AtomicOps;
	};

	class AtomicPtrBase
	{
	protected:
		typedef AtomicOpsPtr::TAtomicPtr TAtomicPtr;
		typedef AtomicOpsPtr AtomicOps;
	};

} } // namespace red { namespace OrbisAPI {

#undef INTERNAL_MEM_BARRIER_ACQUIRE
#undef INTERNAL_MEM_BARRIER_RELEASE

#endif // RED_THREADS_ATOMIC_ORBISAPI_INL