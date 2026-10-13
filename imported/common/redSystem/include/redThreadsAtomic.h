/**
* Copyright (c) 2013 CD Projekt Red. All Rights Reserved.
*/
#ifndef RED_THREADS_ATOMIC_H
#define RED_THREADS_ATOMIC_H
#pragma once

#include "redThreadsPlatform.h"

#if defined ( RED_THREADS_PLATFORM_WINDOWS_API )
#	include "redThreadsAtomicWinAPI.inl"
#elif defined ( RED_PLATFORM_ORBIS )
#	include "redThreadsAtomicOrbisAPI.inl"
#elif defined ( RED_PLATFORM_LINUX )
#	include "redThreadsAtomicLinuxAPI.inl"
#else
#	error Platform not supported
#endif

//////////////////////////////////////////////////////////////////////////
// Atomic operations with full memory barriers
//////////////////////////////////////////////////////////////////////////
namespace atomic
{
	namespace OSAPI = ::red::OSAPI;
	
	typedef OSAPI::AtomicOps8::TAtomic8 TAtomic8;
	typedef OSAPI::AtomicOps16::TAtomic16 TAtomic16;
	typedef OSAPI::AtomicOps32::TAtomic32 TAtomic32;
	typedef OSAPI::AtomicOps64::TAtomic64 TAtomic64;
	typedef OSAPI::AtomicOpsPtr::TAtomicPtr TAtomicPtr;

	namespace prv
	{
		// #tbd: really worth the bother, memcpy in general case should be optimized out
		template< typename TFrom, typename TTo>
		struct IsPointerConv
		{
			static const bool Value = std::is_pointer<TFrom>::value && std::is_pointer<TTo>::value;
		};
	}

	// Because of strict-aliasing. Could go through a union, but that wouldn't work
	// well with type qualifiers on template argument types.
	template < typename TFrom, typename TTo>
	RED_FORCE_INLINE auto alias_cast( TFrom from ) -> typename std::enable_if<!prv::IsPointerConv<TFrom,TTo>::Value, TTo >::type
	{
		static_assert( sizeof(TFrom) == sizeof(TTo), "Invalid alias cast" );

		// Simple types
		// memcpy call should also be optimized out by the compiler
		TTo to = {0}; // #tbd: replace with c++11 type traits check
		red::Memcpy( &to, &from, sizeof from );
		return to;
	}

 	template < typename TFrom, typename TTo >
	RED_FORCE_INLINE auto alias_cast( TFrom from ) -> typename std::enable_if<prv::IsPointerConv<TFrom,TTo>::Value, TTo >::type
 	{
 		static_assert( sizeof(TFrom) == sizeof(TTo), "Invalid ptr cast" );
 
 		// Simple types
 		const uintptr_t addr = reinterpret_cast< uintptr_t >( from );
 		TTo to = reinterpret_cast< TTo >( addr );
 		return to;
 	}

	template< typename TFrom > RED_FORCE_INLINE TAtomic8* alias_cast8( TFrom from )
	{
		return alias_cast<TFrom, TAtomic8*>( from );
	}

	template< typename TFrom > RED_FORCE_INLINE TAtomic16* alias_cast16( TFrom from )
	{
		return alias_cast<TFrom, TAtomic16*>( from );
	}

	template< typename TFrom > RED_FORCE_INLINE TAtomic32* alias_cast32( TFrom from )
	{
		return alias_cast<TFrom, TAtomic32*>( from );
	}

	template< typename TFrom > RED_FORCE_INLINE TAtomic64* alias_cast64( TFrom from )
	{
		return alias_cast<TFrom, TAtomic64*>( from );
	}

	template< typename TFrom> RED_FORCE_INLINE TAtomicPtr alias_castptr( TFrom from )
	{
		return alias_cast<TFrom, TAtomicPtr>( from );
	}

	RED_FORCE_INLINE TAtomic8		Increment8( TAtomic8 volatile* addend ) { return OSAPI::AtomicOps8::Increment( addend ); }
	RED_FORCE_INLINE TAtomic8		Decrement8( TAtomic8 volatile* addend ) { return OSAPI::AtomicOps8::Decrement( addend ); }
	RED_FORCE_INLINE TAtomic8		Exchange8( TAtomic8 volatile* target, TAtomic8 value ) { return OSAPI::AtomicOps8::Exchange( target, value ); }
	RED_FORCE_INLINE TAtomic8		CompareExchange8( TAtomic8 volatile* destination, TAtomic8 exchange, TAtomic8 comparand ) { return OSAPI::AtomicOps8::CompareExchange( destination, exchange, comparand ); }
	RED_FORCE_INLINE TAtomic8		ExchangeAdd8( TAtomic8 volatile* addend, TAtomic8 value ) { return OSAPI::AtomicOps8::ExchangeAdd( addend, value ); }
	RED_FORCE_INLINE TAtomic8		Or8( TAtomic8 volatile* destination, TAtomic8 value ) { return OSAPI::AtomicOps8::Or( destination, value ); }
	RED_FORCE_INLINE TAtomic8		And8( TAtomic8 volatile* destination, TAtomic8 value ) { return OSAPI::AtomicOps8::And( destination, value ); }
	RED_FORCE_INLINE TAtomic8		FetchValue8( TAtomic8 volatile* destination ) { return OSAPI::AtomicOps8::FetchValue( destination ); }

	RED_FORCE_INLINE TAtomic16		Increment16( TAtomic16 volatile* addend ) { return OSAPI::AtomicOps16::Increment( addend ); }
	RED_FORCE_INLINE TAtomic16		Decrement16( TAtomic16 volatile* addend ) { return OSAPI::AtomicOps16::Decrement( addend ); }
	RED_FORCE_INLINE TAtomic16		Exchange16( TAtomic16 volatile* target, TAtomic16 value ) { return OSAPI::AtomicOps16::Exchange( target, value ); }  
	RED_FORCE_INLINE TAtomic16		CompareExchange16( TAtomic16 volatile* destination, TAtomic16 exchange, TAtomic16 comparand ) { return OSAPI::AtomicOps16::CompareExchange( destination, exchange, comparand ); }
	RED_FORCE_INLINE TAtomic16		ExchangeAdd16( TAtomic16 volatile* addend, TAtomic16 value )  { return OSAPI::AtomicOps16::ExchangeAdd( addend, value ); }
	RED_FORCE_INLINE TAtomic16		Or16( TAtomic16 volatile* destination, TAtomic16 value ) { return OSAPI::AtomicOps16::Or( destination, value ); }
	RED_FORCE_INLINE TAtomic16		And16( TAtomic16 volatile* destination, TAtomic16 value ) { return OSAPI::AtomicOps16::And( destination, value ); }
	RED_FORCE_INLINE TAtomic16		FetchValue16( TAtomic16 volatile* destination ) {  return OSAPI::AtomicOps16::FetchValue( destination ); }

	RED_FORCE_INLINE TAtomic32		Increment32( TAtomic32 volatile* addend ) { return OSAPI::AtomicOps32::Increment( addend ); }
	RED_FORCE_INLINE TAtomic32		Decrement32( TAtomic32 volatile* addend ) { return OSAPI::AtomicOps32::Decrement( addend ); }
	RED_FORCE_INLINE TAtomic32		Exchange32( TAtomic32 volatile* target, TAtomic32 value ) { return OSAPI::AtomicOps32::Exchange( target, value ); }  
	RED_FORCE_INLINE TAtomic32		CompareExchange32( TAtomic32 volatile* destination, TAtomic32 exchange, TAtomic32 comparand ) { return OSAPI::AtomicOps32::CompareExchange( destination, exchange, comparand ); }
	RED_FORCE_INLINE TAtomic32		ExchangeAdd32( TAtomic32 volatile* addend, TAtomic32 value )  { return OSAPI::AtomicOps32::ExchangeAdd( addend, value ); }
	RED_FORCE_INLINE TAtomic32		Or32( TAtomic32 volatile* destination, TAtomic32 value ) { return OSAPI::AtomicOps32::Or( destination, value ); }
	RED_FORCE_INLINE TAtomic32		And32( TAtomic32 volatile* destination, TAtomic32 value ) { return OSAPI::AtomicOps32::And( destination, value ); }
	RED_FORCE_INLINE TAtomic32		FetchValue32( TAtomic32 volatile* destination ) {  return OSAPI::AtomicOps32::FetchValue( destination ); }

	RED_FORCE_INLINE TAtomic64		Increment64( TAtomic64 volatile* addend ) { return OSAPI::AtomicOps64::Increment( addend ); }
	RED_FORCE_INLINE TAtomic64		Decrement64( TAtomic64 volatile* addend ) { return OSAPI::AtomicOps64::Decrement( addend ); }
	RED_FORCE_INLINE TAtomic64		Exchange64( TAtomic64 volatile* target, TAtomic64 value ) { return OSAPI::AtomicOps64::Exchange( target, value ); }
	RED_FORCE_INLINE TAtomic64		CompareExchange64( TAtomic64 volatile* destination, TAtomic64 exchange, TAtomic64 comparand ) { return OSAPI::AtomicOps64::CompareExchange( destination, exchange, comparand ); }
	RED_FORCE_INLINE TAtomic64		ExchangeAdd64( TAtomic64 volatile* addend, TAtomic64 value ) { return OSAPI::AtomicOps64::ExchangeAdd( addend, value ); }
	RED_FORCE_INLINE TAtomic64		Or64( TAtomic64 volatile* destination, TAtomic64 value ) { return OSAPI::AtomicOps64::Or( destination, value ); }
	RED_FORCE_INLINE TAtomic64		And64( TAtomic64 volatile* destination, TAtomic64 value ) { return OSAPI::AtomicOps64::And( destination, value ); }
	RED_FORCE_INLINE TAtomic64		FetchValue64( TAtomic64 volatile* destination ) { return OSAPI::AtomicOps64::FetchValue( destination ); }
	
	RED_FORCE_INLINE TAtomicPtr		ExchangePtr( TAtomicPtr volatile* target, TAtomicPtr value ) { return OSAPI::AtomicOpsPtr::Exchange( target, value ); }
	RED_FORCE_INLINE TAtomicPtr		CompareExchangePtr( TAtomicPtr volatile* destination, TAtomicPtr exchange, TAtomicPtr comparand ) { return OSAPI::AtomicOpsPtr::CompareExchange( destination, exchange, comparand ); }
}

// Convenience Helpers. Probably should consider remove TAtomic.
namespace atomic
{
	RED_FORCE_INLINE Uint32		Increment(Uint32 volatile* addend) { return OSAPI::AtomicOps32::Increment( atomic::alias_cast32(addend) ); }
	RED_FORCE_INLINE Uint32		Decrement(Uint32 volatile* addend) { return OSAPI::AtomicOps32::Decrement(atomic::alias_cast32(addend)); }
	RED_FORCE_INLINE Uint32		Exchange(Uint32 volatile* target, Uint32 value) { return OSAPI::AtomicOps32::Exchange(atomic::alias_cast32(target), value); }
	RED_FORCE_INLINE Uint32		CompareExchange(Uint32 volatile* destination, Uint32 exchange, TAtomic32 comparand) { return OSAPI::AtomicOps32::CompareExchange(atomic::alias_cast32(destination), exchange, comparand); }
	RED_FORCE_INLINE Uint32		ExchangeAdd(Uint32 volatile* addend, Uint32 value) { return OSAPI::AtomicOps32::ExchangeAdd(atomic::alias_cast32(addend), value); }
	RED_FORCE_INLINE Uint32		Or(Uint32 volatile* destination, Uint32 value) { return OSAPI::AtomicOps32::Or(atomic::alias_cast32(destination), value); }
	RED_FORCE_INLINE Uint32		And(Uint32 volatile* destination, Uint32 value) { return OSAPI::AtomicOps32::And(atomic::alias_cast32(destination), value); }
	RED_FORCE_INLINE Uint32		FetchValue(Uint32 volatile* destination) { return OSAPI::AtomicOps32::FetchValue(atomic::alias_cast32(destination)); }

	RED_FORCE_INLINE Int32		Increment(Int32 volatile* addend) { return OSAPI::AtomicOps32::Increment(atomic::alias_cast32(addend)); }
	RED_FORCE_INLINE Int32		Decrement(Int32 volatile* addend) { return OSAPI::AtomicOps32::Decrement(atomic::alias_cast32(addend)); }
	RED_FORCE_INLINE Int32		Exchange(Int32 volatile* target, Int32 value) { return OSAPI::AtomicOps32::Exchange(atomic::alias_cast32(target), value); }
	RED_FORCE_INLINE Int32		CompareExchange(Int32 volatile* destination, Int32 exchange, TAtomic32 comparand) { return OSAPI::AtomicOps32::CompareExchange(atomic::alias_cast32(destination), exchange, comparand); }
	RED_FORCE_INLINE Int32		ExchangeAdd(Int32 volatile* addend, Int32 value) { return OSAPI::AtomicOps32::ExchangeAdd(atomic::alias_cast32(addend), value); }
	RED_FORCE_INLINE Int32		Or(Int32 volatile* destination, Int32 value) { return OSAPI::AtomicOps32::Or(atomic::alias_cast32(destination), value); }
	RED_FORCE_INLINE Int32		And(Int32 volatile* destination, Int32 value) { return OSAPI::AtomicOps32::And(atomic::alias_cast32(destination), value); }
	RED_FORCE_INLINE Int32		FetchValue(Int32 volatile* destination) { return OSAPI::AtomicOps32::FetchValue(atomic::alias_cast32(destination)); }

	//---

	RED_FORCE_INLINE Uint64		Increment(Uint64 volatile* addend) { return OSAPI::AtomicOps64::Increment(atomic::alias_cast64(addend)); }
	RED_FORCE_INLINE Uint64		Decrement(Uint64 volatile* addend) { return OSAPI::AtomicOps64::Decrement(atomic::alias_cast64(addend)); }
	RED_FORCE_INLINE Uint64		Exchange(Uint64 volatile* target, Uint64 value) { return OSAPI::AtomicOps64::Exchange(atomic::alias_cast64(target), value); }
	RED_FORCE_INLINE Uint64		CompareExchange(Uint64 volatile* destination, Uint64 exchange, Uint64 comparand) { return OSAPI::AtomicOps64::CompareExchange(atomic::alias_cast64(destination), exchange, comparand); }
	RED_FORCE_INLINE Uint64		ExchangeAdd(Uint64 volatile* addend, Uint64 value) { return OSAPI::AtomicOps64::ExchangeAdd(atomic::alias_cast64(addend), value); }
	RED_FORCE_INLINE Uint64		Or(Uint64 volatile* destination, Uint64 value) { return OSAPI::AtomicOps64::Or(atomic::alias_cast64(destination), value); }
	RED_FORCE_INLINE Uint64		And(Uint64 volatile* destination, Uint64 value) { return OSAPI::AtomicOps64::And(atomic::alias_cast64(destination), value); }
	RED_FORCE_INLINE Uint64		FetchValue(Uint64 volatile* destination) { return OSAPI::AtomicOps64::FetchValue(atomic::alias_cast64(destination)); }

	RED_FORCE_INLINE Int64		Increment(Int64 volatile* addend) { return OSAPI::AtomicOps64::Increment(atomic::alias_cast64(addend)); }
	RED_FORCE_INLINE Int64		Decrement(Int64 volatile* addend) { return OSAPI::AtomicOps64::Decrement(atomic::alias_cast64(addend)); }
	RED_FORCE_INLINE Int64		Exchange(Int64 volatile* target, Int64 value) { return OSAPI::AtomicOps64::Exchange(atomic::alias_cast64(target), value); }
	RED_FORCE_INLINE Int64		CompareExchange(Int64 volatile* destination, Int64 exchange, Int64 comparand) { return OSAPI::AtomicOps64::CompareExchange(atomic::alias_cast64(destination), exchange, comparand); }
	RED_FORCE_INLINE Int64		ExchangeAdd(Int64 volatile* addend, Int64 value) { return OSAPI::AtomicOps64::ExchangeAdd(atomic::alias_cast64(addend), value); }
	RED_FORCE_INLINE Int64		Or(Int64 volatile* destination, Int64 value) { return OSAPI::AtomicOps64::Or(atomic::alias_cast64(destination), value); }
	RED_FORCE_INLINE Int64		And(Int64 volatile* destination, Int64 value) { return OSAPI::AtomicOps64::And(atomic::alias_cast64(destination), value); }
	RED_FORCE_INLINE Int64		FetchValue(Int64 volatile* destination) { return OSAPI::AtomicOps64::FetchValue(atomic::alias_cast64(destination)); }
}

namespace red {

	//////////////////////////////////////////////////////////////////////////
	// Generic interlocked operation type wrapper. Only 32 and 64 bit integral types supported.
	template< typename T >
	class Atomic : protected OSAPI::AtomicIntBase< sizeof(T) >
	{	
		REDTHR_NOCOPY_CLASS( Atomic );

	private:
		typedef OSAPI::AtomicIntBase< sizeof(T) > Base;
		typedef typename Base::TAtomic TAtomic;
		typedef typename Base::AtomicOps AtomicOps;

	private:
		mutable TAtomic	m_target;

	public:
		RED_FORCE_INLINE Atomic( T value = T() );
		
		// The functions generally mimic the Win32 Interlocked* familiy of functions, in terms
		// of return types and parameter order.

		//! performs atomic "target += 1; return target; ( PRE-incremented )"
		RED_FORCE_INLINE T Increment();

		//! performs atomic "target -= 1; return target; ( PRE-decremented )"
		RED_FORCE_INLINE T Decrement();
		
		//! performs atomic "oldTarget = target; target |= value; return oldTarget"
		RED_FORCE_INLINE T Or( T value );

		//! performs atomic "oldTarget = target; target &= value; return oldTarget"
		RED_FORCE_INLINE T And( T value );

		//! performs atomic "oldTarget = target; target = value; return oldTarget;"
		RED_FORCE_INLINE T Exchange( T value );

		//! performs atomic "oldTarget = target; if ( target == comparand) target = exchange; return oldTarget;"
		RED_FORCE_INLINE T CompareExchange( T exchange, T comparand );

		//!< performs atomic "oldTarget = target; target += value; return oldTarget;"
		RED_FORCE_INLINE T ExchangeAdd( T value );

		//!< performs atomic "target = value;"
		RED_FORCE_INLINE void SetValue( T value );

		//!< performs atomic "return target;"
		RED_FORCE_INLINE T GetValue() const;

		//! performs atomic "oldTarget = target; target += 1; return oldTarget; ( POST-incremented )
		RED_FORCE_INLINE T PostIncrement();

		//! performs atomic "oldTarget = target; target -= 1; return oldTarget; ( POST-decremented )"
		RED_FORCE_INLINE T PostDecrement();
	};

	//////////////////////////////////////////////////////////////////////////
	// Generic pointer interlocked operation type wrapper.
	template< typename T >
	class Atomic< T* > : protected OSAPI::AtomicPtrBase
	{
		REDTHR_NOCOPY_CLASS( Atomic );

	private:
		typedef OSAPI::AtomicPtrBase Base;
		typedef Base::TAtomicPtr TAtomicPtr;
		typedef Base::AtomicOps AtomicOps;

	private:
		mutable TAtomicPtr	m_target;

	public:
		RED_FORCE_INLINE Atomic( T* value = nullptr );

		RED_FORCE_INLINE T*			Exchange( T* value );
		RED_FORCE_INLINE T*			CompareExchange( T* exchange, T* comparand );
		RED_FORCE_INLINE void		SetValue( T* value );
		RED_FORCE_INLINE T*			GetValue() const;
	};

	//////////////////////////////////////////////////////////////////////////
	// Bool interlocked operation type wrapper
	template<>
	class Atomic< Bool > : protected OSAPI::AtomicIntBase< sizeof(Int32) >
	{
		REDTHR_NOCOPY_CLASS( Atomic );

	private:
		typedef OSAPI::AtomicIntBase< sizeof(Int32) > Base;
		typedef Base::TAtomic TAtomic;
		typedef Base::AtomicOps AtomicOps;

	private:
		mutable TAtomic	m_target;

	public:
		RED_FORCE_INLINE Atomic( Bool value = false );

		RED_FORCE_INLINE Bool Exchange( Bool value );
		RED_FORCE_INLINE Bool CompareExchange( Bool exchange, Bool comparand );
		RED_FORCE_INLINE void SetValue( Bool value );
		RED_FORCE_INLINE Bool GetValue() const;
	};

#ifndef RED_CONFIGURATION_FINAL

	REDSYSTEM_API_TEMPLATE template class REDSYSTEM_API Atomic<Int32>;
	REDSYSTEM_API_TEMPLATE template class REDSYSTEM_API Atomic<Uint32>;
	REDSYSTEM_API_TEMPLATE template class REDSYSTEM_API Atomic<Int64>;
	REDSYSTEM_API_TEMPLATE template class REDSYSTEM_API Atomic<Uint64>;

#endif // RED_CONFIGURATION_FINAL

} // namespace red

#include "redThreadsAtomic.inl"

#endif // RED_THREADS_ATOMIC_H