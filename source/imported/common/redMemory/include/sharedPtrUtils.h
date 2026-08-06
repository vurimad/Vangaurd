/**
* Copyright (c) 2016 CD Projekt Red. All Rights Reserved.
*/

#ifndef _RED_MEMORY_SHARED_PTR_UTILS_H_
#define _RED_MEMORY_SHARED_PTR_UTILS_H_

namespace red
{
	template< typename T, typename U, typename V, typename W, template< typename, typename, typename > class Storage >
	Storage< T, V, W > StaticCast( const Storage< U, V, W > & storage );

	template< typename T, typename U, template< typename > class Storage >
	Storage< T > StaticCast( const Storage< U > & storage );

}

#include "sharedPtrUtils.hpp"

#endif
