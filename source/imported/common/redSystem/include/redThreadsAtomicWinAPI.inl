/**
* Copyright (c) 2013 CD Projekt Red. All Rights Reserved.
*/
#ifndef RED_THREADS_ATOMIC_WINAPI_H
#define RED_THREADS_ATOMIC_WINAPI_H
#pragma once

#include <intrin.h>

#pragma intrinsic( _InterlockedExchange8 )
#pragma intrinsic( _InterlockedCompareExchange8 )
#pragma intrinsic( _InterlockedExchangeAdd8 )
#pragma intrinsic( _InterlockedOr8 )
#pragma intrinsic( _InterlockedAnd8 )

#pragma intrinsic( _InterlockedIncrement16 )
#pragma intrinsic( _InterlockedDecrement16 )
#pragma intrinsic( _InterlockedExchange16 )
#pragma intrinsic( _InterlockedCompareExchange16 )
#pragma intrinsic( _InterlockedExchangeAdd16 )
#pragma intrinsic( _InterlockedOr16 )
#pragma intrinsic( _InterlockedAnd16 )

#pragma intrinsic( _InterlockedIncrement )
#pragma intrinsic( _InterlockedDecrement )
#pragma intrinsic( _InterlockedExchange )
#pragma intrinsic( _InterlockedCompareExchange )
#pragma intrinsic( _InterlockedExchangeAdd )
#pragma intrinsic( _InterlockedOr )
#pragma intrinsic( _InterlockedAnd )

#pragma intrinsic( _InterlockedIncrement64 )
#pragma intrinsic( _InterlockedDecrement64 )
#pragma intrinsic( _InterlockedExchange64 )
#pragma intrinsic( _InterlockedCompareExchange64 )
#pragma intrinsic( _InterlockedExchangeAdd64 )

#pragma intrinsic( _InterlockedExchangePointer )
#pragma intrinsic( _InterlockedCompareExchangePointer )
#pragma intrinsic( _InterlockedOr64 )
#pragma intrinsic( _InterlockedAnd64 )

namespace red { namespace WinAPI {

	struct AtomicOps8
	{
		typedef char TAtomic8;

		RED_FORCE_INLINE static TAtomic8		Increment( TAtomic8 volatile* addend ) { return (TAtomic8)((Uint8)::_InterlockedExchangeAdd8( addend, 1 ) + 1 ); }
		RED_FORCE_INLINE static TAtomic8		Decrement( TAtomic8 volatile* addend ) { return (TAtomic8)((Uint8)::_InterlockedExchangeAdd8( addend, -1 ) - 1 ); }
		RED_FORCE_INLINE static TAtomic8		Exchange( TAtomic8 volatile* target, TAtomic8 value ) { return ::_InterlockedExchange8( target, value ); }
		RED_FORCE_INLINE static TAtomic8		CompareExchange( TAtomic8 volatile* destination, TAtomic8 exchange, TAtomic8 comparand ) { return ::_InterlockedCompareExchange8( destination, exchange, comparand ); }
		RED_FORCE_INLINE static TAtomic8		ExchangeAdd( TAtomic8 volatile* addend, TAtomic8 value ) { return ::_InterlockedExchangeAdd8( addend, value ); }
		RED_FORCE_INLINE static TAtomic8		Or( TAtomic8 volatile* destination, TAtomic8 value ) { return ::_InterlockedOr8( destination, value ); }
		RED_FORCE_INLINE static TAtomic8		And( TAtomic8 volatile* destination, TAtomic8 value ) { return ::_InterlockedAnd8( destination, value ); }
		RED_FORCE_INLINE static TAtomic8		FetchValue( TAtomic8 volatile* destination ) { return *(volatile TAtomic8*)( destination ); }
	};

	struct AtomicOps16
	{
		//FIXME: redSystem align macro... alignas isn't a good match for MSVC __declspec(align).
		typedef __declspec(align(2)) short TAtomic16;

		RED_FORCE_INLINE static TAtomic16		Increment( TAtomic16 volatile* addend ) { return ::_InterlockedIncrement16( addend ); }
		RED_FORCE_INLINE static TAtomic16		Decrement( TAtomic16 volatile* addend ) { return ::_InterlockedDecrement16( addend ); }
		RED_FORCE_INLINE static TAtomic16		Exchange( TAtomic16 volatile* target, TAtomic16 value ) { return ::_InterlockedExchange16( target, value ); }  
		RED_FORCE_INLINE static TAtomic16		CompareExchange( TAtomic16 volatile* destination, TAtomic16 exchange, TAtomic16 comparand ) { return ::_InterlockedCompareExchange16( destination, exchange, comparand ); }
		RED_FORCE_INLINE static TAtomic16		ExchangeAdd( TAtomic16 volatile* addend, TAtomic16 value )  { return ::_InterlockedExchangeAdd16( addend, value ); }
		RED_FORCE_INLINE static TAtomic16		Or( TAtomic16 volatile* destination, TAtomic16 value ) { return ::_InterlockedOr16( destination, value ); }
		RED_FORCE_INLINE static TAtomic16		And( TAtomic16 volatile* destination, TAtomic16 value ) { return ::_InterlockedAnd16( destination, value ); }
		RED_FORCE_INLINE static TAtomic16		FetchValue( TAtomic16 volatile* destination) { return *(volatile TAtomic16*)(destination); }
	};

	struct AtomicOps32
	{
		//FIXME: redSystem align macro... alignas isn't a good match for MSVC __declspec(align).
		typedef __declspec(align(4)) LONG TAtomic32;

		RED_FORCE_INLINE static TAtomic32		Increment( TAtomic32 volatile* addend ) { return ::_InterlockedIncrement( addend ); }
		RED_FORCE_INLINE static TAtomic32		Decrement( TAtomic32 volatile* addend ) { return ::_InterlockedDecrement( addend ); }
		RED_FORCE_INLINE static TAtomic32		Exchange( TAtomic32 volatile* target, TAtomic32 value ) { return ::_InterlockedExchange( target, value ); }  
		RED_FORCE_INLINE static TAtomic32		CompareExchange( TAtomic32 volatile* destination, TAtomic32 exchange, TAtomic32 comparand ) { return ::_InterlockedCompareExchange( destination, exchange, comparand ); }
		RED_FORCE_INLINE static TAtomic32		ExchangeAdd( TAtomic32 volatile* addend, TAtomic32 value )  { return ::_InterlockedExchangeAdd( addend, value ); }
		RED_FORCE_INLINE static TAtomic32		Or( TAtomic32 volatile* destination, TAtomic32 value ) { return ::_InterlockedOr( destination, value ); }
		RED_FORCE_INLINE static TAtomic32		And( TAtomic32 volatile* destination, TAtomic32 value ) { return ::_InterlockedAnd( destination, value ); }
		RED_FORCE_INLINE static TAtomic32		FetchValue( TAtomic32 volatile* destination) { return *(volatile TAtomic32*)(destination); }
	};

#ifdef RED_ARCH_X64
	struct AtomicOps64
	{
		//FIXME: redSystem align macro... alignas isn't a good match for MSVC __declspec(align).
		typedef __declspec(align(8)) LONG64 TAtomic64;

		RED_FORCE_INLINE static TAtomic64		Increment( TAtomic64 volatile* addend ) { return ::_InterlockedIncrement64( addend ); }
		RED_FORCE_INLINE static TAtomic64		Decrement( TAtomic64 volatile* addend ) { return ::_InterlockedDecrement64( addend ); }
		RED_FORCE_INLINE static TAtomic64		Exchange( TAtomic64 volatile* target, TAtomic64 value ) { return ::_InterlockedExchange64( target, value ); }
		RED_FORCE_INLINE static TAtomic64		CompareExchange( TAtomic64 volatile* destination, TAtomic64 exchange, TAtomic64 comparand ) { return ::_InterlockedCompareExchange64( destination, exchange, comparand ); }
		RED_FORCE_INLINE static TAtomic64		ExchangeAdd( TAtomic64 volatile* addend, TAtomic64 value ) { return ::_InterlockedExchangeAdd64( addend, value ); }
		RED_FORCE_INLINE static TAtomic64		Or( TAtomic64 volatile* destination, TAtomic64 value ) { return ::_InterlockedOr64( destination, value ); }
		RED_FORCE_INLINE static TAtomic64		And( TAtomic64 volatile* destination, TAtomic64 value ) { return ::_InterlockedAnd64( destination, value ); }
		RED_FORCE_INLINE static TAtomic64		FetchValue( TAtomic64 volatile* destination ) { return *(volatile TAtomic64*)(destination); }
	};
#endif

	struct AtomicOpsPtr
	{
		//FIXME: redSystem align macro... alignas isn't a good match for MSVC __declspec(align).
		typedef __declspec(align(8)) void* TAtomicPtr;

		RED_FORCE_INLINE static TAtomicPtr Exchange( TAtomicPtr volatile* target, TAtomicPtr value ) { return _InterlockedExchangePointer( target, value ); }
		RED_FORCE_INLINE static TAtomicPtr CompareExchange( TAtomicPtr volatile* destination, TAtomicPtr exchange, TAtomicPtr comparand ) { return _InterlockedCompareExchangePointer( destination, exchange, comparand ); }
		RED_FORCE_INLINE static TAtomicPtr FetchValue( TAtomicPtr volatile* destination ) { return *(volatile TAtomicPtr*)(destination); }
	};

	template <size_t Size>
	class AtomicIntBase
	{
	};

	template <>
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

} } // namespace red { namespace WinAPI {

#endif // RED_THREADS_ATOMIC_WINAPI_H
