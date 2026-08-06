/**
 * Copyright (c) 2020 CD Projekt Red. All Rights Reserved.
 */

#ifndef _RED_SYSTEM_FUNCTION_UTILS_H_
#define _RED_SYSTEM_FUNCTION_UTILS_H_

namespace red
{
	template< typename T >
	struct ClosureWrapper
	{
		using Type = T;
	};

	template< typename R, typename... Args >
	struct VTable
	{
		using StoragePtr = void*;

		using InvokePtr = R(*)( StoragePtr, Args&&... );
		using ProcessPtr = void(*)( StoragePtr, StoragePtr );
		using DestructorPtr = void(*)( StoragePtr );

		const InvokePtr invokePtr;
		const ProcessPtr copyPtr;
		const ProcessPtr relocatePtr;
		const DestructorPtr destructorPtr;

		explicit constexpr VTable() noexcept;
		~VTable();

		template< typename C, typename std::enable_if< std::is_copy_constructible< C >::value && std::is_move_constructible< C >::value >::type* = nullptr >
		explicit constexpr VTable( ClosureWrapper< C > );

		template< typename C, typename std::enable_if< !std::is_copy_constructible< C >::value && std::is_move_constructible< C >::value >::type* = nullptr >
		explicit constexpr VTable( ClosureWrapper< C > );

		template< typename C, typename std::enable_if< std::is_copy_constructible< C >::value && !std::is_move_constructible< C >::value >::type* = nullptr >
		explicit constexpr VTable( ClosureWrapper< C > );

		VTable( const VTable& ) = delete;
		VTable( VTable&& ) = delete;

		VTable& operator=( const VTable& ) = delete;
		VTable& operator=( VTable&& ) = delete;
	};

	using DefaultFunctionSmallBufferOptimizationSize = std::integral_constant< Uint32, 24 >;
	using DefaultFixedSizeFunctionSize = std::integral_constant< Uint32, 32 >;
	using DefaultFunctionAlignment = std::integral_constant< Uint32, 8 >;
}

#include "functionUtils.hpp"

#endif