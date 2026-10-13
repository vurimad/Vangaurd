/**
* Copyright (c) 2013 CDProjekt Red, Inc. All Rights Reserved.
*/

#ifndef _RED_TYPE_TRAITS_H_
#define _RED_TYPE_TRAITS_H_

#include <type_traits>

template < typename T >
struct TCopyableType
{
	enum
	{
		Value = std::is_scalar< T >::value
	};
};

template < typename T > struct TAllowUseAsPOD				{ enum { Value = false }; };

#define RED_ALLOW_TYPE_AS_POD(type)	\
template <>							\
struct TAllowUseAsPOD<type>			\
{									\
	enum							\
	{								\
		Value = true				\
	};								\
};

//////////////////////////////////////////////////////////////////////////
// Allows for correct string literal types in template functions that can swap between Ansi and Uni

// The only valid types for these template functions are UniChar and AnsiChar
// Anything else will break
template< typename TUndefined >	RED_INLINE const TUndefined* SelectCharType( const red::AnsiChar*, const red::UniChar* )										{ red::AnsiChar breakingLocal[ 0 ]; RED_UNUSED( breakingLocal[ 0 ] ); return nullptr; }
template <>						RED_INLINE const red::UniChar* SelectCharType< red::UniChar >( const red::AnsiChar*, const red::UniChar* u )	{ return u; }
template <>						RED_INLINE const red::AnsiChar* SelectCharType< red::AnsiChar >( const red::AnsiChar* a, const red::UniChar* )	{ return a; }

template< typename TUndefined >	RED_INLINE const TUndefined SelectCharType( const red::AnsiChar, const red::UniChar )											{ red::AnsiChar breakingLocal[ 0 ]; RED_UNUSED( breakingLocal[ 0 ] ); return 0; }
template <>						RED_INLINE const red::UniChar SelectCharType< red::UniChar >( const red::AnsiChar, const red::UniChar u )		{ return u; }
template <>						RED_INLINE const red::AnsiChar SelectCharType< red::AnsiChar >( const red::AnsiChar a, const red::UniChar )		{ return a; }

// This macro is designed, like the TXT() macro to correctly output a string literal in AnsiChar* or UniChar* format
// but for templates where the type is determined at the compilation stage rather than the pre-processor stage
// Example usage:
// 
// template< typename TChar >
// TChar* TestFunc() { return RED_TEMPLATE_TXT( TChar, "Some Text" ); }
// One level of indirection exists in this macro to allow for the txt parameter to itself also be a macro
#define _RED_TEMPLATE_TXT( type, txt ) SelectCharType< type >( txt, L##txt )
#define RED_TEMPLATE_TXT( type, txt ) _RED_TEMPLATE_TXT( type, txt )

#endif // _RED_TYPE_TRAITS_H_
