/**
 * Copyright (c) 2020 CD Projekt Red. All Rights Reserved.
 */

#ifndef _RED_SYSTEM_FIXED_SIZE_FUNCTION_HPP_
#define _RED_SYSTEM_FIXED_SIZE_FUNCTION_HPP_

namespace red
{
	template< typename R, typename... Args, Uint32 Capacity, Uint32 Alignment >
	FixedSizeFunction< R(Args...), Capacity, Alignment >::FixedSizeFunction()
		: m_vtablePtr(nullptr)
	{}

	template< typename R, typename... Args, Uint32 Capacity, Uint32 Alignment >
	FixedSizeFunction< R(Args...), Capacity, Alignment >::FixedSizeFunction(std::nullptr_t) noexcept
		: m_vtablePtr(nullptr)
	{}

	template< typename R, typename... Args, Uint32 Capacity, Uint32 Alignment >
	FixedSizeFunction< R(Args...), Capacity, Alignment >::~FixedSizeFunction()
	{
		DestroyFunction();
	}

	template< typename R, typename... Args, Uint32 Capacity, Uint32 Alignment >
	template< typename T, typename C,
		typename std::enable_if<
		!std::is_same< C, FixedSizeFunction< R(Args...), Capacity, Alignment > >::value
	>::type* >
		FixedSizeFunction< R(Args...), Capacity, Alignment >::FixedSizeFunction(T&& closure)
	{
		static_assert( sizeof(C) <= Capacity, "Function cannot be constructed from object with that big size" );
		static_assert( Alignment % std::alignment_of< C >::value == 0, "Function cannot be constructed from object with that big alignment" );

		static const VTable< R, Args... > s_vt{ ClosureWrapper< C >{} };
		m_vtablePtr = std::addressof(s_vt);

		CreateFunction(std::forward< T >(closure));
	}

	template< typename R, typename... Args, Uint32 Capacity, Uint32 Alignment >
	FixedSizeFunction< R(Args...), Capacity, Alignment >::FixedSizeFunction(const FixedSizeFunction& other)
		: m_vtablePtr(other.m_vtablePtr)
	{
		CopyFunction(other);
	}

	template< typename R, typename... Args, Uint32 Capacity, Uint32 Alignment >
	FixedSizeFunction< R(Args...), Capacity, Alignment >::FixedSizeFunction(FixedSizeFunction&& other) noexcept
		: m_vtablePtr(Exchange(other.m_vtablePtr, nullptr))
	{
		MoveFunction(std::move(other));
	}

	template< typename R, typename... Args, Uint32 Capacity, Uint32 Alignment >
	FixedSizeFunction< R(Args...), Capacity, Alignment >& FixedSizeFunction< R(Args...), Capacity, Alignment >::operator=(const FixedSizeFunction& other)
	{
		if (this != std::addressof(other))
		{
			DestroyFunction();

			m_vtablePtr = other.m_vtablePtr;

			CopyFunction(other);
		}

		return *this;
	}

	template< typename R, typename... Args, Uint32 Capacity, Uint32 Alignment >
	FixedSizeFunction< R(Args...), Capacity, Alignment >& FixedSizeFunction< R(Args...), Capacity, Alignment >::operator=(FixedSizeFunction&& other) noexcept
	{
		if (this != std::addressof(other))
		{
			DestroyFunction();

			m_vtablePtr = Exchange(other.m_vtablePtr, nullptr);

			MoveFunction(std::move(other));
		}
		return *this;
	}

	template< typename R, typename... Args, Uint32 Capacity, Uint32 Alignment >
	FixedSizeFunction< R(Args...), Capacity, Alignment >& FixedSizeFunction< R(Args...), Capacity, Alignment >::operator=(std::nullptr_t) noexcept
	{
		DestroyFunction();
		m_vtablePtr = nullptr;
		return *this;
	}

	template< typename R, typename... Args, Uint32 Capacity, Uint32 Alignment >
	template< typename T, typename C,
		typename std::enable_if<
		!std::is_same< C, FixedSizeFunction< R(Args...), Capacity, Alignment > >::value
	>::type* >
		FixedSizeFunction< R(Args...), Capacity, Alignment >& FixedSizeFunction< R(Args...), Capacity, Alignment >::operator=(T&& closure)
	{
		static_assert(sizeof(C) <= Capacity, "Function cannot be constructed from object with that big size");
		static_assert(Alignment % std::alignment_of< C >::value == 0, "Function cannot be constructed from object with that big alignment");

		DestroyFunction();

		static const VTable< R, Args... > s_vt{ ClosureWrapper< C >{} };
		m_vtablePtr = std::addressof(s_vt);

		CreateFunction(std::forward< T >(closure));

		return *this;
	}

	template< typename R, typename... Args, Uint32 Capacity, Uint32 Alignment >
	R FixedSizeFunction< R(Args...), Capacity, Alignment >::operator()(Args... args) const
	{
		return m_vtablePtr->invokePtr(std::addressof(m_stack), std::forward< Args >(args)...);
	}

	template< typename R, typename... Args, Uint32 Capacity, Uint32 Alignment >
	FixedSizeFunction< R(Args...), Capacity, Alignment >::operator Bool() const noexcept
	{
		return m_vtablePtr != nullptr;
	}

	template< typename R, typename... Args, Uint32 Capacity, Uint32 Alignment >
	template< typename T, typename C,
		typename std::enable_if<
		!std::is_same< C, FixedSizeFunction< R(Args...), Capacity, Alignment > >::value
	>::type* >
		RED_INLINE void FixedSizeFunction< R(Args...), Capacity, Alignment >::CreateFunction(T&& closure)
	{
		::new (std::addressof(m_stack)) C{ std::forward< T >(closure) };
	}

	template< typename R, typename... Args, Uint32 Capacity, Uint32 Alignment >
	RED_INLINE void FixedSizeFunction< R(Args...), Capacity, Alignment >::DestroyFunction()
	{
		if (m_vtablePtr)
		{
			m_vtablePtr->destructorPtr(std::addressof(m_stack));
		}
	}

	template< typename R, typename... Args, Uint32 Capacity, Uint32 Alignment >
	RED_INLINE void FixedSizeFunction< R(Args...), Capacity, Alignment >::CopyFunction(const FixedSizeFunction& other)
	{
		if (m_vtablePtr)
		{
			m_vtablePtr->copyPtr(std::addressof(m_stack), std::addressof(other.m_stack));
		}
	}

	template< typename R, typename... Args, Uint32 Capacity, Uint32 Alignment >
	RED_INLINE void FixedSizeFunction< R(Args...), Capacity, Alignment >::MoveFunction(FixedSizeFunction&& other)
	{
		if (m_vtablePtr)
		{
			m_vtablePtr->relocatePtr(std::addressof(m_stack), std::addressof(other.m_stack));
		}
	}

	// ---------------------------------------------------------

	template< typename R, typename... Args >
	FunctionPointerWrapper< R(Args...) >::FunctionPointerWrapper()
		: m_functionPtr(nullptr)
	{}

	template< typename R, typename... Args >
	template< typename C >
	FunctionPointerWrapper< R(Args...) >::FunctionPointerWrapper(C&& closure)
		: m_functionPtr(std::move(closure))
	{}

	template< typename R, typename... Args >
	R FunctionPointerWrapper< R(Args...) >::operator()(Args... args) const
	{
		return (*m_functionPtr)(std::forward< Args >(args)...);
	}

	template< typename R, typename... Args >
	FunctionPointerWrapper< R(Args...) >::operator Bool() const noexcept
	{
		return m_functionPtr != nullptr;
	}

}

#endif