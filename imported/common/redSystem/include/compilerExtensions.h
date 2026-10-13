/**
* Copyright (c) 2013 CDProjekt Red, Inc. All Rights Reserved.
*/

#ifndef _RED_COMPILER_EXTENSIONS_H_
#define _RED_COMPILER_EXTENSIONS_H_

#ifdef _MSC_VER
#ifndef _ENABLE_EXTENDED_ALIGNED_STORAGE
#define _ENABLE_EXTENDED_ALIGNED_STORAGE
#endif
#endif

#include <type_traits>

// C++20
namespace red
{
	template< class T >
	struct RemoveCVRef
	{
		using Type = typename std::remove_cv< typename std::remove_reference< T >::type >::type;
	};
}

#define RED_FIELD_DECLTYPE_NOCVREF(class_, field_)  typename ::red::RemoveCVRef< decltype(::std::declval<class_>().field_) >::Type

#define USE_STATIC_ANALYSIS

//#tbd: clang  __attribute__ ((format(printf)))
// but currently useless because of %hs, and crippled with template chars and printf vs wprintf
#if defined( RED_COMPILER_MSC )
# include <sal.h>
# define STATIC_CHECK_PRINTF_MSC _Printf_format_string_
# define STATIC_CHECK_USE_DECL _Use_decl_annotations_
# define RED_ANALYSIS_ASSUME(x) __analysis_assume((x))
# if 0 //_MSC_VER >= 1910 // Requires VS2017 *and* C++17 enabled vs forced to 14
# define RED_NODISCARD [[nodiscard]]
# else
# define RED_NODISCARD __checkReturn
# define RED_ANALYSIS_NORETURN_CLANG_ATTR
# define RED_NO_SANITIZE_CLANG_ATTR(flags)
#endif
#elif defined( RED_COMPILER_CLANG )
# define STATIC_CHECK_PRINTF_MSC
# define STATIC_CHECK_USE_DECL
//# define RED_ANALYSIS_NORETURN [[analyzer_noreturn]]
# define RED_ANALYSIS_NORETURN_CLANG_ATTR __attribute__((analyzer_noreturn))
# define RED_NO_SANITIZE_CLANG_ATTR(flags) __attribute__((no_sanitize(flags)))
# define RED_ANALYSIS_ASSUME(x)
# define RED_NODISCARD [[ nodiscard ]]
#else
# error Unsupported compiler!
#endif

#define RED_CONCATENATE(x,y) x##y
#define RED_CONCATENATE2(x,y) RED_CONCATENATE(x,y)
#define RED_EXPAND(...) __VA_ARGS__

//FIXME>>>>>: On Clang __COUNTER__ is unique per translation unit. So for our current use, we need to make variables declared with it static in the .cpp
#define RED_UNIQUE_NAME(prefix) RED_CONCATENATE2(prefix,__COUNTER__)

#if defined( RED_COMPILER_CLANG )
# define RED_FUNCTION __PRETTY_FUNCTION__
#elif defined( RED_COMPILER_MSC )
# define RED_FUNCTION __FUNCTION__
#else
# error Unsupported compiler!
#endif

// Used when TXT() is required by a macro
#define MACRO_TXT( x )	TXT( x )

// Used when you need to turn a macro parameter into a string literal
#define RED_STRINGIFY( x ) #x

// Used when you want to expand x macro first before turning it into string literal
#define RED_EXPAND_AND_STRINGIFY( x ) RED_STRINGIFY( x )

#define RED_STRINGIFY_AND_CONCATENATE( x, y ) #x #y

// For when a parameter is defined, but not yet used (If this particular version of the function does not require the variable, remove the identifier from the function signature instead)
#define RED_UNUSED( x )	(void)x
#define RED_UNUSED2( x, y ) RED_UNUSED(x); RED_UNUSED(y)
#define RED_UNUSED3( x, y, z ) RED_UNUSED2(x,y); RED_UNUSED(z)
#define RED_UNUSED4( x, y, z, w ) RED_UNUSED2(x,y); RED_UNUSED2(z,w)

// For when the variable is unused in some configs to quiet compiler warnings - don't lie and call it RED_UNUSED globally
#define RED_TOUCH( x )	(void)x
#define RED_TOUCH2( x, y ) RED_TOUCH((x)); RED_TOUCH((y))
#define RED_TOUCH3( x, y, z ) RED_TOUCH2(x,y); RED_TOUCH(z)
#define RED_TOUCH4( x, y, z, w ) RED_TOUCH2(x,y); RED_TOUCH2(z,w)

#if ( defined( RED_COMPILER_MSC ) && _MSC_VER >= 1900 ) || ( defined( RED_COMPILER_CLANG ) )
# define RED_HAS_CONST_EXPR
#endif

#ifdef RED_HAS_CONST_EXPR
# define RED_CONSTEXPR_ASSERT static_assert
#else
# define RED_CONSTEXPR_ASSERT RED_FATAL_ASSERT
#endif

#if defined( RED_HAS_CONST_EXPR )
template<typename E>
constexpr auto red_autocast_enum(E e) -> typename std::underlying_type<E>::type
{
	return static_cast< typename std::underlying_type<E>::type >( e );
}
# define RED_AUTOCAST_ENUM(x) red_autocast_enum((x))
#elif defined( RED_COMPILER_MSC )
// MSVC doesn't seem to care about it being strictly non-convertable
# define RED_AUTOCAST_ENUM(x) x
#else
// Or just define it as passthrough
# error Unsupported platform!
#endif

//////////////////////////////////////////////////////////////////////////
// Non standard extensions
#if defined( RED_COMPILER_MSC )

#	define RED_NOINLINE __declspec( noinline )

#	define RED_ALIGNED_CLASS( type, alignment )		class __declspec( align( alignment ) )  type
#	define RED_ALIGNED_STRUCT( type, alignment )	struct __declspec( align( alignment ) ) type
#	define RED_ALIGNED_ANON_STRUCT( alignment ) struct __declspec( align( alignment ) )
#	define RED_ALIGNED_VAR( type, alignment )		__declspec( align( alignment ) ) type
#	define RED_ALIGN( alignment )					__declspec( align( alignment ) )
#	define RED_ALIGNED_CLASS_API( type, api, alignment )		class api __declspec( align( alignment ) )  type
#	define RED_ALIGNED_STRUCT_API( type, api, alignment )	struct api __declspec( align( alignment ) ) type

// Compiler Pointer optimisation hints
#	define RED_RESTRICT_RETURN					__declspec( restrict )
#	define RED_RESTRICT_PARAMS					__declspec( noalias )
#	define RED_RESTRICT_LOCAL					__restrict

// Miscellaneous optimisation hints
#	define RED_ASSUME( expression )				__assume( expression )

// Function call conventions
#	define RED_CDECL							__cdecl					// Default C++ calling convention - required for variadic functions
#	define RED_STDCALL							__stdcall				// Windows API function calling convention
#	define RED_FASTCALL							__fastcall
#	define RED_VECTORCALL						__vectorcall

#	define RED_INLINE							inline
#	define RED_FORCE_INLINE						__forceinline

#	define RED_LIKELY( condition )				( condition )
#	define RED_UNLIKELY( condition )			( condition )

#	define NOEXCEPT
#	define FUNC_NOOPTS

// Class extensions
#	define RED_PURE_INTERFACE(cls_)				__declspec(novtable) cls_ abstract

// Warning suppression
#	define RED_DISABLE_WARNING_MSC( n )			__pragma( warning( disable : n ) )
#	define RED_DISABLE_WARNING_CLANG( a )

#	define RED_WARNING_PUSH()					__pragma( warning( push ) )
#	define RED_WARNING_POP()					__pragma( warning( pop ) )

// Mark something (function, type, identifier, etc) as deprecated
#	define RED_DEPRECATED( identifier )			__pragma( deprecated( identifier ) )

//	Compile time Messages
#	define RED_MESSAGE_INTERNAL( text, line )	__pragma( message( __FILE__ ##"(" RED_STRINGIFY( line ) ##") : " ##text ) )
#	define RED_MESSAGE( text )					RED_MESSAGE_INTERNAL( text, __LINE__ )

#	define NOEXCEPT
#	define FUNC_NOOPTS

#	define RED_PLATFORM_LIBRARY_EXT				lib

// The following macro "RED_NO_EMPTY_FILE()" can be put into a file
// in order suppress the MS Visual C++ Linker warning 4221
//
// warning LNK4221: no public symbols found; archive member will be inaccessible
//
// This warning occurs on PC and XBOX when a file compiles out completely
// has no externally visible symbols which may be dependant on configuration
// #defines and options.

#ifdef _CPPRTTI
	#define RED_HAS_NATIVE_RTTI
#endif

#	define RED_NO_EMPTY_FILE()   namespace { char noEmptyFileDummy##__LINE__; }

#elif defined( RED_COMPILER_CLANG )

#	define RED_CLANG_ALIGNAS( alignment ) alignas( alignment )
#	define RED_NOINLINE __attribute__((noinline))

// Struct / variable alignment
#	define RED_ALIGNED_CLASS( type, alignment )		class RED_CLANG_ALIGNAS( alignment )  type
#	define RED_ALIGNED_STRUCT( type, alignment )	struct RED_CLANG_ALIGNAS( alignment )  type
#	define RED_ALIGNED_ANON_STRUCT( alignment )		struct RED_CLANG_ALIGNAS( alignment )
#	define RED_ALIGNED_VAR( type, alignment )		RED_CLANG_ALIGNAS( alignment )  type
#	define RED_ALIGN( alignment )					RED_CLANG_ALIGNAS( alignment )

#	define RED_ALIGNED_CLASS_API( type, api, alignment )	class RED_CLANG_ALIGNAS( alignment )  type
#	define RED_ALIGNED_STRUCT_API( type, api, alignment )	struct RED_CLANG_ALIGNAS( alignment )  type

// Compiler Pointer optimisation hints
#	define RED_RESTRICT_RETURN
#	define RED_RESTRICT_PARAMS
#	define RED_RESTRICT_LOCAL

// Miscellaneous optimisation hints
#	define RED_ASSUME( expression )

// Function call conventions
// FIXME redSystem. Supports e.g., __attribute__((cdecl)), but this is probably preferable for the game
#	define RED_CDECL
#	define RED_STDCALL
#	define RED_FASTCALL
#	define RED_VECTORCALL

#	define RED_INLINE						inline

// Note: may cause a warning if fails to inline
#if defined( RED_CONFIGURATION_DEBUG ) || defined( RED_CONFIGURATION_NOPTS )
#	define RED_FORCE_INLINE					inline
#else
#	define RED_FORCE_INLINE					inline __attribute__((always_inline))
#endif

#	define RED_LIKELY( condition )			__builtin_expect( ( condition ), 1 )
#	define RED_UNLIKELY( condition )		__builtin_expect( ( condition ), 0 )

#	define NOEXCEPT							noexcept
#	define FUNC_NOOPTS						__attribute__((optnone))

// Class extensions
#	define RED_PURE_INTERFACE(cls_)			cls_

#	define RED_CLANG_PRAGMA_INTERNAL( a )	_Pragma( #a )

// Warning suppression
#	define RED_DISABLE_WARNING_MSC( n )
#	define RED_DISABLE_WARNING_CLANG( a )	RED_CLANG_PRAGMA_INTERNAL( clang diagnostic ignored a )

#	define RED_WARNING_PUSH()				RED_CLANG_PRAGMA_INTERNAL( clang diagnostic push )
#	define RED_WARNING_POP()				RED_CLANG_PRAGMA_INTERNAL( clang diagnostic pop )

// Clang only supports deprecating a feature when it's declared (as an __attribute__)
#	define RED_DEPRECATED( identifier )

//	Compile time Messages
#ifndef RED_PLATFORM_LINUX
#	define RED_MESSAGE( text )				RED_CLANG_PRAGMA_INTERNAL( message text )
#else
#   define RED_MESSAGE( text )              // clang on linux is treating this as warnings so we are ignoring them for now
#endif
#	define RED_PLATFORM_LIBRARY_EXT			a

#	define NOEXCEPT							noexcept
#	define FUNC_NOOPTS						__attribute__((optnone))

#	define RED_NO_EMPTY_FILE()

#ifdef __GXX_RTTI
	#define RED_HAS_NATIVE_RTTI
#endif

#endif

#ifndef RED_CONFIGURATION_FINAL
#	define RED_MOCKABLE virtual
#else
#	define RED_MOCKABLE
#endif

// portable RED_DEBUG_BREAK macro
#if defined( RED_PLATFORM_WINPC )
# define RED_DEBUG_BREAK() __debugbreak()
#elif defined( RED_PLATFORM_DURANGO )
// __debugbreak() can freeze the Xbox if no debugger attached.
# define RED_DEBUG_BREAK()\
	do {\
		if ( ::IsDebuggerPresent() )\
		{\
			__debugbreak();\
		}\
		else\
		{\
			::RaiseException( 1, 0, 0, nullptr );\
		}\
	} while(0,0)
#elif defined( RED_PLATFORM_ORBIS )
# define RED_DEBUG_BREAK() _SCE_BREAK()
#elif defined( RED_PLATFORM_LINUX )
# define RED_DEBUG_BREAK() asm("int3")
#endif

//////////////////////////////////////////////////////////////////////////
// Debugging
#if defined( RED_PLATFORM_WIN32 ) || defined( RED_PLATFORM_WIN64 ) || defined( RED_PLATFORM_DURANGO )
#	define RED_BREAKPOINT() do{ if( ::IsDebuggerPresent() ){ __debugbreak(); } } while(0,0)
#elif defined( RED_PLATFORM_ORBIS )
# ifndef RED_CONFIGURATION_FINAL
#	define RED_BREAKPOINT() do{ if( ::sceDbgIsDebuggerAttached() ) { RED_DEBUG_BREAK(); } } while((void)0,0)
# else
#   define RED_BREAKPOINT()
# endif
#elif defined( RED_PLATFORM_LINUX )
#	include <sys/types.h>
#	include <sys/ptrace.h>

#	define RED_BREAKPOINT() do {											\
		bool debuggerPresent = ( ptrace( PTRACE_TRACEME, 0, 1, 0 ) < 0 );	\
		if ( !debuggerPresent ) { ptrace( PTRACE_DETACH, 0, 1, 0 ); }		\
		if( debuggerPresent) { RED_DEBUG_BREAK(); } } while((void)0,0)
#endif

//////////////////////////////////////////////////////////////////////////
// Ugly solution but it works and forces linker to add file which uses this macro to final build
#define INDIRECT_COMBINE(_1,_2) _1##_2
#define COMBINE(_1,_2) INDIRECT_COMBINE( _1, _2 )
#define RED_FORCE_LINK_THIS_FILE(x) char COMBINE( GForceLink_, x ) = '0';
#define RED_FORCE_LINK_THAT_FILE(x) { extern char COMBINE( GForceLink_, x ); COMBINE( GForceLink_, x ) = '1'; }

#endif // _RED_COMPILER_EXTENSIONS_H_
