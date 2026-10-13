/**
 * Copyright (c) 2016 CD Projekt Red. All Rights Reserved.
 */

#ifndef _RED_MEMORY_OBJECT_UTILS_HPP_
#define _RED_MEMORY_OBJECT_UTILS_HPP_

namespace red
{
namespace memory
{
	template <typename T>
	struct HasMemberOperatorNewFunction
	{
		typedef char TrueType;
		typedef long FalseType;

		template< typename U > static TrueType Test( decltype(&U::operator new) );
		template< typename U > static FalseType Test(...);    

		enum { Value = sizeof(Test<T>(0)) == sizeof(TrueType) };
	};
}
}

#endif
