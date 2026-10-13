/**
 * Copyright (c) 2008 CD Projekt Red. All Rights Reserved.
 */

#pragma once

#include "../../redSystem/include/hash.h"

namespace red {

	//////////////////////////////////////////////////////////////////////////
	// GetHash

	// Default implementation
	template< typename K > RED_INLINE THash32 GetHash( const K& key ) { return key.CalcHash(); }

	RED_INLINE THash32 GetHash( const Bool key ) { return key ? 1 : 0; }

	RED_INLINE THash32 GetHash( const Int8 key ) { return key; }
	RED_INLINE THash32 GetHash( const Uint8 key ) { return key; }

	RED_INLINE THash32 GetHash( const Char key ) { return key; }
	RED_INLINE THash32 GetHash( const Int16 key ) { return key; }
	RED_INLINE THash32 GetHash( const Uint16 key ) { return key; }

	RED_INLINE THash32 GetHash( const Int32 key ) { return key; }
	RED_INLINE THash32 GetHash( const Uint32 key ) { return key; }

	RED_INLINE THash32 GetHash( const Int64 key ) { return CalculateHash32( &key, sizeof( Int64 ) ); }
	RED_INLINE THash32 GetHash( const Uint64 key ) { return CalculateHash32( &key, sizeof( Uint64 ) ); }

	RED_INLINE THash32 GetHash( const Float key ) { return CalculateHash32( &key, sizeof( Float ) ); }
	RED_INLINE THash32 GetHash( const Double key ) { return CalculateHash32( &key, sizeof( Double ) ); }

	template < typename T1, typename T2 >
	RED_INLINE THash32 GetHash( const std::pair< T1, T2 >& key )
	{
		return GetHash( key.first ) ^ GetHash( key.second );
	}

	//////////////////////////////////////////////////////////////////////////
	// DefaultHashFunc

	template< typename K, Bool passByValue = (sizeof( K ) <= sizeof( int )) >
	class DefaultHashFunc
	{
	};

	// Default 'default hash function'
	template< typename K >
	class DefaultHashFunc< K, false >
	{
	public:
		static RED_INLINE THash32 GetHash( const K& key ) { return red::GetHash( key ); }
	};

	// Specialization of the default hash function for types smaller than native word size (e.g. 8 bytes on 64 bits)
	template< typename K >
	class DefaultHashFunc< K, true >
	{
	public:
		static RED_INLINE THash32 GetHash( const K key ) { return red::GetHash( key ); }
	};

	// Specialization of the default hash function for pointers (had to specialize for both passValue being true and false; is there a way to avoid this?)
	template< typename K >
	class DefaultHashFunc< K*, false >
	{
	public:
		static RED_INLINE THash32 GetHash( const K* key ) { return red::CalculatePtrHash32( key ); }
	};

	template< typename K >
	class DefaultHashFunc< K*, true >
	{
	public:
		static RED_INLINE THash32 GetHash( const K* key ) { return red::CalculatePtrHash32( key ); }
	};

	template< typename, typename Storage, typename PoolType >
	class SharedStorage;

	// Specialization of the default hash function for SharedStorage
	template< typename K, typename U, typename V >
	class DefaultHashFunc< red::SharedStorage< K, U, V >, false >
	{
	public:
		static RED_INLINE THash32 GetHash( const red::SharedStorage< K, U, V > key ) { return red::CalculatePtrHash32( key.Get() ); }
	};

	template< typename K, typename U, typename V >
	class DefaultHashFunc< red::SharedStorage< K, U, V >, true >
	{
	public:
		static RED_INLINE THash32 GetHash( const red::SharedStorage< K, U, V >& key ) { return red::CalculatePtrHash32( key.Get() ); }
	};

	//////////////////////////////////////////////////////////////////////////
	// HashPolicy

	template< typename T >
	struct DefaultHashPolicy
	{
		typedef DefaultHashFunc< T >				HashFunc;
		typedef alg::DefaultEqualFunc< T >			EqualFunc;
	};

	template < typename THashFunc, typename TEqualFunc >
	struct HashPolicy
	{
		typedef	THashFunc							HashFunc;
		typedef TEqualFunc							EqualFunc;
	};

	template < typename THashFunc, typename T >
	struct HashPolicyDefaultEqual
	{
		typedef	THashFunc							HashFunc;
		typedef red::alg::DefaultEqualFunc< T >		EqualFunc;
	};

	template < typename TEqualFunc, typename T >
	struct HashPolicyDefaultHash
	{
		typedef	red::DefaultHashFunc< T >			HashFunc;
		typedef TEqualFunc							EqualFunc;
	};

} // red