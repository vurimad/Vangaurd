/**
 * Copyright (c) 2016 CD Projekt Red. All Rights Reserved.
 */

#ifndef _RED_MEMORY_OPERATORS_INTERNAL_H_
#define _RED_MEMORY_OPERATORS_INTERNAL_H_

//////////////////////////////////////////////////////////////////////////

#include "../include/types.h"
#include "../include/hookType.h"

namespace red
{
namespace memory
{
namespace internal
{
	// TODO it should be possible to reduce the count of needed operator helper function...

	template< typename ObjectType >
	void * NewHelper( u32 disabledHooks );

	// This version is called from RED_NEW with a Pool Type directly.
	template< typename ObjectType, typename Proxy >
	void * NewHelper( Proxy proxy, u32 disabledHooks );

	template< typename ObjectType, typename Proxy >
	void * NewHelper( Proxy * proxy, u32 disabledHooks );

	template< typename ObjectType >
	ObjectType * NewArrayHelper( u32 count, u32 disabledHooks );

	// This version is called from RED_NEW_ARRAY with a Pool Type directly.
	template< typename ObjectType, typename Proxy >
	ObjectType * NewArrayHelper( Proxy proxy, u32 count, u32 disabledHooks );

	template< typename ObjectType, typename Proxy >
	ObjectType * NewArrayHelper( Proxy * proxy, u32 count, u32 disabledHooks );

	template< typename ObjectType >
	void DeleteHelper( ObjectType * ptr, u32 disabledHooks );

	template< typename Proxy, typename ObjectType >
	void DeleteHelper( Proxy, ObjectType * ptr, u32 disabledHooks );

	template< typename Proxy, typename ObjectType >
	void DeleteHelper( Proxy * proxy, ObjectType * ptr, u32 disabledHooks );

	template< typename ObjectType >
	void DeleteArrayHelper( ObjectType * ptr, u32 count, u32 disabledHooks );

	template< typename Proxy, typename ObjectType >
	void DeleteArrayHelper( Proxy, ObjectType * ptr, u32 count, u32 disabledHooks );

	template< typename Proxy, typename ObjectType >
	void DeleteArrayHelper( Proxy * proxy, ObjectType * ptr, u32 count, u32 disabledHooks );

	template< typename Proxy >
	void * AllocateHelper( Proxy, u32 size, u32 disabledHooks );

	template< typename Proxy >
	void * AllocateHelper( Proxy * proxy, u32 size, u32 disabledHooks );

	template< typename Proxy >
	void * AllocateAlignedHelper( Proxy, u32 size, u32 alignment, u32 disabledHooks );

	template< typename Proxy >
	void * AllocateAlignedHelper( Proxy * proxy, u32 size, u32 alignment, u32 disabledHooks );

	template< typename Proxy >
	void FreeHelper( Proxy, const void * block, u32 disabledHooks );

	template< typename Proxy >
	void FreeHelper( Proxy * proxy, const void * block, u32 disabledHooks );

	template< typename Proxy >
	void * ReallocateHelper( Proxy, void * block, u32 size, u32 disabledHooks );

	template< typename Proxy >
	void * ReallocateHelper( Proxy * proxy, void * block, u32 size, u32 disabledHooks );
	
	template< typename Proxy >
	void * ReallocateAlignedHelper( Proxy , void * block, u32 size, u32 alignment, u32 disabledHooks );

	template< typename Proxy >
	void * ReallocateAlignedHelper( Proxy * proxy, void * block, u32 size, u32 alignment, u32 disabledHooks );
}
}
}

//////////////////////////////////////////////////////////////////////////

#define _INTERNAL_RED_NEW( type ) \
	::new ( red::memory::internal::NewHelper< type >( red::memory::HookType::HookType_None ) ) type

#define _INTERNAL_RED_NEW_WITHOUT_HOOKS( type, disabledHooks ) \
	::new ( red::memory::internal::NewHelper< type >( disabledHooks ) ) type

#define _INTERNAL_RED_NEW_WITH_PROXY( type, proxy ) \
	::new ( red::memory::internal::NewHelper< type >( proxy(), red::memory::HookType::HookType_None ) ) type

#define _INTERNAL_RED_NEW_WITH_PROXY_WITHOUT_HOOKS( type, proxy, disabledHooks ) \
	::new ( red::memory::internal::NewHelper< type >( proxy(), disabledHooks ) ) type

#define _INTERNAL_RED_NEW_ARRAY( type, count ) \
	red::memory::internal::NewArrayHelper< type >( count, red::memory::HookType::HookType_None )

#define _INTERNAL_RED_NEW_ARRAY_WITH_PROXY( type, count, proxy ) \
	red::memory::internal::NewArrayHelper< type >( proxy(), count, red::memory::HookType::HookType_None )

#define _INTERNAL_RED_NEW_ARRAY_WITHOUT_HOOKS( type, count, disabledHooks ) \
	red::memory::internal::NewArrayHelper< type >( count, disabledHooks )

#define _INTERNAL_RED_NEW_ARRAY_WITH_PROXY_WITHOUT_HOOKS( type, count, proxy, disabledHooks ) \
	red::memory::internal::NewArrayHelper< type >( proxy(), count, disabledHooks )

//////////////////////////////////////////////////////////////////////////

#define _INTERNAL_RED_DELETE( ptr ) \
	do{ if( ptr ) red::memory::internal::DeleteHelper( ptr, red::memory::HookType::HookType_None ); } while( 0, 0 )

#define _INTERNAL_RED_DELETE_WITHOUT_HOOKS( ptr, disabledHooks ) \
	do{ if( ptr ) red::memory::internal::DeleteHelper( ptr, disabledHooks ); } while( 0, 0 )

#define _INTERNAL_RED_DELETE_WITH_PROXY( ptr, proxy ) \
	do{ if( ptr ) red::memory::internal::DeleteHelper( proxy(), ptr, red::memory::HookType::HookType_None ); } while( 0, 0 )

#define _INTERNAL_RED_DELETE_WITH_PROXY_WITHOUT_HOOKS( ptr, proxy, disabledHooks ) \
	do{ if( ptr ) red::memory::internal::DeleteHelper( proxy(), ptr, disabledHooks ); } while( 0, 0 )

#define _INTERNAL_RED_DELETE_ARRAY( ptr, count ) \
	do{ if( ptr ) red::memory::internal::DeleteArrayHelper( ptr, count, red::memory::HookType::HookType_None ); } while( 0, 0 )

#define _INTERNAL_RED_DELETE_ARRAY_WITHOUT_HOOKS( ptr, count, disabledHooks ) \
	do{ if( ptr ) red::memory::internal::DeleteArrayHelper( ptr, count, disabledHooks ); } while( 0, 0 )

#define _INTERNAL_RED_DELETE_ARRAY_WITH_PROXY( ptr, count, proxy ) \
	do{ if( ptr ) red::memory::internal::DeleteArrayHelper( proxy(), ptr, count, red::memory::HookType::HookType_None ); } while( 0, 0 )

#define _INTERNAL_RED_DELETE_ARRAY_WITH_PROXY_WITHOUT_HOOKS( ptr, count, proxy, disabledHooks ) \
	do{ if( ptr ) red::memory::internal::DeleteArrayHelper( proxy(), ptr, count, disabledHooks ); } while( 0, 0 )

//////////////////////////////////////////////////////////////////////////

#define _INTERNAL_RED_NEW_1( type )			_INTERNAL_RED_NEW( type )
#define _INTERNAL_RED_NEW_2( type, proxy )	_INTERNAL_RED_NEW_WITH_PROXY( type, proxy )

#define _INTERNAL_RED_NEW_WITHOUT_HOOKS_2( type, disabledHooks )			_INTERNAL_RED_NEW_WITHOUT_HOOKS( type, disabledHooks )
#define _INTERNAL_RED_NEW_WITHOUT_HOOKS_3( type, proxy, disabledHooks )		_INTERNAL_RED_NEW_WITH_PROXY_WITHOUT_HOOKS( type, proxy, disabledHooks )

#define _INTERNAL_RED_NEW_ARRAY_2( type, count )		_INTERNAL_RED_NEW_ARRAY( type, count )
#define _INTERNAL_RED_NEW_ARRAY_3( type, count, proxy )	_INTERNAL_RED_NEW_ARRAY_WITH_PROXY( type, count, proxy )

#define _INTERNAL_RED_NEW_ARRAY_WITHOUT_HOOKS_3( type, count, disabledHooks )			_INTERNAL_RED_NEW_ARRAY_WITHOUT_HOOKS( type, count, disabledHooks )
#define _INTERNAL_RED_NEW_ARRAY_WITHOUT_HOOKS_4( type, count, proxy, disabledHooks )	_INTERNAL_RED_NEW_ARRAY_WITH_PROXY_WITHOUT_HOOKS( type, count, proxy, disabledHooks )

#define _INTERNAL_RED_DELETE_1( ptr )			_INTERNAL_RED_DELETE( ptr )
#define _INTERNAL_RED_DELETE_2( ptr, proxy )	_INTERNAL_RED_DELETE_WITH_PROXY( ptr, proxy )

#define _INTERNAL_RED_DELETE_WITHOUT_HOOKS_2( ptr, disabledHooks )			_INTERNAL_RED_DELETE_WITHOUT_HOOKS( ptr, disabledHooks )
#define _INTERNAL_RED_DELETE_WITHOUT_HOOKS_3( ptr, proxy, disabledHooks )	_INTERNAL_RED_DELETE_WITH_PROXY_WITHOUT_HOOKS( ptr, proxy, disabledHooks )

#define _INTERNAL_RED_DELETE_ARRAY_2( ptr, count )			_INTERNAL_RED_DELETE_ARRAY( ptr, count )
#define _INTERNAL_RED_DELETE_ARRAY_3( ptr, count, proxy )	_INTERNAL_RED_DELETE_ARRAY_WITH_PROXY( ptr, count, proxy )

#define _INTERNAL_RED_DELETE_ARRAY_WITHOUT_HOOKS_3( ptr, count, disabledHooks )			_INTERNAL_RED_DELETE_ARRAY_WITHOUT_HOOKS( ptr, count, disabledHooks )
#define _INTERNAL_RED_DELETE_ARRAY_WITHOUT_HOOKS_4( ptr, count, proxy, disabledHooks )	_INTERNAL_RED_DELETE_ARRAY_WITH_PROXY_WITHOUT_HOOKS( ptr, count, proxy, disabledHooks )

//////////////////////////////////////////////////////////////////////////

#include "operatorsInternal.hpp"

#endif
