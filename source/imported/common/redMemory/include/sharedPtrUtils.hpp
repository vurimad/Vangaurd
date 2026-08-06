/**
* Copyright (c) 2016 CD Projekt Red. All Rights Reserved.
*/

#ifndef _RED_MEMORY_SHARED_PTR_UTILS_HPP_
#define _RED_MEMORY_SHARED_PTR_UTILS_HPP_

#include "sharedStorage.h"
#include "weakStorage.h"

namespace red
{
	template< typename T, typename U, typename V, typename W, template< typename, typename, typename > class Storage >
	RED_MEMORY_INLINE Storage< T, V, W > StaticCast( const Storage< U, V, W > & storage )
	{
		static_assert(	
			std::is_base_of< T, U >::value || 
			std::is_base_of< U, T >::value,
			"Cannot static cast unrelated type.");

		static_assert(
			std::is_base_of< SharedStorage< U, V, W >, Storage< U, V, W > >::value ||
			std::is_base_of< WeakStorage< U, V, W >, Storage< U, V, W > >::value, 
			"Cannot static cast smart object unrelated from SharedStorage or WeakStorage." );

		return *reinterpret_cast< const Storage< T, V, W > * >( &storage );
	}

	template< typename T, typename U, template< typename > class Storage >
	RED_MEMORY_INLINE Storage< T > StaticCast( const Storage< U > & storage )
	{
		typedef Storage< U > InputType;
		typedef Storage< T > OutputType;
		
		static_assert(
			std::is_same< typename InputType::StorageType, typename OutputType::StorageType >::value, 
			"Cannot static cast smart object of different storage." );

		static_assert(
			std::is_base_of< SharedStorage< U, typename InputType::StorageType >, Storage< U > >::value ||
			std::is_base_of< WeakStorage< U, typename InputType::StorageType >, Storage< U > >::value, 
			"Cannot static cast smart object unrelated from SharedStorage or WeakStorage." );
		
		static_assert(	
			std::is_base_of< T, U >::value || 
			std::is_base_of< U, T >::value,
			"Cannot static cast unrelated type.");

		return *reinterpret_cast< const Storage< T > * >( &storage );
	}
}

#endif
