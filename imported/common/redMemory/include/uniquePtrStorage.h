/**
* Copyright (c) 2016 CD Projekt Red. All Rights Reserved.
*/

#ifndef _RED_MEMORY_UNIQUE_PTR_STORAGE_H_
#define _RED_MEMORY_UNIQUE_PTR_STORAGE_H_

#include "pool.h"

namespace red
{
namespace memory
{
	class DefaulUniquePtrDestructor;

	template< typename T, typename DestructorFunctor >
	class UniquePtrStorage_EmptyDestructor;
	
	template< typename T, typename DestructorFunctor >
	class UniquePtrStorage_AggregateDestructor;

	template< typename T, typename PoolType >
	class UniquePtrStorage_PoolOverrideDestructor;

	template< typename T, typename DestructorFunctor >
	struct UniquePtrStorage_DestructorResolver
	{
		typedef typename std::conditional< 
			std::is_empty< DestructorFunctor >::value,
			UniquePtrStorage_EmptyDestructor< T, DestructorFunctor >,
			UniquePtrStorage_AggregateDestructor< T, DestructorFunctor >
		>::type Type;
	};

	template< typename T, typename Destructor >
	struct UniquePtrStorage_Resolver
	{
		typedef typename std::conditional< 
			std::is_base_of< Pool, Destructor >::value,
			UniquePtrStorage_PoolOverrideDestructor< T, Destructor >,
			typename UniquePtrStorage_DestructorResolver< T, Destructor >::Type
		>::type Type;
	};

	class DefaulUniquePtrDestructor
	{
	public:

		template< typename T >
		void operator()( T * ptr ) const;
	};

	template< typename T, typename DestructorFunctor >
	class UniquePtrStorage_EmptyDestructor : private DestructorFunctor
	{
	public:

		UniquePtrStorage_EmptyDestructor();
		explicit UniquePtrStorage_EmptyDestructor( T * ptr );
		UniquePtrStorage_EmptyDestructor( T * ptr, const DestructorFunctor & functor );
		UniquePtrStorage_EmptyDestructor( T * ptr, DestructorFunctor && functor );
		UniquePtrStorage_EmptyDestructor( UniquePtrStorage_EmptyDestructor && storage );
		~UniquePtrStorage_EmptyDestructor();

		T * Get() const;
		T * Release();
		void Swap( UniquePtrStorage_EmptyDestructor & storage );
		DestructorFunctor & GetDestructor();
		const DestructorFunctor & GetDestructor() const;

	private:

		T * m_pointer;
	};

	template< typename T, typename DestructorFunctor >
	class UniquePtrStorage_AggregateDestructor
	{
	public:
		UniquePtrStorage_AggregateDestructor();
		explicit UniquePtrStorage_AggregateDestructor( T * ptr );
		UniquePtrStorage_AggregateDestructor( T * ptr, const DestructorFunctor & functor );
		UniquePtrStorage_AggregateDestructor( T * ptr, DestructorFunctor && functor );
		UniquePtrStorage_AggregateDestructor( UniquePtrStorage_AggregateDestructor && storage );
		~UniquePtrStorage_AggregateDestructor();

		T * Get() const;
		T * Release();
		void Swap( UniquePtrStorage_AggregateDestructor & storage );
		DestructorFunctor & GetDestructor();
		const DestructorFunctor & GetDestructor() const;

	private:

		T * m_pointer;
		DestructorFunctor m_destructor;
	};

	template< typename T, typename PoolType >
	class UniquePtrStorage_PoolOverrideDestructor
	{
	public:
		UniquePtrStorage_PoolOverrideDestructor();
		explicit UniquePtrStorage_PoolOverrideDestructor( T * ptr );
		UniquePtrStorage_PoolOverrideDestructor( T * ptr, const PoolType & pool );
		UniquePtrStorage_PoolOverrideDestructor( T * ptr, PoolType && pool );
		UniquePtrStorage_PoolOverrideDestructor( UniquePtrStorage_PoolOverrideDestructor && storage );
		~UniquePtrStorage_PoolOverrideDestructor();

		T * Get() const;
		T * Release();
		void Swap( UniquePtrStorage_PoolOverrideDestructor & storage );
		PoolType & GetDestructor();
		const PoolType & GetDestructor() const;

	private:

		T * m_pointer;
	};
}
}

#include "uniquePtrStorage.hpp"

#endif
