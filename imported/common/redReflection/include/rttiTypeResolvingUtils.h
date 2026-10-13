/**
 * Copyright (c) 2016 CD Projekt Red. All Rights Reserved.
 */
#pragma once

// Bunch of templates needed for extract inner type from complex type
namespace rtti
{
	namespace extractor
	{
		template< typename T >
		struct RttiTypeExtractor
		{
			typedef T InnerType;
		};

		template< typename T >
		struct RttiTypeExtractor<T*>
		{
			typedef T InnerType;
		};

		template<class T, Uint32 ElemCount>
		struct RttiTypeExtractor<T[ElemCount]>
		{
			typedef T InnerType;
		};

		template<template<typename> class X, typename T >
		struct RttiTypeExtractor< X< T > >
		{
			typedef T InnerType;
		};

		template<template<typename, Uint32> class X, typename T, Uint32 ElemCount >
		struct RttiTypeExtractor< X< T, ElemCount > >
		{
			typedef T InnerType;
		};
	}
}
