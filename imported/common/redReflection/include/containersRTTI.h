/*
* Copyright (c) 2016 CD Projekt Red. All Rights Reserved.
*/

#pragma once

#include "rttiTypeName.h"

namespace red
{
	template < typename TElement > class DynArray;
	template < typename TElement, Uint32 MaxSize > class StaticArray;
	template < typename TStorage > class BitSetDynamicBase;
	using BitSetDynamic = BitSetDynamicBase< Uint32 >;
	using BitSet64Dynamic = BitSetDynamicBase< Uint64 >;
}

namespace rtti
{
	/// the actual magic
	extern RED_REFLECTION_API const CName FormatDynArrayTypeName( const CName innerTypeName );
	extern RED_REFLECTION_API const CName FormatStaticArrayTypeName( const CName innerTypeName, const Uint32 maxSize );
}

/// container -> RTTI  binding
template < typename TElement >
struct TTypeName< red::DynArray< TElement > >
{
	static const CName GetTypeName()
	{
		static CName typeName = rtti::FormatDynArrayTypeName( TTypeName< TElement >::GetTypeName() );
		return typeName;
	}
};

template < typename TElement, Uint32 MaxSize  >
struct TTypeName< red::StaticArray< TElement, MaxSize > >
{
	static const CName GetTypeName()
	{
		static CName typeName = rtti::FormatStaticArrayTypeName( TTypeName< TElement >::GetTypeName(), MaxSize );
		return typeName;
	}
};

template <>
struct TTypeName< red::BitSetDynamic >
{
	static const CName GetTypeName()
	{
		static CName typeName = RED_NAME_CONSTEXPR( "BitSetDynamic" );
		return typeName;
	}
};

template <>
struct TTypeName< red::BitSet64Dynamic >
{
	static const CName GetTypeName()
	{
		static CName typeName = RED_NAME_CONSTEXPR( "BitSet64Dynamic" );
		return typeName;
	}
};