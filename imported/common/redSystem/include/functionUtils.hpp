/**
 * Copyright (c) 2020 CD Projekt Red. All Rights Reserved.
 */

#ifndef _RED_SYSTEM_FUNCTION_UTILS_HPP_
#define _RED_SYSTEM_FUNCTION_UTILS_HPP_

namespace red
{
	template< typename R, typename... Args >
	constexpr VTable< R, Args... >::VTable() noexcept
		: invokePtr( nullptr )
		, copyPtr( []( StoragePtr, StoragePtr ) {} )
		, relocatePtr( []( StoragePtr, StoragePtr ) {} )
		, destructorPtr( []( StoragePtr ) {} )
	{}

	template< typename R, typename... Args >
	VTable< R, Args... >::~VTable()
	{}

	template< typename R, typename... Args >
	template< typename C, typename std::enable_if< std::is_copy_constructible< C >::value && std::is_move_constructible< C >::value >::type* >
	constexpr VTable< R, Args... >::VTable( ClosureWrapper< C > )
		: invokePtr( []( StoragePtr storagePtr, Args&&... args ) -> R { return ( *static_cast< C* >( storagePtr ) )( std::forward< Args >( args )... ); } )
		, copyPtr( []( StoragePtr dstPtr, StoragePtr srcPtr ) { ::new ( dstPtr ) C{ ( *static_cast< C* >( srcPtr ) ) }; } )
		, relocatePtr( []( StoragePtr dstPtr, StoragePtr srcPtr ) { ::new ( dstPtr ) C{ std::move( *static_cast< C* >( srcPtr ) ) }; static_cast< C* >( srcPtr )->~C(); } )
		, destructorPtr( []( StoragePtr srcPtr ) { static_cast< C* >( srcPtr )->~C(); } )
	{}

	template< typename R, typename... Args >
	template< typename C, typename std::enable_if< !std::is_copy_constructible< C >::value && std::is_move_constructible< C >::value >::type* >
	constexpr VTable< R, Args... >::VTable( ClosureWrapper< C > )
		: invokePtr( []( StoragePtr storagePtr, Args&&... args ) -> R { return ( *static_cast< C* >( storagePtr ) )( std::forward< Args >( args )... ); } )
		, copyPtr( nullptr )
		, relocatePtr( []( StoragePtr dstPtr, StoragePtr srcPtr ) { ::new ( dstPtr ) C{ std::move( *static_cast< C* >( srcPtr ) ) }; static_cast< C* >( srcPtr )->~C(); } )
		, destructorPtr( []( StoragePtr srcPtr ) { static_cast< C* >( srcPtr )->~C(); } )
	{}

	template< typename R, typename... Args >
	template< typename C, typename std::enable_if< std::is_copy_constructible< C >::value && !std::is_move_constructible< C >::value >::type* >
	constexpr VTable< R, Args... >::VTable( ClosureWrapper< C > )
		: invokePtr( []( StoragePtr storagePtr, Args&&... args ) -> R { return ( *static_cast< C* >( storagePtr ) )( std::forward< Args >( args )... ); } )
		, copyPtr( []( StoragePtr dstPtr, StoragePtr srcPtr ) { ::new ( dstPtr ) C{ ( *static_cast< C* >( srcPtr ) ) }; } )
		, relocatePtr( nullptr )
		, destructorPtr( []( StoragePtr srcPtr ) { static_cast< C* >( srcPtr )->~C(); } )
	{}
}

#endif