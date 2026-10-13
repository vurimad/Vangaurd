/**
 * Copyright (c) 2020 CD Projekt Red. All Rights Reserved.
 */

#ifndef _RED_SYSTEM_FIXED_SIZE_FUNCTION_H_
#define _RED_SYSTEM_FIXED_SIZE_FUNCTION_H_

#include "functionUtils.h"

namespace red
{
	template<
		typename,
		Uint32 Capacity = DefaultFixedSizeFunctionSize::value,
		Uint32 Alignment = DefaultFunctionAlignment::value >
	class FixedSizeFunction;

	template< typename R, typename... Args, Uint32 Capacity, Uint32 Alignment >
	class FixedSizeFunction< R( Args... ), Capacity, Alignment >
	{
	public:
		FixedSizeFunction();
		FixedSizeFunction( std::nullptr_t ) noexcept;

		~FixedSizeFunction();

		template< typename T, typename C = typename std::decay< T >::type,
			typename std::enable_if<
				!std::is_same< C, FixedSizeFunction >::value
			>::type* = nullptr >
		FixedSizeFunction( T&& closure );

		FixedSizeFunction( const FixedSizeFunction& other );
		FixedSizeFunction( FixedSizeFunction&& other ) noexcept;

		FixedSizeFunction& operator=( const FixedSizeFunction& other );
		FixedSizeFunction& operator=( FixedSizeFunction&& other ) noexcept;
		FixedSizeFunction& operator=( std::nullptr_t ) noexcept;

		template< typename T, typename C = typename std::decay< T >::type,
			typename std::enable_if<
				!std::is_same< C, FixedSizeFunction >::value
			>::type* = nullptr >
		FixedSizeFunction& operator=( T&& closure );

		R operator()( Args... args ) const;

		explicit operator Bool() const noexcept;

	private:
		template< typename T, typename C = typename std::decay< T >::type,
			typename std::enable_if<
				!std::is_same< C, FixedSizeFunction >::value
			>::type* = nullptr >
		void CreateFunction( T&& closure );
		void DestroyFunction();
		void CopyFunction( const FixedSizeFunction& other );
		void MoveFunction( FixedSizeFunction&& other );

		using VTablePtr = const VTable< R, Args... >*;

		mutable typename std::aligned_storage< Capacity, Alignment >::type m_stack;
		VTablePtr m_vtablePtr;
	};

	template< typename >
	class FunctionPointerWrapper;

	template< typename R, typename... Args >
	class FunctionPointerWrapper< R( Args... ) >
	{
	public:
		FunctionPointerWrapper();

		template< typename C >
		FunctionPointerWrapper( C&& closure );

		R operator()( Args... args ) const;

		explicit operator Bool() const noexcept;

	private:
		R ( *m_functionPtr )( Args... );
	};

}

#include "fixedSizeFunction.hpp"

#endif