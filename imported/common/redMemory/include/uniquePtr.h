/**
* Copyright (c) 2014 CD Projekt Red. All Rights Reserved.
*/

#ifndef _RED_MEMORY_UNIQUE_PTR_H_
#define _RED_MEMORY_UNIQUE_PTR_H_

#include "uniquePtrStorage.h"

namespace red
{
	template< typename PtrType, typename DeleterType = memory::DefaulUniquePtrDestructor >
	class UniquePtr
	{
	public:
	
		UniquePtr();
		UniquePtr( std::nullptr_t );
		explicit UniquePtr( PtrType * pointer );
		UniquePtr( PtrType * pointer, const DeleterType & destroyer );
		UniquePtr( PtrType * pointer, DeleterType && destroyer );
		UniquePtr( UniquePtr && moveFrom ); 

		UniquePtr( const UniquePtr& ) = delete;
		UniquePtr& operator=( const UniquePtr& ) = delete;

		template< typename U, typename V > 
		UniquePtr( UniquePtr< U, V > && moveFrom );
		~UniquePtr();
		
		PtrType * Get() const;

		PtrType & operator*() const;
		PtrType * operator->() const;
	
		PtrType * ReleaseOwnership();
	
		void Reset( PtrType * pointer = nullptr );
	
		void Swap( UniquePtr & swapWith );
		
		UniquePtr& operator=( UniquePtr&& moveFrom );

		template<typename U, typename V > 
		UniquePtr& operator=( UniquePtr< U, V > && moveFrom );

		struct BoolConversion{ int valid; };
		typedef int BoolConversion::*bool_operator;

		operator bool_operator () const;
		bool operator!() const;

		DeleterType & GetDeleter();
		const DeleterType & GetDeleter() const;

		template< typename... Args >
		static UniquePtr Create( Args && ... args );

	private:

		// The idea is that if the Destructor provided is empty, the size of UniquePtr is still the size of a single pointer.
		typedef typename memory::UniquePtrStorage_Resolver< PtrType, DeleterType >::Type StorageType;
		StorageType m_storage;
	};

	template< typename T, typename... Args >
	UniquePtr< T > CreateUniquePtr( Args && ... args );
	
	template< typename T, typename PoolType, typename... Args >
	UniquePtr< T, PoolType > CreateUniquePtr( Args && ... args );

	template< typename T >
	UniquePtr< T > MakeUniquePtr( T* ptr );
	
	template< typename LeftType, typename RightType, typename DeleterType >
	bool operator==( const UniquePtr< LeftType, DeleterType> & leftPtr, const UniquePtr< RightType, DeleterType> & rightPtr );

	template< typename LeftType, typename RightType, typename DeleterType >
	bool operator!=( const UniquePtr< LeftType, DeleterType > & leftPtr, const UniquePtr< RightType, DeleterType > & rightPtr );

	template< typename LeftType, typename RightType, typename DeleterType >
	bool operator<( const UniquePtr< LeftType, DeleterType > & leftPtr, const UniquePtr< RightType, DeleterType > & rightPtr );
}

#include "uniquePtr.hpp"

#endif
