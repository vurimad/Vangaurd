/**
* Copyright (c) 2013 CD Projekt Red. All Rights Reserved.
*/

namespace red {

//////////////////////////////////////////////////////////////////////////
// General integral template
//////////////////////////////////////////////////////////////////////////

template< typename T > inline Atomic< T >::Atomic( T value /*=T()*/ )
	: m_target( static_cast< TAtomic > ( value ) )
{
}

//template< typename T > inline Atomic< T >::Atomic( red::EUndefined  )
//{
//	// No init
//}

template< typename T > inline T Atomic< T >::Increment()
{
	return static_cast<T>( AtomicOps::Increment( &m_target ) );
}

template< typename T > inline T Atomic< T >::Decrement()
{
	return static_cast<T>( AtomicOps::Decrement( &m_target ) );
}

template< typename T > inline T Atomic< T >::PostIncrement()
{
	return static_cast<T>( AtomicOps::ExchangeAdd( &m_target, TAtomic(1) ) );
}

template< typename T > inline T Atomic< T >::PostDecrement()
{
	return static_cast<T>( AtomicOps::ExchangeAdd( &m_target, TAtomic(-1) ) );
}

template< typename T > inline T	Atomic< T >::Or( T value )
{
	return static_cast<T>( AtomicOps::Or( &m_target, value ) );
}

template< typename T > inline T Atomic< T >::And( T value )
{
	return static_cast<T>( AtomicOps::And( &m_target, value ) );
}

template< typename T > inline T Atomic< T >::Exchange( T value )
{
	return static_cast< T >( AtomicOps::Exchange( &m_target, static_cast< TAtomic >( value ) ) );
}

template< typename T > inline T Atomic< T >::CompareExchange( T exchange, T comparand )
{
	return static_cast< T >( AtomicOps::CompareExchange( &m_target, static_cast< TAtomic >( exchange ),
																	 static_cast< TAtomic >( comparand ) ) );
}

template< typename T > inline T Atomic< T >::ExchangeAdd( T value )
{
	return static_cast<T>( AtomicOps::ExchangeAdd( &m_target, value ) );
}

template< typename T > inline void Atomic< T >::SetValue( T value )
{
	(void)AtomicOps::Exchange( &m_target, static_cast< TAtomic >( value ) );
}

template< typename T > inline T Atomic< T >::GetValue() const
{
	return static_cast< T >( AtomicOps::FetchValue( &m_target ) );
}

//////////////////////////////////////////////////////////////////////////
// Boolean template specialization
//////////////////////////////////////////////////////////////////////////

inline Atomic< Bool >::Atomic( Bool value /*= false */ )
	: m_target( value )
{
}

//inline Atomic< Bool >::Atomic( red::EUndefined )
//{
//	// No init
//}

inline Bool	Atomic< Bool >::Exchange( Bool value )
{
	return AtomicOps::Exchange( &m_target, value ? TAtomic(1) : TAtomic(0) ) != TAtomic(0);
}

inline Bool Atomic< Bool >::CompareExchange( Bool exchange, Bool comparand )
{
	return AtomicOps::CompareExchange( &m_target, exchange ? TAtomic(1) : TAtomic(0), comparand ? TAtomic(1) : TAtomic(0) ) != TAtomic(0);
}

inline void Atomic< Bool >::SetValue( Bool value )
{
	(void)AtomicOps::Exchange( &m_target, value ? TAtomic(1) : TAtomic(0) );
}

inline Bool Atomic< Bool >::GetValue() const
{
	return AtomicOps::FetchValue( &m_target) != TAtomic(0);
}

//////////////////////////////////////////////////////////////////////////
// Pointer template specialization
//////////////////////////////////////////////////////////////////////////

template< typename T > inline Atomic< T* >::Atomic( T* value /*=nullptr*/ )
	: m_target( const_cast< void* >( static_cast< const void* >( value ) ) )
{
}

//template< typename T > inline Atomic< T* >::Atomic( red::EUndefined )
//{
//	// No init
//}

template< typename T > inline T* Atomic< T* >::Exchange( T* value )
{
	const TAtomicPtr result = AtomicOps::Exchange( &m_target, atomic::alias_cast< T*, TAtomicPtr >(value) );
	return atomic::alias_cast< TAtomicPtr, T* >(result);
}

template< typename T > inline T* Atomic< T* >::CompareExchange( T* exchange, T* comparand )
{
	const TAtomicPtr result = AtomicOps::CompareExchange( &m_target, atomic::alias_cast< T*, TAtomicPtr >(exchange),
																   atomic::alias_cast< T*, TAtomicPtr >(comparand) );
	return atomic::alias_cast< TAtomicPtr, T* >(result);
}

template< typename T > inline void Atomic< T* >::SetValue( T* value )
{
	(void)AtomicOps::Exchange( &m_target, atomic::alias_cast< T*, TAtomicPtr >( value ) );
}

template< typename T > inline T* Atomic< T* >::GetValue() const
{
	TAtomicPtr result = AtomicOps::FetchValue( &m_target);
	return atomic::alias_cast< TAtomicPtr, T* >( result );
}

} // namespace red