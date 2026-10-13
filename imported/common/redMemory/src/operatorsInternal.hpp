/**
 * Copyright (c) 2016 CD Projekt Red. All Rights Reserved.
 */

#ifndef _RED_MEMORY_OPERATORS_INTERNAL_HPP_
#define _RED_MEMORY_OPERATORS_INTERNAL_HPP_

#include "../include/poolUtils.h"
#include "../include/utils.h"
#include "../include/systemOOMHandler.h"
#include "../include/block.h"
#include "pool.h"
#include "functions.h"
#include "objectUtils.h"

namespace red
{
namespace memory
{
namespace internal
{
	struct ArrayInitializer;
	struct TrivialArrayInitializer;

	struct ArrayDestructor;
	struct TrivialArrayDestructor;

	struct StaticInvoker;
	struct RuntimeInvoker;

	// Used from the forms: RED_NEW( MemoryPool_Type, Object ) etc...
	struct AllocateStaticPool;	
	struct AllocateAlignedStaticPool;

	// Used from the forms: FixedSizeAllocator allocator; RED_NEW( allocator, Object ) etc...
	struct AllocateRuntimeProxy;
	struct AllocateAlignedRuntimeProxy;

	struct AllocateStaticAllocator;
	struct AllocateAlignedStaticAllocator;

	template< typename T >
	struct ProxyTraits
	{
		typedef typename std::remove_cv< T >::type Type;
		typedef typename std::is_base_of< Pool, Type >::type IsPoolType;
		typedef typename std::is_same< Pool, Type >::type IsRuntimePoolType;
		typedef typename std::integral_constant< bool, !IsPoolType::value || IsRuntimePoolType::value >::type IsProxyType;
		typedef typename std::integral_constant< bool, !IsPoolType::value && !IsRuntimePoolType::value >::type IsAllocatorType;
	};

	template< typename T, bool NeedAligned >
	struct AllocateInvokerResolver
	{
		typedef ProxyTraits< T > Traits;

		typedef typename std::conditional< NeedAligned, AllocateAlignedStaticPool, AllocateStaticPool >::type StaticInvokerType;
		typedef typename std::conditional< NeedAligned, AllocateAlignedRuntimeProxy, AllocateRuntimeProxy >::type ProxyInvokerType;
		typedef typename std::conditional< NeedAligned, AllocateAlignedStaticAllocator, AllocateStaticAllocator >::type AllocatorInvokerType;

		typedef typename std::conditional
			< 
				Traits::IsProxyType::value, 
				typename std::conditional< Traits::IsAllocatorType::value, AllocatorInvokerType, ProxyInvokerType >::type,
				StaticInvokerType 
			>::type Type;
	};

	template< typename T >
	struct InvokerResolver
	{
		typedef ProxyTraits< T > Traits;
		
		typedef typename std::conditional
			< 
				Traits::IsProxyType::value, 
				RuntimeInvoker, 
				StaticInvoker 
			>::type Type;
	};

	template< typename T >
	struct ArrayInitializerResolver
	{
		typedef typename std::conditional
			< 
				std::is_trivially_constructible< T >::value,
				TrivialArrayInitializer,
				ArrayInitializer
			>::type Type;
	};

	template< typename T >
	struct ArrayDestructorResolver
	{
		typedef typename std::conditional
			<
				std::is_trivially_destructible< T >::value,
				TrivialArrayDestructor,
				ArrayDestructor
			>::type Type;
	};

	template< typename T, Bool >
	struct DeleteResolver
	{
		typedef typename PoolResolver< T >::PoolType PoolType;

		static void Delete( T* ptr, u32 disabledHooks )
		{
			ptr->~T();
			Free< PoolType >( ptr, disabledHooks );
		}

		static void Delete( T* ptr, u32 count, u32 disabledHooks )
		{
			typedef typename ArrayDestructorResolver< T >::Type ArrayDestructorType;
			ArrayDestructorType::Apply( ptr, count );
			Free< PoolType >( ptr, disabledHooks );
		}
	};

	template< typename T >
	struct DeleteResolver< T, true >
	{
		static void Delete( T* ptr, u32 disabledHooks )
		{
			const red::memory::Pool& pool = ptr->GetMemoryPool();
			ptr->~T();
			Free( pool, ptr, disabledHooks );
		}

		static void Delete( T* ptr, u32 count, u32 disabledHooks )
		{
			typedef typename ArrayDestructorResolver< T >::Type ArrayDestructorType;
			const red::memory::Pool& pool = ptr->GetMemoryPool();
			ArrayDestructorType::Apply( ptr, count );
			Free( pool, ptr, disabledHooks );
		}
	};

	struct AllocateStaticPool
	{
		template< typename T >
		RED_MEMORY_INLINE static void * Invoke( T & , u32 size, u32, u32 disabledHooks )
		{
			return Allocate< T >( size, disabledHooks );
		}

		template< typename T >
		RED_MEMORY_INLINE static void * Invoke( u32 size, u32, u32 disabledHooks )
		{
			return Allocate< T >( size, disabledHooks );
		}
	};

	struct AllocateAlignedStaticPool
	{
		template< typename T >
		RED_MEMORY_INLINE static void * Invoke( T & , u32 size, u32 alignment, u32 disabledHooks )
		{
			return AllocateAligned< T >( size, alignment, disabledHooks );
		}

		template< typename T >
		RED_MEMORY_INLINE static void * Invoke( u32 size, u32 alignment, u32 disabledHooks )
		{
			return AllocateAligned< T >( size, alignment, disabledHooks );
		}
	};

	struct AllocateRuntimeProxy
	{
		template< typename T >
		RED_MEMORY_INLINE static void * Invoke( T & proxy, u32 size, u32, u32 disabledHooks )
		{
			return Allocate( proxy, size, disabledHooks );
		}
	};

	struct AllocateStaticAllocator
	{
		template< typename T >
		RED_MEMORY_INLINE static void * Invoke( T & proxy, u32 size, u32, u32 disabledHooks )
		{
			void* ptr = Allocate( proxy, size, disabledHooks );

			if ( !ptr )
			{
				HandleAllocateFailure( T::TypeId, std::addressof( proxy ), size, T::DefaultAlignmentType::value );
			}

			return ptr;
		}
	};

	struct AllocateAlignedRuntimeProxy
	{
		template< typename T >
		RED_MEMORY_INLINE static void * Invoke( T & proxy, u32 size, u32 alignment, u32 disabledHooks )
		{
			return AllocateAligned( proxy, size, alignment, disabledHooks );
		}
	};

	struct AllocateAlignedStaticAllocator
	{
		template< typename T >
		RED_MEMORY_INLINE static void * Invoke( T & proxy, u32 size, u32 alignment, u32 disabledHooks )
		{
			void* ptr = AllocateAligned( proxy, size, alignment, disabledHooks );
			if ( !ptr )
			{
				HandleAllocateFailure( T::TypeId, std::addressof( proxy ), size, alignment );
			}

			return ptr;
		}
	};

	struct StaticInvoker
	{
		template< typename T >
		RED_MEMORY_INLINE static void * InvokeAllocate( T & , u32 size, u32, u32 disabledHooks )
		{
			return Allocate< T >( size, disabledHooks );
		}

		template< typename T >
		RED_MEMORY_INLINE static void * InvokeAllocateAligned( T & , u32 size, u32 alignment, u32 disabledHooks )
		{
			return AllocateAligned< T >( size, alignment, disabledHooks );
		}

		template< typename T >
		RED_MEMORY_INLINE static void InvokeFree( T &, const void * block, u32 disabledHooks )
		{
			Free< T >( block, disabledHooks );
		}

		template< typename T >
		RED_MEMORY_INLINE static void * InvokeReallocate( T & , void * block, u32 size, u32 disabledHooks )
		{
			return Reallocate< T >( block, size, disabledHooks );
		}

		template< typename T >
		RED_MEMORY_INLINE static void * InvokeReallocateAligned( T & , void * block, u32 size, u32 alignment, u32 disabledHooks )
		{
			return ReallocateAligned< T >( block, size, alignment, disabledHooks );
		}
	};

	struct RuntimeInvoker
	{
		template< typename T >
		RED_MEMORY_INLINE static void * InvokeAllocate( T & proxy, u32 size, u32, u32 disabledHooks )
		{
			return Allocate( proxy, size, disabledHooks );
		}

		template< typename T >
		RED_MEMORY_INLINE static void * InvokeAllocateAligned( T & proxy, u32 size, u32 alignment, u32 disabledHooks )
		{
			return AllocateAligned( proxy, size, alignment, disabledHooks );
		}

		template< typename T >
		RED_MEMORY_INLINE static void InvokeFree( T & proxy, const void * block, u32 disabledHooks )
		{
			Free( proxy, block, disabledHooks );
		}

		template< typename T >
		RED_MEMORY_INLINE static void * InvokeReallocate( T & proxy, void * block, u32 size, u32 disabledHooks )
		{
			return Reallocate( proxy, block, size, disabledHooks );
		}

		template< typename T >
		RED_MEMORY_INLINE static void * InvokeReallocateAligned( T & proxy, void * block, u32 size, u32 alignment, u32 disabledHooks )
		{
			return ReallocateAligned( proxy, block, size, alignment, disabledHooks );
		}
	};

	struct TrivialArrayInitializer
	{
		template< typename T >
		RED_MEMORY_INLINE static void Apply( T * , u32 )
		{}
	};

	struct ArrayInitializer
	{
		template< typename T >
		RED_MEMORY_INLINE static void Apply( T * buffer, u32 count )
		{
			for( u32 index = 0; index != count; ++index )
			{
				::new ( buffer + index ) T();
			}
		}
	};

	struct TrivialArrayDestructor
	{
		template< typename T >
		RED_MEMORY_INLINE static void Apply( T * /*buffer*/, u32 )
		{}
	};

	struct ArrayDestructor
	{
		template< typename T >
		RED_MEMORY_INLINE static void Apply( T * buffer, u32 count )
		{
			for( u32 index = 0; index != count; ++index )
			{
				T* object = buffer++;
				object->~T();
			}
		}
	};

	template< typename ObjectType >
	RED_MEMORY_INLINE void * NewHelper( u32 disabledHooks )
	{
		static_assert( !HasMemberOperatorNewFunction< ObjectType >::Value, "object can't define it's own operator new." );

		typedef typename PoolResolver< ObjectType >::PoolType	PoolType;
		typedef typename PoolType::DefaultAlignmentType	DefaultAlignmentType;

		typedef typename AllocateInvokerResolver< PoolType, ( __alignof( ObjectType ) > DefaultAlignmentType::value ) >::Type AllocateInvoker;
		
#ifndef RED_MEMORY_ENABLE_DEFAULT_ALLOCATION
		static_assert(!std::is_same< PoolType, PoolDefault >::value, "Allocation from Default Pool are forbidden.");
#endif

		const u32 size = sizeof( ObjectType );
		const u32 alignment = __alignof( ObjectType );

		return AllocateInvoker::template Invoke< PoolType >( size, alignment, disabledHooks );
	}

	// This version is called from RED_NEW with a Pool Type directly.
	template< typename ObjectType, typename Proxy >
	RED_MEMORY_INLINE void * NewHelper( Proxy proxy, u32 disabledHooks )
	{
		static_assert( std::is_base_of< Pool, Proxy >::value, "Provided Pool do not inherit from PoolInterface class." );
		static_assert( !std::is_same< Pool, Proxy >::value, "Provided Pool can't be Pool Interface class. Use RED_NEW with reference to pool as first argument." );
#ifndef RED_MEMORY_ENABLE_DEFAULT_ALLOCATION
		static_assert(!std::is_same< Proxy, PoolDefault >::value, "Allocation from Default Pool are forbidden.");
#endif

		typedef typename Proxy::DefaultAlignmentType DefaultAlignmentType;
		
		typedef typename AllocateInvokerResolver< Proxy, ( __alignof( ObjectType ) > DefaultAlignmentType::value ) >::Type AllocateInvoker;
	
		const u32 size = sizeof( ObjectType );
		const u32 alignment = __alignof( ObjectType );
	
		return AllocateInvoker::Invoke( proxy, size, alignment, disabledHooks );
	}

	template< typename ObjectType, typename Proxy >
	RED_MEMORY_INLINE void * NewHelper( Proxy * proxy, u32 disabledHooks )
	{
#ifndef RED_MEMORY_ENABLE_DEFAULT_ALLOCATION
		static_assert(!std::is_same< Proxy, PoolDefault >::value, "Allocation from Default Pool are forbidden.");
#endif
		typedef typename Proxy::DefaultAlignmentType DefaultAlignmentType;

		typedef typename AllocateInvokerResolver< Proxy, ( __alignof( ObjectType ) > DefaultAlignmentType::value ) >::Type AllocateInvoker;
		
		const u32 size = sizeof( ObjectType );
		const u32 alignment = __alignof( ObjectType );
		return AllocateInvoker::Invoke( *proxy, size, alignment, disabledHooks );
	}

	template< typename ObjectType >
	RED_MEMORY_INLINE ObjectType * NewArrayHelper( u32 count, u32 disabledHooks )
	{
		static_assert( !HasMemberOperatorNewFunction< ObjectType >::Value, "object can't define it's own operator new." );

		typedef typename PoolResolver< ObjectType >::PoolType PoolType;
		typedef typename PoolType::DefaultAlignmentType DefaultAlignmentType;
		typedef typename ArrayInitializerResolver< ObjectType >::Type ArrayInitializerType;
		typedef typename AllocateInvokerResolver< PoolType, ( __alignof( ObjectType ) > DefaultAlignmentType::value ) >::Type AllocateInvoker;

#ifndef RED_MEMORY_ENABLE_DEFAULT_ALLOCATION
		static_assert(!std::is_same< PoolType, PoolDefault >::value, "Allocation from Default Pool are forbidden.");
#endif

		const u32 size = count * sizeof( ObjectType );
		const u32 alignment = __alignof( ObjectType );

		void * block = AllocateInvoker::template Invoke< PoolType >( size, alignment, disabledHooks );
		ObjectType * objectArray = static_cast< ObjectType* >( block );
		ArrayInitializerType::Apply( objectArray, count );

		return objectArray;
	}

	// This version is called from RED_NEW_ARRAY with a Pool Type directly.
	template< typename ObjectType, typename Proxy >
	RED_MEMORY_INLINE ObjectType * NewArrayHelper( Proxy proxy, u32 count, u32 disabledHooks )
	{
		static_assert( std::is_base_of< Pool, Proxy >::value, "Provided Pool do not inherit from PoolInterface class." );
		static_assert( !std::is_same< Pool, Proxy >::value, "Provided Pool can't be PoolInterface class. Use RED_NEW_ARRAY with reference to pool as first argument." );
		
#ifndef RED_MEMORY_ENABLE_DEFAULT_ALLOCATION
		static_assert(!std::is_same< Proxy, PoolDefault >::value, "Allocation from Default Pool are forbidden.");
#endif

		typedef typename Proxy::DefaultAlignmentType DefaultAlignmentType;
		typedef typename ArrayInitializerResolver< ObjectType >::Type ArrayInitializerType;
		typedef typename AllocateInvokerResolver< Proxy, ( __alignof( ObjectType ) > DefaultAlignmentType::value ) >::Type AllocateInvoker;

		const u32 size = count * sizeof( ObjectType );
		const u32 alignment = __alignof( ObjectType );

		void * block = AllocateInvoker::Invoke( proxy, size, alignment, disabledHooks );
		ObjectType * objectArray = static_cast< ObjectType* >( block );
		ArrayInitializerType::Apply( objectArray, count );

		return objectArray;
	}

	template< typename ObjectType, typename Proxy >
	RED_MEMORY_INLINE ObjectType * NewArrayHelper( Proxy * proxy, u32 count, u32 disabledHooks )
	{
#ifndef RED_MEMORY_ENABLE_DEFAULT_ALLOCATION
		static_assert(!std::is_same< Proxy, PoolDefault >::value, "Allocation from Default Pool are forbidden.");
#endif

		typedef typename Proxy::DefaultAlignmentType DefaultAlignmentType;
		typedef typename AllocateInvokerResolver< Proxy, ( __alignof( ObjectType ) > DefaultAlignmentType::value ) >::Type AllocateInvoker;
		typedef typename ArrayInitializerResolver< ObjectType >::Type ArrayInitializerType;

		const u32 size = count * sizeof( ObjectType );
		const u32 alignment = __alignof( ObjectType );

		void * block = AllocateInvoker::Invoke( *proxy, size, alignment, disabledHooks );
		ObjectType * objectArray = static_cast< ObjectType* >( block );
		ArrayInitializerType::Apply( objectArray, count );

		return objectArray;
	}

	template< typename ObjectType >
	RED_MEMORY_INLINE void DeleteHelper( ObjectType * ptr, u32 disabledHooks )
	{
		static_assert( !HasMemberOperatorNewFunction< ObjectType >::Value, "object can't define it's own operator new." );

		static_assert( !std::is_void< ObjectType >::value, "Cannot call dtor on a void *" );
		static_assert( sizeof( ObjectType ) != 0, "Trying to delete an Incomplete type! Did you forgot the include?" );
		static_assert( !std::is_polymorphic< ObjectType >::value || std::has_virtual_destructor< ObjectType >::value, "Object is abstract but virtual dtor is missing." );

		DeleteResolver< ObjectType, InternalHasPolymorphicPoolContext< ObjectType >::Value >::Delete( ptr, disabledHooks );
	}

	template< typename Proxy, typename ObjectType >
	RED_MEMORY_INLINE void DeleteHelper( Proxy, ObjectType * ptr, u32 disabledHooks )
	{
		static_assert( !std::is_void< ObjectType >::value, "Cannot call dtor on a void *" );
		static_assert( sizeof( ObjectType ) != 0, "Trying to delete an Incomplete type! Did you forgot the include?" );
		static_assert( !std::is_polymorphic< ObjectType >::value || std::has_virtual_destructor< ObjectType >::value, "Object is abstract but virtual dtor is missing." );
		static_assert( std::is_base_of< Pool, Proxy >::value, "Provided pool do not inherit from Pool class." );
		static_assert( !std::is_same< Pool, Proxy >::value, "Provided Pool can't be Pool class. Use RED_DELETE with reference to a pool as first argument." );
	
		ptr->~ObjectType();
		Free< Proxy >( ptr, disabledHooks );
	}

	template< typename Proxy, typename ObjectType >
	RED_MEMORY_INLINE void DeleteHelper( Proxy * proxy, ObjectType * ptr, u32 disabledHooks )
	{
		static_assert( !std::is_void< ObjectType >::value, "Cannot call dtor on a void *" );
		static_assert( sizeof( ObjectType ) != 0, "Trying to delete an Incomplete type! Did you forgot the include?" );
		static_assert( !std::is_polymorphic< ObjectType >::value || std::has_virtual_destructor< ObjectType >::value, "Object is abstract but virtual dtor is missing." );
		
		typedef typename InvokerResolver< Proxy >::Type InvokerType;

		ptr->~ObjectType();
		InvokerType::InvokeFree( *proxy, ptr, disabledHooks );
	}

	template< typename ObjectType >
	RED_MEMORY_INLINE void DeleteArrayHelper( ObjectType * ptr, u32 count, u32 disabledHooks )
	{
		static_assert( !std::is_void< ObjectType >::value, "Cannot call dtor on a void *" );
		static_assert( sizeof( ObjectType ) != 0, "Trying to delete an Incomplete type! Did you forgot the include?" );
		static_assert( !std::is_polymorphic< ObjectType >::value || std::has_virtual_destructor< ObjectType >::value, "Object is abstract but virtual dtor is missing." );

		DeleteResolver< ObjectType, InternalHasPolymorphicPoolContext< ObjectType >::Value >::Delete( ptr, count, disabledHooks );
	}

	template< typename Proxy, typename ObjectType >
	RED_MEMORY_INLINE void DeleteArrayHelper( Proxy, ObjectType * ptr, u32 count, u32 disabledHooks )
	{
		static_assert( !std::is_void< ObjectType >::value, "Cannot call dtor on a void *" );
		static_assert( sizeof( ObjectType ) != 0, "Trying to delete an Incomplete type! Did you forgot the include?" );
		static_assert( !std::is_polymorphic< ObjectType >::value || std::has_virtual_destructor< ObjectType >::value, "Object is abstract but virtual dtor is missing." );
		static_assert( std::is_base_of< Pool, Proxy >::value, "Provided pool do not inherit from Pool class." );
		static_assert( !std::is_same< Pool, Proxy >::value, "Provided pool can't be Pool class. Use RED_DELETE_ARRAY with reference to a pool as first argument." );
		
		typedef typename ArrayDestructorResolver< ObjectType >::Type ArrayDestructorType;
		
		ArrayDestructorType::Apply( ptr, count );
		Free< Proxy >( ptr, disabledHooks );
	}

	template< typename Proxy, typename ObjectType >
	RED_MEMORY_INLINE void DeleteArrayHelper( Proxy * proxy, ObjectType * ptr, u32 count, u32 disabledHooks )
	{
		static_assert( !std::is_void< ObjectType >::value, "Cannot call dtor on a void *" );
		static_assert( sizeof( ObjectType ) != 0, "Trying to delete an Incomplete type! Did you forgot the include?" );
		static_assert( !std::is_polymorphic< ObjectType >::value || std::has_virtual_destructor< ObjectType >::value, "Object is abstract but virtual dtor is missing." );
		
		typedef typename ArrayDestructorResolver< ObjectType >::Type ArrayDestructorType;
		typedef typename InvokerResolver< Proxy >::Type InvokerType;

		ArrayDestructorType::Apply( ptr, count );
		InvokerType::InvokeFree( *proxy, ptr, disabledHooks );
	}

	template< typename Proxy >
	RED_MEMORY_INLINE void * AllocateHelper( Proxy, u32 size, u32 disabledHooks )
	{
		static_assert( std::is_base_of< Pool, Proxy >::value, "Provided pool do not inherit from Pool class." );
		static_assert( !std::is_same< Pool, Proxy >::value, "Provided pool can't be Pool class. Use RED_ALLOCATE with reference to a pool as first argument." );
#ifndef RED_MEMORY_ENABLE_DEFAULT_ALLOCATION
		static_assert( !std::is_same< Proxy, PoolDefault >::value, "Allocation from Default Pool are forbidden.");
#endif
		return Allocate< Proxy >( size, disabledHooks );
	}

	template< typename Proxy >
	RED_MEMORY_INLINE void * AllocateHelper( Proxy * proxy, u32 size, u32 disabledHooks )
	{
#ifndef RED_MEMORY_ENABLE_DEFAULT_ALLOCATION
		static_assert(!std::is_same< Proxy, PoolDefault >::value, "Allocation from Default Pool are forbidden.");
#endif
		typedef typename InvokerResolver< Proxy >::Type InvokerType;
		return InvokerType::InvokeAllocate( *proxy, size, 0, disabledHooks );
	}

	template< typename Proxy >
	RED_MEMORY_INLINE void * AllocateAlignedHelper( Proxy, u32 size, u32 alignment, u32 disabledHooks )
	{
		static_assert( std::is_base_of< Pool, Proxy >::value, "Provided pool do not inherit from Pool class." );
		static_assert( !std::is_same< Pool, Proxy >::value, "Provided pool can't be Pool class. Use RED_ALLOCATE_ALIGNED with reference to a pool as first argument." );
#ifndef RED_MEMORY_ENABLE_DEFAULT_ALLOCATION
		static_assert(!std::is_same< Proxy, PoolDefault >::value, "Allocation from Default Pool are forbidden.");
#endif
		return AllocateAligned< Proxy >( size, alignment, disabledHooks );
	}

	template< typename Proxy >
	RED_MEMORY_INLINE void * AllocateAlignedHelper( Proxy * proxy, u32 size, u32 alignment, u32 disabledHooks )
	{
#ifndef RED_MEMORY_ENABLE_DEFAULT_ALLOCATION
		static_assert(!std::is_same< Proxy, PoolDefault >::value, "Allocation from Default Pool are forbidden.");
#endif
		typedef typename InvokerResolver< Proxy >::Type InvokerType;
		return InvokerType::InvokeAllocateAligned( *proxy, size, alignment, disabledHooks );
	}

	template< typename Proxy >
	RED_MEMORY_INLINE void FreeHelper( Proxy, const void * block, u32 disabledHooks )
	{
		static_assert( std::is_base_of< Pool, Proxy >::value, "Provided pool do not inherit from Pool class." );
		static_assert( !std::is_same< Pool, Proxy >::value, "Provided pool can't be Pool class. Use RED_FREE with reference to a pool as first argument." );
		
#ifndef RED_MEMORY_ENABLE_DEFAULT_ALLOCATION
		static_assert(!std::is_same< Proxy, PoolDefault >::value, "Allocation from Default Pool are forbidden.");
#endif

		Free< Proxy >( block, disabledHooks );
	}

	template< typename Proxy >
	RED_MEMORY_INLINE void FreeHelper( Proxy * proxy, const void * block, u32 disabledHooks )
	{
		typedef typename InvokerResolver< Proxy >::Type InvokerType;

#ifndef RED_MEMORY_ENABLE_DEFAULT_ALLOCATION
		static_assert(!std::is_same< Proxy, PoolDefault >::value, "Allocation from Default Pool are forbidden.");
#endif

		InvokerType::InvokeFree( *proxy, block, disabledHooks );
	}

	template< typename Proxy >
	RED_MEMORY_INLINE void * ReallocateHelper( Proxy, void * block, u32 size, u32 disabledHooks )
	{
		static_assert( std::is_base_of< Pool, Proxy >::value, "Provided pool do not inherit from Pool class." );
		static_assert( !std::is_same< Pool, Proxy >::value, "Provided pool can't be Pool class. Use RED_REALLOCATE with reference to a pool as first argument." );
#ifndef RED_MEMORY_ENABLE_DEFAULT_ALLOCATION
		static_assert(!std::is_same< Proxy, PoolDefault >::value, "Allocation from Default Pool are forbidden.");
#endif
		return Reallocate< Proxy >( block, size, disabledHooks );
	}

	template< typename Proxy >
	RED_MEMORY_INLINE void * ReallocateHelper( Proxy * proxy, void * block, u32 size, u32 disabledHooks )
	{
#ifndef RED_MEMORY_ENABLE_DEFAULT_ALLOCATION
		static_assert(!std::is_same< Proxy, PoolDefault >::value, "Allocation from Default Pool are forbidden.");
#endif
		typedef typename InvokerResolver< Proxy >::Type InvokerType;
		return InvokerType::InvokeReallocate( *proxy, block, size, disabledHooks );
	}

	template< typename Proxy >
	RED_MEMORY_INLINE void * ReallocateAlignedHelper( Proxy , void * block, u32 size, u32 alignment, u32 disabledHooks )
	{
		static_assert( std::is_base_of< Pool, Proxy >::value, "Provided pool do not inherit from Pool class." );
		static_assert( !std::is_same< Pool, Proxy >::value, "Provided pool can't be Pool class. Use RED_REALLOCATE_ALIGNED with reference to a pool as first argument." );
#ifndef RED_MEMORY_ENABLE_DEFAULT_ALLOCATION
		static_assert(!std::is_same< Proxy, PoolDefault >::value, "Allocation from Default Pool are forbidden.");
#endif
		return ReallocateAligned< Proxy >( block, size, alignment, disabledHooks );
	}

	template< typename Proxy >
	RED_MEMORY_INLINE void * ReallocateAlignedHelper( Proxy * proxy, void * block, u32 size, u32 alignment, u32 disabledHooks )
	{
#ifndef RED_MEMORY_ENABLE_DEFAULT_ALLOCATION
		static_assert(!std::is_same< Proxy, PoolDefault >::value, "Allocation from Default Pool are forbidden.");
#endif
		typedef typename InvokerResolver< Proxy >::Type InvokerType;
		return InvokerType::InvokeReallocateAligned( *proxy, block, size, alignment, disabledHooks );
	}
}
}
}

#endif
