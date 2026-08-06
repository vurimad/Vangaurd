/**
 * Copyright (c) 2017 CD Projekt Red. All Rights Reserved.
 */

#ifndef _RED_MEMORY_ALLOCATOR_METRICS_SERIALIZER_HPP_
#define _RED_MEMORY_ALLOCATOR_METRICS_SERIALIZER_HPP_

#include <type_traits>
#include "../src/assert.h"

namespace red
{
namespace memory
{
	class Serializer;

	namespace detail
	{

		// it is temporary code just to continue compilation even when allocator does not have SerializeMetrics method.
		template< typename T, typename F >
		struct HasSerializeMetricsMethod
		{
			static_assert( std::integral_constant< T, false >::value, "Second template parameter needs to be of function type" );
		};

		template< typename T, typename R, typename... Args >
		struct HasSerializeMetricsMethod< T, R ( Args... ) >
		{
		private:
			template< typename U >
			static auto Check( U* ) -> typename std::is_same< decltype( std::declval< U >().SerializeMetrics( std::declval< Args >()... ) ), R >::type;

			template< typename >
			static std::false_type Check( ... );

			typedef decltype( Check< T >( 0 ) ) Type;

		public:
			static constexpr bool Value = Type::value;
		};

		template< bool HasSerializeMetricsMethod = false >
		struct SerializeMetricsExecutor
		{
			template< typename AllocatorType >
			static void Execute( void* , Serializer &  )
			{
			}
		};

		template<>
		struct SerializeMetricsExecutor< true >
		{
			template< typename AllocatorType >
			static void Execute( void* allocator, Serializer & serializer )
			{
				RED_MEMORY_ASSERT( allocator != nullptr, "Given allocator does not exist." );
				auto* typedAllocator = static_cast< AllocatorType* >( allocator );
				typedAllocator->SerializeMetrics( serializer );
			}
		};
	}

	template< typename AllocatorType >
	void SerializeAllocatorMetrics( void * allocator, Serializer & serializer )
	{
		RED_MEMORY_ASSERT( allocator != nullptr, "Given allocator does not exist." );
		detail::SerializeMetricsExecutor< detail::HasSerializeMetricsMethod< AllocatorType, void( Serializer & ) >::Value >::template Execute< AllocatorType >( allocator, serializer );
	}
}
}

#endif