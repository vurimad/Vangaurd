#pragma once

#define INDIRECT_EXPAND( x )  x

#define GET_MACRO_FOR_ARG_NR5(_1,_2,_3,_4,_5,NAME,...) NAME
#define BUILD_NAMESPACE5(_1,_2,_3,_4,_5) _1::_2::_3::_4::_5
#define BUILD_NAMESPACE4(_1,_2,_3,_4) _1::_2::_3::_4
#define BUILD_NAMESPACE3(_1,_2,_3) _1::_2::_3
#define BUILD_NAMESPACE2(_1,_2) _1::_2
#define BUILD_NAMESPACE1(_1) _1
#define BUILD_NAMESPACE(...) INDIRECT_EXPAND( GET_MACRO_FOR_ARG_NR5(__VA_ARGS__, BUILD_NAMESPACE5, BUILD_NAMESPACE4, BUILD_NAMESPACE3, BUILD_NAMESPACE2, BUILD_NAMESPACE1 )(__VA_ARGS__) )

#define DECLARE_IN_NAMESPACE5(_typeAndName,_1,_2,_3,_4,_5) namespace _1 { namespace _2 { namespace _3 { namespace _4 { namespace _5 { _typeAndName; } } } } }
#define DECLARE_IN_NAMESPACE4(_typeAndName,_1,_2,_3,_4) namespace _1 { namespace _2 { namespace _3 { namespace _4 { _typeAndName; } } } }
#define DECLARE_IN_NAMESPACE3(_typeAndName,_1,_2,_3) namespace _1 { namespace _2 { namespace _3 { _typeAndName; } } }
#define DECLARE_IN_NAMESPACE2(_typeAndName,_1,_2) namespace _1 { namespace _2 { _typeAndName; } }
#define DECLARE_IN_NAMESPACE1(_typeAndName,_1) namespace _1 { _typeAndName; }
#define DECLARE_IN_NAMESPACE(_typeAndName, ...) INDIRECT_EXPAND( GET_MACRO_FOR_ARG_NR5( __VA_ARGS__, DECLARE_IN_NAMESPACE5, DECLARE_IN_NAMESPACE4, DECLARE_IN_NAMESPACE3, DECLARE_IN_NAMESPACE2, DECLARE_IN_NAMESPACE1 )( _typeAndName, __VA_ARGS__ ) )

#define TO_STRING_MACRO5(_1,_2,_3,_4,_5) #_1 #_2 #_3 #_4 #_5
#define TO_STRING_MACRO4(_1,_2,_3,_4) #_1 #_2 #_3 #_4
#define TO_STRING_MACRO3(_1,_2,_3) #_1 #_2 #_3
#define TO_STRING_MACRO2(_1,_2) #_1 #_2
#define TO_STRING_MACRO1(_1) #_1
#define TO_STRING_MACRO(...) INDIRECT_EXPAND( GET_MACRO_FOR_ARG_NR5(__VA_ARGS__, TO_STRING_MACRO5, TO_STRING_MACRO4, TO_STRING_MACRO3, TO_STRING_MACRO2, TO_STRING_MACRO1 )(__VA_ARGS__) )

#define GET_MACRO_FOR_ARG_NR8(_1,_2,_3,_4,_5,_6,_7,_8,NAME,...) NAME
#define JTM8(_1,_2,_3,_4,_5,_6,_7,_8) JTM2( JTM7(_1, _2, _3, _4, _5, _6, _7), _8)
#define JTM7(_1,_2,_3,_4,_5,_6,_7) JTM2( JTM6(_1, _2, _3, _4, _5, _6), _7)
#define JTM6(_1,_2,_3,_4,_5,_6) JTM2( JTM5(_1, _2, _3, _4, _5), _6)
#define JTM5(_1,_2,_3,_4,_5) JTM2( JTM4(_1, _2, _3, _4), _5)
#define JTM4(_1,_2,_3,_4) JTM2( JTM3(_1, _2, _3), _4)
#define JTM3(_1,_2,_3) JTM2( JTM2( _1, _2), _3 )
#define JTM2_IN(_1,_2) _1##_2
#define JTM2(_1,_2) JTM2_IN( _1, _2 )
#define JTM1(_1) _1
#define JOIN_TOKENS_MACRO(...) INDIRECT_EXPAND( GET_MACRO_FOR_ARG_NR8(__VA_ARGS__, JTM8, JTM7, JTM6, JTM5, JTM4, JTM3, JTM2, JTM1 )(__VA_ARGS__) )
