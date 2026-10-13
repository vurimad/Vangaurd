/**
* Copyright (c) 2014 CD Projekt Red. All Rights Reserved.
*/

#ifndef _RED_MEMORY_SHARED_STORAGE_H_
#define _RED_MEMORY_SHARED_STORAGE_H_

#include "uniquePtr.h"

namespace red
{
	template< typename, typename Storage, typename PoolType >
	class WeakStorage;

	template< typename T, typename Storage, typename PoolType = void >
	class SharedStorage : public Storage
	{
	public:

		typedef Storage ParentType;
		typedef Storage StorageType;
		typedef T * PtrType;
		typedef T & RefType;
	
		SharedStorage();
		SharedStorage( std::nullptr_t );
		SharedStorage( const SharedStorage & copyFrom );
		SharedStorage( SharedStorage && rvalue );

		template< typename U >
		explicit SharedStorage( U * pointer );	
		
		template< typename U >
		SharedStorage( const SharedStorage< U, Storage, PoolType > & copyFrom );

		template< typename U >
		SharedStorage( SharedStorage< U, Storage, PoolType > && rvalue );

		template< typename U >
		SharedStorage( const WeakStorage< U, Storage, PoolType > & copyFrom );

		template< typename U, typename P = PoolType,
			typename std::enable_if<
				std::is_same< P, void >::value
			>::type* = nullptr >
		SharedStorage( UniquePtr< U > && rvalue );

		template< typename U, typename P = PoolType,
			typename std::enable_if<
				!std::is_same< P, void >::value
			>::type* = nullptr >
		SharedStorage( UniquePtr< U, PoolType > && rvalue );

		~SharedStorage();

		PtrType Get() const;

		PtrType operator->() const;
		RefType operator*() const;

		void Reset();
		void Reset( PtrType pointer );

		template< typename U >
		void Reset( U * pointer );

		void Swap( SharedStorage & swapWith );

		Int32 GetRefCount() const;
		Int32 GetWeakRefCount() const;
		
		SharedStorage & operator=( const SharedStorage & copyFrom );
		SharedStorage & operator=( SharedStorage && rvalue );
		
		template< typename U >
		SharedStorage & operator=( const SharedStorage< U, Storage, PoolType >  & copyFrom );

		template< typename U >
		SharedStorage & operator=( SharedStorage< U, Storage, PoolType > && rvalue );

		template< typename U >
		SharedStorage & operator=( UniquePtr< U > && rvalue );
			
		explicit operator Bool() const;
		Bool operator!() const;

		template< typename... Args  >
		static SharedStorage Create( Args && ... args );
	
	private:

		template< typename U = PoolType >
 		void Destroy( typename std::enable_if< std::is_same< U, void >::value, U >::type* = 0 );
		
		template< typename U = PoolType >
		void Destroy( typename std::enable_if< std::is_base_of< red::memory::Pool, U >::value, U >::type* = 0 );
	};  

	template< typename LeftType, typename LeftStorage, typename LeftPool, typename RightType, typename RightStorage, typename RightPool >
	bool operator==( const SharedStorage< LeftType, LeftStorage, LeftPool > & leftPtr, const SharedStorage< RightType, RightStorage, RightPool > & rightPtr );

	template< typename LeftType, typename LeftStorage, typename LeftPool, typename RightType, typename RightStorage, typename RightPool >
	bool operator!=( const SharedStorage< LeftType, LeftStorage, LeftPool > & leftPtr, const SharedStorage< RightType, RightStorage, RightPool > & rightPtr );

	template< typename LeftType, typename LeftStorage, typename LeftPool, typename RightType, typename RightStorage, typename RightPool >
	bool operator<( const SharedStorage< LeftType, LeftStorage, LeftPool > & leftPtr, const SharedStorage< RightType, RightStorage, RightPool > & rightPtr );

	template< typename LeftType, typename LeftStorage, typename LeftPool >
	bool operator==( const SharedStorage< LeftType, LeftStorage, LeftPool > & leftPtr, std::nullptr_t );

	template< typename LeftType, typename LeftStorage, typename LeftPool >
	bool operator!=( const SharedStorage< LeftType, LeftStorage, LeftPool > & leftPtr, std::nullptr_t );

	template< typename RightType, typename RightStorage, typename RightPool >
	bool operator==( std::nullptr_t, const SharedStorage< RightType, RightStorage, RightPool > & rightPtr );

	template< typename RightType, typename RightStorage, typename RightPool >
	bool operator!=( std::nullptr_t, const SharedStorage< RightType, RightStorage, RightPool > & rightPtr );
}

#include "sharedStorage.hpp"

#endif
