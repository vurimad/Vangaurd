/*
* Copyright (c) 2015 CD Projekt Red. All Rights Reserved.
*/

#pragma once

template < class H, class T >
struct TypeList
{
	typedef H Head;
	typedef T Tail;
};

namespace TypeListUtils
{
	struct NullType
	{

	};

	template
		<
		typename T1  = NullType, typename T2  = NullType, typename T3  = NullType,
		typename T4  = NullType, typename T5  = NullType, typename T6  = NullType,
		typename T7  = NullType, typename T8  = NullType, typename T9  = NullType,
		typename T10 = NullType, typename T11 = NullType, typename T12 = NullType,
		typename T13 = NullType, typename T14 = NullType, typename T15 = NullType,
		typename T16 = NullType, typename T17 = NullType, typename T18 = NullType
		>
	struct MakeTypelist
	{
	private:
		typedef typename MakeTypelist
			<
			T2 , T3 , T4 ,
			T5 , T6 , T7 ,
			T8 , T9 , T10,
			T11, T12, T13,
			T14, T15, T16,
			T17, T18
			>
			::Result TailResult;

	public:
		typedef TypeList<T1, TailResult> Result;
	};

	template<>
	struct MakeTypelist<>
	{
		typedef NullType Result;
	};

	///////////////////////////////////////////////////////////////////////////////
	// Length implementation
	template < class TList >
	struct Length
	{
		enum { Value = 1 + Length< typename TList::Tail >::Value };
	};
	template <> struct Length< NullType >
	{
		enum { Value = 0 };
	};
	////////////////////////////////////////////////////////////////////////////////
	// class template PushBack
	// Adds a type or a typelist to another
	// Invocation (TList is a typelist and T is either a type or a typelist):
	// PushBack<TList, T>::Result
	// returns a typelist that is TList followed by T and NullType-terminated
	////////////////////////////////////////////////////////////////////////////////

	template < class TList, class T > struct PushBack;
	//{
	//	static_assert( false, "INVALID TypeListUtils::PushBack istantiation. Check if first argument is a list (or NullType)" );
	//	typedef 
	//};

	template <> struct PushBack< NullType, NullType >
	{
		typedef NullType Result;
	};

	template < class T > struct PushBack< NullType, T >
	{
		typedef TypeList< T,NullType > Result;
	};

	template <class Head, class Tail>
	struct PushBack< NullType, TypeList< Head, Tail > >
	{
		typedef TypeList<Head, Tail> Result;
	};

	template <class Head, class Tail, class T>
	struct PushBack< TypeList< Head, Tail >, T >
	{
		typedef TypeList< Head, typename PushBack< Tail, T >::Result > Result;
	};

	template < class TList, class T >
	struct PushFront
	{
		typedef TypeList< T, TList > Result;
	};

	template < class T >
	struct PushFront< NullType, T >
	{
		typedef TypeList< T, NullType > Result;
	};

	////////////////////////////////////////////////////////////////////////////////
	// FilterElements
	// Filters template list and erases elements, for which
	// TFilter< TElement >::Value is false.
	////////////////////////////////////////////////////////////////////////////////
	template < class TOptionTrue, class TOptionFalse, Bool val >
	struct SelectOption
	{
		typedef TOptionTrue Result;
	};

	template < class TOptionTrue, class TOptionFalse >
	struct SelectOption< TOptionTrue, TOptionFalse, false >
	{
		typedef TOptionFalse Result;
	};

	template < class TList, template< class H > class TFilter >
	struct FilterElements
	{
	private:
		typedef typename FilterElements< typename TList::Tail, TFilter >::Result TailResult;
		typedef typename FilterElements< typename TList::Tail, TFilter >::FilteredResult TailFilteredResult;

		struct FalseHelperClass
		{
			typedef TailResult Result;
			typedef TypeList< typename TList::Head, TailFilteredResult > FilteredResult;
		};
		struct TrueHelperClass
		{
			typedef TypeList< typename TList::Head, TailResult > Result;
			typedef TailFilteredResult FilteredResult;
		};

	public:
		typedef typename SelectOption< TrueHelperClass, FalseHelperClass, TFilter< typename TList::Head >::Value >::Result ComputedHelper;
		typedef typename ComputedHelper::Result Result;
		typedef typename ComputedHelper::FilteredResult FilteredResult;
	};

	template < template< class H > class TFilter >
	struct FilterElements< NullType, TFilter >
	{
		typedef NullType Result;
		typedef NullType FilteredResult;
	};

};			// namespace TypeListUtils

