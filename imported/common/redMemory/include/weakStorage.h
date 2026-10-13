/**
* Copyright (c) 2014 CD Projekt Red. All Rights Reserved.
*/

#ifndef _RED_MEMORY_WEAK_STORAGE_H_
#define _RED_MEMORY_WEAK_STORAGE_H_

namespace red
{
	template< typename T, typename Storage, typename PoolType >
	class SharedStorage;

	template< typename T, typename Storage, typename PoolType = void >
	class WeakStorage : public Storage
	{
	public:
		
		typedef Storage ParentType;
		typedef Storage StorageType;

		WeakStorage();
		WeakStorage( const WeakStorage & pointer );
		WeakStorage( WeakStorage && pointer );

		template< typename U >
		WeakStorage( const WeakStorage< U, Storage, PoolType > & pointer );

		template< typename U >
		WeakStorage( WeakStorage< U, Storage, PoolType > && pointer );

		template< typename U  >
		WeakStorage( const SharedStorage< U, Storage, PoolType > & pointer );
		
		~WeakStorage();

		WeakStorage & operator=( const WeakStorage & pointer );
		WeakStorage & operator=( WeakStorage && pointer );

		template< typename U >
		WeakStorage & operator=( const WeakStorage< U, Storage, PoolType > & pointer );

		template< typename U >
		WeakStorage & operator=( WeakStorage< U, Storage, PoolType > && pointer );

		template< typename U >
		WeakStorage & operator=( const SharedStorage< U, Storage, PoolType > & pointer );

		SharedStorage< T, Storage, PoolType > Lock() const;

		bool Expired() const;
		void Reset();
		void Swap( WeakStorage & swapWith );

		Int32 GetRefCount() const;
		Int32 GetWeakRefCount() const; 

		template < typename LeftType, typename RightType, typename Store >
		friend bool operator==( const WeakStorage< LeftType, Store > & left, const WeakStorage< RightType, Store > & right );

		template < typename LeftType, typename RightType, typename Store >
		friend bool operator!=( const WeakStorage< LeftType, Store > & left, const WeakStorage< RightType, Store > & right );

		// Other Comparison operators left unimplemented because WeakPtr & Co
		// are not meant to be used as keys for associative containers
		//template < typename LeftType, typename RightType, typename Storage >
		//bool operator<( const WeakStorage< LeftType, Storage > & left, const WeakStorage< RightType, Storage > & right );
	};

}

#include "weakStorage.hpp"

#endif 
