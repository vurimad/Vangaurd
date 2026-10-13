/**
 * Copyright (c) 2015 CD Projekt Red. All Rights Reserved.
 */

#ifndef _RED_MEMORY_OPERATORS_H_
#define _RED_MEMORY_OPERATORS_H_

#include "redMemoryInternal.h" // should be included via local precompiled header. However, for the sake of standalone include, I'm making sure it is included. 
#include "../src/operatorsInternal.h"
#include "../src/macroUtils.h"

//////////////////////////////////////////////////////////////////////////
// 
// HOW-TO guide can be found in documentation.cpp file.
// Take note also that RED_NEW cannot be used with object that have member operator new.
//
#ifdef __INTELLISENSE__
# define RED_NEW( type, ... ) new type
# define RED_NEW_WITHOUT_HOOKS( type, ... ) new type
# define RED_NEW_ARRAY( type, count, ... ) new type[ count ]
# define RED_NEW_ARRAY_WITHOUT_HOOKS( type, count, ... ) new type[ count ]
# define RED_DELETE( ptr, ... ) delete ptr
# define RED_DELETE_WITHOUT_HOOKS( ptr, ... ) delete ptr
# define RED_DELETE_ARRAY( ptr, count, ... ) delete [] ptr
# define RED_DELETE_ARRAY_WITHOUT_HOOKS( ptr, count, ... ) delete [] ptr
#else
# ifdef RED_COMPILER_MSC
	#define RED_NEW( ... )						RED_MEMORY_CONCAT( RED_MEMORY_OVERLOAD( _INTERNAL_RED_NEW_, __VA_ARGS__ )( __VA_ARGS__ ), RED_MEMORY_EMPTY() )
	#define RED_NEW_WITHOUT_HOOKS( ... )		RED_MEMORY_CONCAT( RED_MEMORY_OVERLOAD( _INTERNAL_RED_NEW_WITHOUT_HOOKS_, __VA_ARGS__ )( __VA_ARGS__ ), RED_MEMORY_EMPTY() )
	#define RED_NEW_ARRAY( ... )				RED_MEMORY_CONCAT( RED_MEMORY_OVERLOAD( _INTERNAL_RED_NEW_ARRAY_, __VA_ARGS__ )( __VA_ARGS__ ), RED_MEMORY_EMPTY() )
	#define RED_NEW_ARRAY_WITHOUT_HOOKS( ... )	RED_MEMORY_CONCAT( RED_MEMORY_OVERLOAD( _INTERNAL_RED_NEW_ARRAY_WITHOUT_HOOKS_, __VA_ARGS__ )( __VA_ARGS__ ), RED_MEMORY_EMPTY() )

	#define RED_DELETE( ... )						RED_MEMORY_CONCAT( RED_MEMORY_OVERLOAD( _INTERNAL_RED_DELETE_, __VA_ARGS__ )( __VA_ARGS__ ), RED_MEMORY_EMPTY() )
	#define RED_DELETE_WITHOUT_HOOKS( ... )			RED_MEMORY_CONCAT( RED_MEMORY_OVERLOAD( _INTERNAL_RED_DELETE_WITHOUT_HOOKS_, __VA_ARGS__ )( __VA_ARGS__ ), RED_MEMORY_EMPTY() )
	#define RED_DELETE_ARRAY( ... )					RED_MEMORY_CONCAT( RED_MEMORY_OVERLOAD( _INTERNAL_RED_DELETE_ARRAY_, __VA_ARGS__ )( __VA_ARGS__ ), RED_MEMORY_EMPTY() )
	#define RED_DELETE_ARRAY_WITHOUT_HOOKS( ... )	RED_MEMORY_CONCAT( RED_MEMORY_OVERLOAD( _INTERNAL_RED_DELETE_ARRAY_WITHOUT_HOOKS_, __VA_ARGS__ )( __VA_ARGS__ ), RED_MEMORY_EMPTY() )
# else
	#define RED_NEW( ... )						RED_MEMORY_OVERLOAD( _INTERNAL_RED_NEW_, __VA_ARGS__ )( __VA_ARGS__ )
	#define RED_NEW_WITHOUT_HOOKS( ... )		RED_MEMORY_OVERLOAD( _INTERNAL_RED_NEW_WITHOUT_HOOKS_, __VA_ARGS__ )( __VA_ARGS__ )
	#define RED_NEW_ARRAY( ... )				RED_MEMORY_OVERLOAD( _INTERNAL_RED_NEW_ARRAY_, __VA_ARGS__ )( __VA_ARGS__ )
	#define RED_NEW_ARRAY_WITHOUT_HOOKS( ... )	RED_MEMORY_OVERLOAD( _INTERNAL_RED_NEW_ARRAY_WITHOUT_HOOKS_, __VA_ARGS__ )( __VA_ARGS__ )
	
	#define RED_DELETE( ... )						RED_MEMORY_OVERLOAD( _INTERNAL_RED_DELETE_, __VA_ARGS__ )( __VA_ARGS__ )
	#define RED_DELETE_WITHOUT_HOOKS( ... )			RED_MEMORY_OVERLOAD( _INTERNAL_RED_DELETE_WITHOUT_HOOKS_, __VA_ARGS__ )( __VA_ARGS__ )
	#define RED_DELETE_ARRAY( ... )					RED_MEMORY_OVERLOAD( _INTERNAL_RED_DELETE_ARRAY_, __VA_ARGS__ )( __VA_ARGS__ )
	#define RED_DELETE_ARRAY_WITHOUT_HOOKS( ... )	RED_MEMORY_OVERLOAD( _INTERNAL_RED_DELETE_ARRAY_WITHOUT_HOOKS_, __VA_ARGS__ )( __VA_ARGS__ )
# endif
#endif	

//////////////////////////////////////////////////////////////////////////

#define RED_ALLOCATE( proxy, size ) \
	red::memory::internal::AllocateHelper( proxy(), static_cast< red::memory::u32 >( size ), red::memory::HookType::HookType_None  )

#define RED_ALLOCATE_WITHOUT_HOOKS( proxy, size, disabledHooks ) \
	red::memory::internal::AllocateHelper( proxy(), static_cast< red::memory::u32 >( size ), static_cast< red::memory::u32 >( disabledHooks ) )

#define RED_ALLOCATE_ALIGNED( proxy, size, alignment ) \
	red::memory::internal::AllocateAlignedHelper( proxy(), static_cast< red::memory::u32 >( size ), static_cast< red::memory::u32 >( alignment ), red::memory::HookType::HookType_None  )

#define RED_ALLOCATE_ALIGNED_WITHOUT_HOOKS( proxy, size, alignment, disabledHooks ) \
	red::memory::internal::AllocateAlignedHelper( proxy(), static_cast< red::memory::u32 >( size ), static_cast< red::memory::u32 >( alignment ), static_cast< red::memory::u32 >( disabledHooks ) )

#define RED_REALLOCATE( proxy, ptr, size ) \
	red::memory::internal::ReallocateHelper( proxy(), ptr, static_cast< red::memory::u32 >( size ), red::memory::HookType::HookType_None  )

#define RED_REALLOCATE_WITHOUT_HOOKS( proxy, ptr, size, disabledHooks ) \
	red::memory::internal::ReallocateHelper( proxy(), ptr, static_cast< red::memory::u32 >( size ), static_cast< red::memory::u32 >( disabledHooks ) )

#define RED_REALLOCATE_ALIGNED( proxy, ptr, size, alignment ) \
	red::memory::internal::ReallocateAlignedHelper( proxy(), ptr, static_cast< red::memory::u32 >( size ), static_cast< red::memory::u32 >( alignment ), red::memory::HookType::HookType_None  )

#define RED_REALLOCATE_ALIGNED_WITHOUT_HOOKS( proxy, ptr, size, alignment, disabledHooks ) \
	red::memory::internal::ReallocateAlignedHelper( proxy(), ptr, static_cast< red::memory::u32 >( size ), static_cast< red::memory::u32 >( alignment ), static_cast< red::memory::u32 >( disabledHooks ) )

#define RED_FREE( proxy, ptr ) \
	red::memory::internal::FreeHelper( proxy(), ptr, red::memory::HookType::HookType_None  )

#define RED_FREE_WITHOUT_HOOKS( proxy, ptr, disabledHooks ) \
	red::memory::internal::FreeHelper( proxy(), ptr, static_cast< red::memory::u32 >( disabledHooks ) )

//////////////////////////////////////////////////////////////////////////

#define RED_ALLOCA( size ) ::alloca( (size) )

#ifdef __INTELLISENSE__
# define RED_NEW_ALLOCA( type, ... ) new type
#else
# define RED_NEW_ALLOCA( type, ... ) new( RED_ALLOCA( sizeof( type ) ) ) type( __VA_ARGS__ )
#endif

//////////////////////////////////////////////////////////////////////////

#endif
