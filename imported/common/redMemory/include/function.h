/**
 * Copyright (c) 2018 CD Projekt Red. All Rights Reserved.
 */

#ifndef _RED_MEMORY_FUNCTION_H_
#define _RED_MEMORY_FUNCTION_H_

#include "pool.h"

namespace red
{
	template<
		typename,
		Uint32 SmallBufferOptimizationSize = DefaultFunctionSmallBufferOptimizationSize::value,
		Uint32 Alignment = DefaultFunctionAlignment::value >
	class Function;

	template< typename R, typename... Args, Uint32 SmallBufferOptimizationSize, Uint32 Alignment >
	class Function< R( Args... ), SmallBufferOptimizationSize, Alignment >
	{
	public:
		Function();
		Function( const red::memory::Pool& pool );
		Function( std::nullptr_t ) noexcept;

		~Function();

		template< typename T, typename C = typename std::decay< T >::type,
			typename std::enable_if<
				!std::is_same< C, Function >::value
				&& !std::is_base_of< red::memory::Pool, C >::value
			>::type* = nullptr >
		Function( T&& closure );

		template< typename T, typename C = typename std::decay< T >::type,
			typename std::enable_if<
				!std::is_same< C, Function >::value
				&& !std::is_base_of< red::memory::Pool, C >::value
			>::type* = nullptr >
		explicit Function( const red::memory::Pool& pool, T&& closure );

		Function( const Function& other );
		Function( Function&& other ) noexcept;

		Function& operator=( const Function& other );
		Function& operator=( Function&& other ) noexcept;
		Function& operator=( std::nullptr_t ) noexcept;

		template< typename T, typename C = typename std::decay< T >::type,
			typename std::enable_if<
				!std::is_same< C, Function >::value
				&& !std::is_base_of< red::memory::Pool, C >::value
			>::type* = nullptr >
		Function& operator=( T&& closure );

		R operator()( Args... args ) const;

		explicit operator Bool() const noexcept;

		void SetPool( const red::memory::Pool& pool );
		const red::memory::Pool& GetPool() const;

		Uint32 Debug_GetPoolAllocationSize() const { return m_poolAllocationSize; }

	private:
		using VTablePtr = const VTable< R, Args... >*;

		enum FunctionMode
		{
			FunctionMode_Internal = 0,
			FunctionMode_External = 1
		};

		Bool IsInternal() const;

		template< typename T, typename C = typename std::decay< T >::type,
			typename std::enable_if<
				!std::is_same< C, Function >::value
				&& !std::is_base_of< red::memory::Pool, C >::value
			>::type* = nullptr >
		void CreateFunction( T&& closure );

		template< typename T, typename C = typename std::decay< T >::type,
			typename std::enable_if<
				!std::is_same< C, Function >::value
				&& !std::is_base_of< red::memory::Pool, C >::value
			>::type* = nullptr >
		void CreateInternalFunction( T&& closure );

		template< typename T, typename C = typename std::decay< T >::type,
			typename std::enable_if<
				!std::is_same< C, Function >::value
				&& !std::is_base_of< red::memory::Pool, C >::value
			>::type* = nullptr >
		void CreateExternalFunction( T&& closure );

		void DestroyFunction();
		void DestroyInternalFunction();
		void DestroyExternalFunction();

		void CopyFunction( const Function& other );
		void CopyInternalFunction( const Function& other );
		void CopyExternalFunction( const Function& other );

		void MoveFunction( Function&& other );
		void MoveInternalFunction( Function&& other );
		void MoveExternalFunction( Function&& other );

		mutable typename std::aligned_storage< SmallBufferOptimizationSize, Alignment >::type m_stack;
		VTablePtr m_vtablePtr;
		typename std::aligned_storage< sizeof( red::memory::Pool* ), std::alignment_of< red::memory::Pool* >::value >::type m_pool;
		Uint32 m_poolAllocationSize : 31;
		Uint32 m_mode : 1;
	};
}

#include "function.hpp"

#endif