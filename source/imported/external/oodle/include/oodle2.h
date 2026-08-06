// Publicate 0 to v:\devel\projects\oodle2\include\oodle2.h

//===================================================
// Oodle2 Core header
// (C) Copyright 1994-2018 RAD Game Tools, Inc.  
//===================================================

#ifndef __OODLE2_H_INCLUDED__
#define __OODLE2_H_INCLUDED__

#ifndef OODLE2_PUBLIC_HEADER
#define OODLE2_PUBLIC_HEADER 1
#endif

#ifdef _MSC_VER
#pragma pack(push, Oodle, 8)

#pragma warning(push)
#pragma warning(disable : 4127) // conditional is constant
#endif

//===============================================
// (C) Copyright 1994-2018 RAD Game Tools, Inc.  
//===============================================

#if !defined(__RADTYPESH__) && !defined(__RADRR_COREH__)
#define __RADTYPESH__
#define __RADRR_COREH__ // block old rr_core

#define RADCOPYRIGHT "Copyright (C) 1994-2018, RAD Game Tools, Inc."

#if !defined(__RADRES__) // don't include anything for resource compiles

//  __RAD32__ means at least 32 bit code (always defined)
//  __RAD64__ means 64 bit code (64-bit OSes only)

//  __RADNT__ means Win32 and Win64 desktop
//  __RADWINRT__ means Windows Store/Phone App (x86, x64, arm)
//  __RADWIN__ means win32, win64, windows store/phone, xenon, durango
//  __RADWINRTAPI__ means Windows RT API (Win Store, Win Phone, Durango)
//  __RADMAC__ means MacOS (32 or 64-bit)
//  __RADXENON__ means the Xbox360 console
//  __RADXBOXONE__ means Xbox One
//  __RADWIIU__ means the Nintendo Wii U
//  __RADNX__ means the Nintendo NX
//  __RADPS3__ means the Sony PlayStation 3
//  __RADPS4__ means the Sony PlayStation 4
//  __RADANDROID__ means Android NDK
//  __RADNACL__ means Native Client SDK
//  __RADLINUX__ means Linux (32 or 64-bit)
//  __RADPSP2__ means PS Vita
//  __RADQNX__ means QNX
//  __RADIPHONE__ means iphone

//  __RADARM__ means arm
//  __RADPPC__ means powerpc
//  __RADX86__ means x86 or x64
//  __RADX64__ means x64
//  __RADNEON__ means you can use NEON intrinsics on ARM

// __RADNOVARARGMACROS__ means #defines can't use ...

// RADDEFSTART is "extern "C" {" on C++, nothing on C
// RADDEFEND is "}" on C++, nothing on C

// RADEXPFUNC and RADEXPLINK are used on both the declaration
//   and definition of exported functions.
//    RADEXPFUNC int RADEXPLINK exported_func()

// RADDEFFUNC and RADLINK are used on both the declaration
//   and definition of public functions (but not exported).
//    RADDEFFUNC int RADLINK public_c_func()

// RADRESTRICT is for non-aliasing pointers/

// RADSTRUCT is defined as "struct" on msvc, and
//   "struct __attribute__((__packed__))" on gcc/clang
//   Used to sort of address generic structure packing
//   (we still require #pragma packs to fix the
//   packing on windows, though)


// ========================================================
// First off, we detect your platform

#if defined(ANDROID)
  #define __RADANDROID__ 1
  #define __RADDETECTED__ __RADANDROID__
#endif

#if defined(__QNX__)
  #define __RADQNX__ 2
  #define __RADDETECTED__ __RADQNX__
#endif

#if defined(__linux__) && !defined(ANDROID)
  #define __RADLINUX__ 3
  #define __RADDETECTED__ __RADLINUX__
#endif

#if defined(__native_client__)
  #define __RADNACL__ 4
  #define __RADDETECTED__ __RADNACL__
#endif

#if defined(_DURANGO) || defined(_SEKRIT) || defined(_SEKRIT1) || defined(_XBOX_ONE)
  #define __RADXBOXONE__ 5
  #define __RADDETECTED__ __RADXBOXONE__
#endif

#if defined(__ORBIS__)
  #define __RADPS4__ 6
  #define __RADDETECTED__ __RADPS4__
#endif

#if defined(CAFE) 
  #define __RADWIIU__ 7
  #define __RADDETECTED__ __RADWIIU__
#endif

#if defined(__psp2__)
  #define __RADPSP2__ 9
  #define __RADDETECTED__ __RADPSP2__
#endif

#if defined(__CELLOS_LV2__)
  #ifdef __SPU__
    #define __RADSPU__ 10
    #define __RADDETECTED__ __RADSPU__
  #else
    #define __RADPS3__ 11
    #define __RADDETECTED__ __RADPS3__
  #endif
#endif

#if defined(_XENON) || ( defined(_XBOX_VER) && (_XBOX_VER == 200) )
  #define __RADXENON__ 12
  #define __RADDETECTED__ __RADXENON__
#endif

#if !defined(__RADXENON__) && !defined(__RADXBOXONE__) &&( defined(_Windows) || defined(WIN32) || defined(__WINDOWS__) || defined(_WIN32) || defined(_WIN64) || defined(WINAPI_FAMILY) )

  #ifdef WINAPI_FAMILY
    // If this is #defined, we might be in a Windows Store App. But
    // VC++ by default #defines this to a symbolic name, not an integer
    // value, and those names are defined in "winapifamily.h". So if
    // WINAPI_FAMILY is #defined, #include the header so we can parse it.
    #include <winapifamily.h>
    #define RAD_WINAPI_IS_APP (!WINAPI_FAMILY_PARTITION(WINAPI_PARTITION_DESKTOP))
    #if RAD_WINAPI_IS_APP
      #define __RADWINRTAPI__
      #define __RADWINRT__ 13
      #define __RADDETECTED__ __RADWINRT__
    #endif
  #else
    #define RAD_WINAPI_IS_APP 0
  #endif

  #ifndef __RADWINRT__
    // if we aren't WinRT, then we are plain old NT
    #define __RADNT__ 14
    #define __RADDETECTED__ __RADNT__
  #endif
#endif

#if defined(__APPLE__)
  #include "TargetConditionals.h"
  #if defined(TARGET_IPHONE_SIMULATOR) && TARGET_IPHONE_SIMULATOR
    #define __RADIPHONE__ 15
    #define __RADIPHONESIM__ 16
    #define __RADDETECTED__ __RADIPHONESIM__
  #elif defined(TARGET_OS_IPHONE) && TARGET_OS_IPHONE
    #define __RADIPHONE__ 15
    #define __RADDETECTED__ __RADIPHONE__
  #else
    #define __RADMAC__ 17
    #define __RADDETECTED__ __RADMAC__
  #endif
#endif

#if defined(NN_NINTENDO_SDK)
  #define __RADNX__  18
  #define __RADDETECTED__ __RADNX__
#endif

#if !__RADDETECTED__
  #error "radtypes.h did not detect your platform."
#endif

// ========================================================
// Now detect some architexture stuff

#define __RAD32__ // we have no non-at-least-32-bit cpus any more

#if defined(__arm__) || defined( _M_ARM )
  #define __RADARM__ 1
  #define __RADDETECTEDPROC__ __RADARM__
  #define __RADLITTLEENDIAN__
#endif
#if defined(__i386) || defined( __i386__ ) || defined( _M_IX86 ) || defined( _X86_ )
  #define __RADX86__ 2
  #if !defined __RADIPHONESIM__
    // only use mmx on PC, Win, Linux - not iphone sim!
    #define __RADMMX__
  #endif
  #define __RADDETECTEDPROC__ __RADX86__
  #define __RADLITTLEENDIAN__
#endif
#if defined(_x86_64) || defined( __x86_64__ ) || defined( _M_X64 ) || defined( _M_AMD64 )
  #define __RADX86__ 2
  #define __RADX64__ 3
  #if !defined __RADIPHONESIM__
    #define __RADMMX__
  #endif
  #define __RADDETECTEDPROC__ __RADX64__
  #define __RADLITTLEENDIAN__
#endif
#if defined(__powerpc) || defined( _M_PPC ) || defined( CAFE ) || defined( _XENON ) || (defined( __CELLOS_LV2__ ) && !defined( __SPU__ ))
  #define __RADPPC__ 4
  #if !defined( CAFE ) 
    #define __RADALTIVEC__
  #endif
  #define __RADDETECTEDPROC__ __RADPPC__
  #define __RADBIGENDIAN__
#endif
#if defined( __CELLOS_LV2__ ) && defined( __SPU__ )
  #define __RADCELLSPU__ 5 
  #define __RADDETECTEDPROC__ __RADCELLSPU__
  #define __RADBIGENDIAN__
#endif
#if defined( __aarch64__ ) || defined( __arm64__ )
  #define __RADARM__ 1
  #define __RADARM64__ 6
  #define __RADDETECTEDPROC__ __RADARM64__
  #define __RADLITTLEENDIAN__
#endif

#if !defined(__RADDETECTEDPROC__)
  #error "radtypes.h did not detect your processor type."
#endif

#if defined(__ppc64__) || defined(__aarch64__) || defined(_M_X64) || defined(__x86_64__) || defined(__x86_64)
  #define __RAD64__
  #define __RAD64REGS__  // need to set this for platforms that aren't 64-bit, but have 64-bit regs (xenon, ps3)
#endif


// ========================================================
// C++ name demangaling nonsense

#ifdef __cplusplus
  #define RADDEFFUNC extern "C"
  #define RADDEFSTART extern "C" {
  #define RADDEFEND }
  #define RADDEFAULT( val ) =val
#else
  #define RADDEFFUNC
  #define RADDEFSTART
  #define RADDEFEND
  #define RADDEFAULT( val )
#endif

// ========================================================
// handle exported function declarations:
//   in anything with __RADNOEXPORTS__, RADEXPFUNC == nothing (turn off exports with this flag)
//   in DLL, RADEXPFUNC == DLL export
//   in EXE, RADEXPFUNC == DLL import
//   in EXE with RADNOEXEEXPORTS, RADEXPFUNC == nothing (turn off imports in EXE with this flag)
//   in static lib, RADEXPFUNC == nothing
#if ( defined(__RADINSTATICLIB__) || defined(__RADNOEXPORTS__ ) || ( defined(__RADNOEXEEXPORTS__) && ( !defined(__RADINDLL__) ) && ( !defined(__RADINSTATICLIB__) ) ) )
  // if we are in a static lib, or exports are off, or if we are in an EXE we asked for no exe exports (or imports)
  //   then EXPFUNC is just a normal function
  #define RADEXPFUNC RADDEFFUNC
#else
  // otherwise, we use imprt or export base on the build flag __RADINDLL__
  #if defined(__RADINDLL__) 
    #define RADEXPFUNC RADDEFFUNC RADDLLEXPORTDLL
  #else
    #define RADEXPFUNC RADDEFFUNC RADDLLIMPORTDLL
  #endif
#endif

#if defined(__RADANDROID__)
  #if defined(__RADARM64__)
    // always neon in ARM64
    #define __RADNEON__
  #elif defined(__RADARM__) && defined(__ARM_NEON__)
    #define __RADNEON__
  #endif
  #define RADRESTRICT __restrict
  #define RADSTRUCT struct __attribute__((__packed__))

  #define RADLINK
  #define RADEXPLINK
  #define RADDLLEXPORTDLL __attribute__((visibility("default")))
  #define RADDLLIMPORTDLL
#endif

#if defined(__RADQNX__)
  #define RADRESTRICT __restrict
  #define RADSTRUCT struct __attribute__((__packed__))

  #define RADLINK
  #define RADEXPLINK RADLINK
  #define RADDLLEXPORTDLL
  #define RADDLLIMPORTDLL
#endif

#if defined(__RADLINUX__)
  #define RADRESTRICT __restrict
  #define RADSTRUCT struct __attribute__((__packed__))

  #if defined(__RADX86__) && !defined(__RADX64__)
    #define RADLINK __attribute__((cdecl))
    #define RADEXPLINK __attribute__((cdecl))
  #else
    #define RADLINK
    #define RADEXPLINK
  #endif
  // for linux, we assume you are building with hidden visibility,
  //   so for RADEXPFUNC, we turn the vis back on...
  #define RADDLLEXPORTDLL __attribute__((visibility("default")))
  #define RADDLLIMPORTDLL
#endif

#if defined(__RADNACL__)
  #define RADRESTRICT __restrict
  #define RADSTRUCT struct __attribute__((__packed__))

  #define RADLINK
  #define RADEXPLINK
  #define RADDLLEXPORTDLL
  #define RADDLLIMPORTDLL
#endif

#if defined(__RADXBOXONE__)
  #define __RADDURANGO__  
  #define __RADWIN__  
  #define __RADSEKRIT__
  
  #define __RADWIN__
  #define RADRESTRICT __restrict
  #define RADSTRUCT struct 
  #define __RADWINRTAPI__

  #define RADLINK __stdcall
  #define RADEXPLINK __stdcall
  #define RADDLLEXPORTDLL __declspec(dllexport) 
  #define RADDLLIMPORTDLL // we don't mark the import functions with dllimport, so we can link to the static lib *or* dll...
#endif

#if defined(__RADPS4__)
  #define __RADSEKRIT2__
  #define RADRESTRICT __restrict
  #define RADSTRUCT struct __attribute__((__packed__))

  #define RADLINK
  #define RADEXPLINK
  #define RADDLLEXPORTDLL __declspec(dllexport)  __attribute__((visibility("default")))
  #define RADDLLIMPORTDLL 
#endif

#if defined(__RADNX__)
  #if defined(__RADARM64__)
    // always neon in ARM64
    #define __RADNEON__
  #elif defined(__RADARM__) && defined(__ARM_NEON__)
    #define __RADNEON__
  #endif
  #define RADRESTRICT __restrict
  #define RADSTRUCT struct __attribute__((__packed__))

  #define RADLINK
  #define RADEXPLINK
  #define RADDLLEXPORTDLL __declspec(dllexport)  __attribute__((visibility("default")))
  #define RADDLLIMPORTDLL 
#endif

#if defined(__RADNT__)
  #define __RADWIN__
  #if _MSC_VER >= 1400
    #define RADRESTRICT __restrict
  #else
    // vc6 and older
    #define RADRESTRICT
    #define __RADNOVARARGMACROS__
  #endif
  #define RADSTRUCT struct 

  #define RADLINK __stdcall
  #define RADEXPLINK __stdcall

  #define RADDLLEXPORTDLL __declspec(dllexport)
  #ifdef __RADX32__
    // on weird NT DLLs built to run on Linux and Mac, no imports
    #define RADDLLIMPORTDLL
  #else
    // normal win32 dll import
    #define RADDLLIMPORTDLL __declspec(dllimport)
  #endif
#endif

#if defined(__RADWINRT__)
  #define __RADWIN__
  #if defined(__RADARM__) // no non-NEON ARMs in WinRT devices (so far)
    #define __RADNEON__
  #endif
  #define RADRESTRICT __restrict
  #define RADSTRUCT struct 

  #define RADLINK __stdcall
  #define RADEXPLINK __stdcall
  #define RADDLLEXPORTDLL __declspec(dllexport)
  #define RADDLLIMPORTDLL __declspec(dllimport)
#endif
      
#if defined(__RADWIIU__)
  #define RADRESTRICT __restrict
  #define RADSTRUCT struct __attribute__((__packed__))

  #define RADLINK
  #define RADEXPLINK
  #define RADDLLEXPORTDLL
  #define RADDLLIMPORTDLL
#endif

#if defined(__RADPSP2__)
  #define __RADNEON__
  #define RADRESTRICT __restrict
  #define RADSTRUCT struct __attribute__((__packed__))

  #define RADLINK
  #define RADEXPLINK
  #define RADDLLEXPORTDLL
  #define RADDLLIMPORTDLL
#endif

#if defined(__RADPS3__)
  #define __RAD64REGS__
  #define __RADCELL__
  #define RADRESTRICT __restrict
  #define RADSTRUCT struct __attribute__((__packed__))

  #define RADLINK
  #define RADEXPLINK
  #define RADDLLEXPORTDLL
  #define RADDLLIMPORTDLL
#endif

#if defined(__RADSPU__)
  #define __RADCELL__
  #define RADRESTRICT __restrict
  #define RADSTRUCT struct __attribute__((__packed__))

  #define RADLINK
  #define RADEXPLINK
  #define RADDLLEXPORTDLL
  #define RADDLLIMPORTDLL
#endif

#if defined(__RADIPHONE__)
  #if defined(__ARM_NEON) || defined(__AARCH64_SIMD__)
    #define __RADNEON__
  #endif
  #define __RADMACAPI__
  #define RADRESTRICT __restrict
  #define RADSTRUCT struct __attribute__((__packed__))

  #define RADLINK
  #define RADEXPLINK
  #define RADDLLEXPORTDLL
  #define RADDLLIMPORTDLL
#endif

#if defined(__RADMAC__)
  #define __RADMACH__
  #define __RADMACAPI__
  #define RADRESTRICT __restrict
  #define RADSTRUCT struct __attribute__((__packed__))

  #define RADLINK
  #define RADEXPLINK
  // for mac, we assume you are building with hidden visibility,
  //   so for RADEXPFUNC, we turn the vis back on...
  #define RADDLLEXPORTDLL __attribute__((visibility("default")))
  #define RADDLLIMPORTDLL

  #ifdef TARGET_API_MAC_CARBON
    #if TARGET_API_MAC_CARBON
      #ifndef __RADCARBON__
        #define __RADCARBON__
      #endif
    #endif
  #endif
#endif

#if defined(__RADXENON__)
  #define __RAD64REGS__
  #define __RADWIN__
  #define RADRESTRICT __restrict
  #define RADSTRUCT struct 

  #define RADLINK __stdcall
  #define RADEXPLINK __stdcall
  #define RADDLLEXPORTDLL // we don't use dlls on xbox
  #define RADDLLIMPORTDLL 
#endif

#ifndef RADLINK
  #error RADLINK was not defined.
#endif

#ifdef _MSC_VER
  #define RADINLINE __inline
#else
  #define RADINLINE inline
#endif

//===========================================================================
// RR_STRING_JOIN joins strings in the preprocessor and works with LINESTRING
#define RR_STRING_JOIN(arg1, arg2)              RR_STRING_JOIN_DELAY(arg1, arg2)
#define RR_STRING_JOIN_DELAY(arg1, arg2)        RR_STRING_JOIN_IMMEDIATE(arg1, arg2)
#define RR_STRING_JOIN_IMMEDIATE(arg1, arg2)    arg1 ## arg2

//===========================================================================
// RR_NUMBERNAME is a macro to make a name unique, so that you can use it to declare
//    variable names and they won't conflict with each other
// using __LINE__ is broken in MSVC with /ZI , but __COUNTER__ is an MSVC extension that works

#ifdef _MSC_VER
  #define RR_NUMBERNAME(name) RR_STRING_JOIN(name,__COUNTER__)
#else
  #define RR_NUMBERNAME(name) RR_STRING_JOIN(name,__LINE__)
#endif

//===================================================================
// simple compiler assert
// this happens at declaration time, so if it's inside a function in a C file, drop {} around it
#ifndef RR_COMPILER_ASSERT
  #define RR_COMPILER_ASSERT(exp)   typedef char RR_NUMBERNAME(_dummy_array) [ (exp) ? 1 : -1 ]
#endif


//===========================================
// first, we set defines for each of the types

#define RAD_S8 signed char
#define RAD_U8 unsigned char
#define RAD_U16 unsigned short
#define RAD_S16 signed short

#if defined(__RAD64__) 
  #define RAD_U32 unsigned int
  #define RAD_S32 signed int

  // pointers are 64 bits.
  #if ( defined(_MSC_VER) && _MSC_VER >= 1300 && defined(_Wp64) && _Wp64 )
    #define RAD_SINTa __w64 signed __int64
    #define RAD_UINTa __w64 unsigned __int64
  #else 
    // non-vc.net compiler or /Wp64 turned off
    #define RAD_UINTa unsigned long long
    #define RAD_SINTa signed long long
  #endif
#endif

#if defined(__RAD32__) && !defined(__RAD64__)
  #define RAD_U32 unsigned int
  #define RAD_S32 signed int

  #if ( ( defined(_MSC_VER) && (_MSC_VER >= 1300 ) ) && ( defined(_Wp64) && ( _Wp64 ) ) )
    #define RAD_SINTa __w64 signed long
    #define RAD_UINTa __w64 unsigned long
  #else 
    // non-vc.net compiler or /Wp64 turned off
    #ifdef _Wp64
      #define RAD_SINTa signed long
      #define RAD_UINTa unsigned long
    #else
      #define RAD_SINTa signed int
      #define RAD_UINTa unsigned int
    #endif
  #endif
#endif

#define RAD_F32 float
#define RAD_F64 double

#if defined(_MSC_VER)
  #define RAD_U64 unsigned __int64
  #define RAD_S64 signed __int64
#else
  #define RAD_U64 unsigned long long
  #define RAD_S64 signed long long
#endif


//================================================================
// Then, we either typedef or define them based on switch settings

#if !defined(RADNOTYPEDEFS)  // this define will turn off typedefs

  #ifndef S8_DEFINED
  #define S8_DEFINED
  typedef RAD_S8 S8;
  #endif

  #ifndef U8_DEFINED
  #define U8_DEFINED
  typedef RAD_U8 U8;
  #endif

  #ifndef S16_DEFINED
  #define S16_DEFINED
  typedef RAD_S16 S16;
  #endif

  #ifndef U16_DEFINED
  #define U16_DEFINED
  typedef RAD_U16 U16;
  #endif

  #ifndef S32_DEFINED
  #define S32_DEFINED
  typedef RAD_S32 S32;
  #endif

  #ifndef U32_DEFINED
  #define U32_DEFINED
  typedef RAD_U32 U32;
  #endif

  #ifndef S64_DEFINED
  #define S64_DEFINED
  typedef RAD_S64 S64;
  #endif

  #ifndef U64_DEFINED
  #define U64_DEFINED
  typedef RAD_U64 U64;
  #endif

  #ifndef F32_DEFINED
  #define F32_DEFINED
  typedef RAD_F32 F32;
  #endif

  #ifndef F64_DEFINED
  #define F64_DEFINED
  typedef RAD_F64 F64;
  #endif

  #ifndef SINTa_DEFINED
  #define SINTa_DEFINED
  typedef RAD_SINTa SINTa;
  #endif

  #ifndef UINTa_DEFINED
  #define UINTa_DEFINED
  typedef RAD_UINTa UINTa;
  #endif

  #ifndef RRBOOL_DEFINED
    #define RRBOOL_DEFINED
    typedef S32 rrbool;
    typedef S32 RRBOOL;
  #endif

#elif !defined(RADNOTYPEDEFINES)  // this define will turn off type defines

  #ifndef S8_DEFINED
  #define S8_DEFINED
  #define S8 RAD_S8
  #endif

  #ifndef U8_DEFINED
  #define U8_DEFINED
  #define U8 RAD_U8
  #endif

  #ifndef S16_DEFINED
  #define S16_DEFINED
  #define S16 RAD_S16
  #endif

  #ifndef U16_DEFINED
  #define U16_DEFINED
  #define U16 RAD_U16
  #endif

  #ifndef S32_DEFINED
  #define S32_DEFINED
  #define S32 RAD_S32
  #endif

  #ifndef U32_DEFINED
  #define U32_DEFINED
  #define U32 RAD_U32
  #endif

  #ifndef S64_DEFINED
  #define S64_DEFINED
  #define S64 RAD_S64
  #endif

  #ifndef U64_DEFINED
  #define U64_DEFINED
  #define U64 RAD_U64
  #endif

  #ifndef F32_DEFINED
  #define F32_DEFINED
  #define F32 RAD_F32
  #endif

  #ifndef F64_DEFINED
  #define F64_DEFINED
  #define F64 RAD_F64
  #endif

  #ifndef SINTa_DEFINED
  #define SINTa_DEFINED
  #define SINTa RAD_SINTa
  #endif

  #ifndef UINTa_DEFINED
  #define UINTa_DEFINED
  #define UINTa RAD_UINTa
  #endif

  #ifndef RRBOOL_DEFINED
    #define RRBOOL_DEFINED
    #define rrbool S32
    #define RRBOOL S32
  #endif

#endif

#endif // __RADRES__

#endif // __RADTYPESH__


// Oodle2 public header

// header version :
//  the DLL is incompatible when MAJOR is bumped
//  MINOR is for internal revs and bug fixes that don't affect API compatibility
#define OODLE2_VERSION_MAJOR            7
#define OODLE2_VERSION_MINOR            6

// OodleVersion string is 1 . MAJOR . MINOR
//  don't make it from macros cuz the doc tool has to parse the string literal

#define OodleVersion "2.7.6"    /*
*/

#ifndef OODLE2_PUBLIC_CORE_DEFINES
#define OODLE2_PUBLIC_CORE_DEFINES 1

#define OOFUNC1 RADEXPFUNC
#define OOFUNC2 RADEXPLINK
#define OOFUNCSTART
#define OODLE_CALLBACK  RADLINK

#ifndef NULL
#define NULL    (0)
#endif

// set up OOFUNC1 :

    #if defined(OODLE_BUILDING_LIB) || defined(OODLE_BUILDING_DLL)
        #error should not see OODLE_BUILDING set for users of oodle.h
    #endif
        
    #if defined(OODLE_IMPORT_LIB) && defined(OODLE_IMPORT_DLL)
        #error multiple OODLE_IMPORT defines
    #endif

    // set one if none set :
    #if !defined(OODLE_IMPORT_DLL) && !defined(OODLE_IMPORT_LIB)
        // on Windows : import DLL
        //  else : import LIB
        #if defined(__RADNT__) || defined(__RADWINRT__)
        #define OODLE_IMPORT_DLL
        #else
        #define OODLE_IMPORT_LIB
        #endif
    #endif
        
    #if defined(OODLE_IMPORT_LIB)
        #undef OOFUNC1
        #define OOFUNC1 RADDEFFUNC
    #elif defined(OODLE_IMPORT_DLL)
        #undef OOFUNC1
        #if defined(__RADNT__) || defined(__RADWINRT__)
        #define OOFUNC1 RADDEFFUNC __declspec(dllimport)
        #else
        #error should not see OODLE_IMPORT_DLL on non-NT
        #endif
    #endif

#endif // OODLE2_PUBLIC_CORE_DEFINES

typedef void (OOFUNC2 t_OodleFPVoidVoid)(void);
/* void-void callback func pointer
*/

//-----------------------------------------------------
// OodleLZ

#if 0
#define OODLE_ALLOW_DEPRECATED_COMPRESSORS /* If you need to encode with the deprecated compressors, define this before including oodle2.h

    You may still decode with them without defining this.
*/
#endif

// Default verbosity compressSelect of 0 will not even log when it sees corruption
typedef enum OodleLZ_Verbosity
{
    OodleLZ_Verbosity_None = 0,
    OodleLZ_Verbosity_Minimal = 1,
    OodleLZ_Verbosity_Some = 2,
    OodleLZ_Verbosity_Lots = 3,
    OodleLZ_Verbosity_Force32 = 0x40000000
} OodleLZ_Verbosity;
/* Verbosity of LZ functions
    LZ functions print information to $OodleXLog_Printf .  The output is
    determined by OodleLog settings.
*/

RR_COMPILER_ASSERT( sizeof(OodleLZ_Verbosity) == 4 );

typedef enum OodleLZ_Compressor
{
    OodleLZ_Compressor_Invalid = -1,
    OodleLZ_Compressor_None = 3,  // None = memcpy, pass through uncompressed bytes
    
    // NEW COMPRESSORS :
    OodleLZ_Compressor_Kraken = 8,    // Fast decodes, high compression, amazing! NOTE : LARGE QUANTUM
    OodleLZ_Compressor_Leviathan = 13,// Leviathan = Kraken's big brother with higher compression. NOTE : LARGE QUANTUM
    OodleLZ_Compressor_Mermaid = 9,   // Mermaid is between Kraken & Selkie - crazy fast, still decent compression. NOTE : LARGE QUANTUM
    OodleLZ_Compressor_Selkie = 11,   // Selkie is a super-fast relative of Mermaid.  Faster than LZB16/LZ4 but more compression.  NOTE : LARGE QUANTUM ; NOTE : Selkie will show up as Mermaid data in the decoder
    OodleLZ_Compressor_Hydra = 12,    // Hydra, the many-headed beast = Leviathan, Kraken, Mermaid, or Selkie (see $OodleLZ_About_Hydra)

#ifdef OODLE_ALLOW_DEPRECATED_COMPRESSORS   
    // DEPRECATED :
    OodleLZ_Compressor_BitKnit = 10, // DEPRECATED : BitKnit ; usually close to LZNA compression levels but faster.  Particularly great on some types of structured binary, such as structs of DWORD/float.:
    OodleLZ_Compressor_LZB16 = 4, // DEPRECATED : LZB16 = LZ-Bytewise ; 64k window ; fast, low compression.  Generally prefer Selkie unless you need the 64k window
    OodleLZ_Compressor_LZNA = 7,  // DEPRECATED : LZNA : the highest compression option in Oodle, comparable to LZMA (7zip) but much faster to decode
    OodleLZ_Compressor_LZH = 0,   // DEPRECATED : LZH , 128k sliding window ; generally use LZHLW instead
    OodleLZ_Compressor_LZHLW = 1, // DEPRECATED : LZH-LargeWindow ; fast to decode, good compression
    OodleLZ_Compressor_LZNIB = 2, // DEPRECATED : LZ-Nibbled ; fast to decompress + medium compression
    OodleLZ_Compressor_LZBLW = 5, // DO NOT USE : LZBLW ; use Selkie instead
    OodleLZ_Compressor_LZA = 6,   // DO NOT USE : LZA ; use LZNA instead
#endif
    
    OodleLZ_Compressor_Count = 14,
    OodleLZ_Compressor_Force32 = 0x40000000
} OodleLZ_Compressor;
/* Selection of compression algorithm.

    Each compressor provides a different balance of speed vs compression ratio.

    New Oodle users should only use the new sea monster family of compressors.

    Deprecated compressors are still in the SDK but will be removed some day.  Stop using them
    and transition to the new compressors.  See $Oodle_FAQ_deprecated_compressors

    The sea monsters are all fuzz safe and use whole-block quantum (not the 16k quantum)
    ($OodleLZ_Compressor_UsesWholeBlockQuantum)

    If you need to encode the deprecated compressors, define $OODLE_ALLOW_DEPRECATED_COMPRESSORS before
    including oodle2.h  (you can always decode them)

    See $Oodle_FAQ_WhichLZ for a quick FAQ on which compressor to use

    See $OodleLZ_About for discussion of how to choose a compressor.
*/

RR_COMPILER_ASSERT( sizeof(OodleLZ_Compressor) == 4 );
 
typedef enum OodleLZ_PackedRawOverlap
{
    OodleLZ_PackedRawOverlap_No = 0,
    OodleLZ_PackedRawOverlap_Yes = 1,
    OodleLZ_PackedRawOverlap_Force32 = 0x40000000
} OodleLZ_PackedRawOverlap;
/* Bool enum
*/

typedef enum OodleLZ_CheckCRC
{
    OodleLZ_CheckCRC_No = 0,
    OodleLZ_CheckCRC_Yes = 1,
    OodleLZ_CheckCRC_Force32 = 0x40000000
} OodleLZ_CheckCRC;
/* Bool enum for the LZ decoder - should it check CRC before decoding or not?

    NOTE : the CRC's in the LZH decompress checks are the CRC's of the *compressed* bytes.  This allows checking the CRc
    prior to decompression, so corrupted data cannot be fed to the compressor.
    
    To use OodleLZ_CheckCRC_Yes, the compressed data must have been made with $(OodleLZ_CompressOptions:sendQuantumCRCs) set to true.
    
    If you want a CRC of the raw bytes, there is one optionally stored in the $OodleLZ_SeekTable and can be confirmed with
    $OodleLZ_CheckSeekTableCRCs
*/


typedef enum OodleLZ_Profile
{
    OodleLZ_Profile_Main=0,         // Main profile (all current features allowed)
    OodleLZ_Profile_Reduced=1,      // Reduced profile (Kraken only, limited feature set)
    OodleLZ_Profile_Force32 = 0x40000000
} OodleLZ_Profile;
/* Decode profile to target */

RR_COMPILER_ASSERT( sizeof(OodleLZ_Profile) == 4 );

typedef enum OodleDecompressCallbackRet
{
    OodleDecompressCallbackRet_Continue=0, 
    OodleDecompressCallbackRet_Cancel=1, 
    OodleDecompressCallbackRet_Invalid=2, 
    OodleDecompressCallbackRet_Force32 = 0x40000000
} OodleDecompressCallbackRet;
/* Return value for $OodleDecompressCallback
    return OodleDecompressCallbackRet_Cancel to abort the in-progress decompression
*/

RADDEFFUNC typedef OodleDecompressCallbackRet (OODLE_CALLBACK OodleDecompressCallback)(void * userdata, const U8 * rawBuf,SINTa rawLen,const U8 * compBuf,SINTa compBufferSize , SINTa rawDone, SINTa compUsed);
/* User-provided callback for decompression

    $:userdata  the data you passed for _pcbData_
    $:rawBuf    the decompressed buffer
    $:rawLen    the total decompressed length
    $:compBuf   the compressed buffer
    $:compBufferSize  the total compressed length
    $:rawDone   number of bytes in rawBuf decompressed so far
    $:compUsed  number of bytes in compBuf consumed so far

    OodleDecompressCallback is called incrementally during decompression.
*/

struct _OodleLZHDecoder;
typedef struct _OodleLZHDecoder OodleLZHDecoder;

typedef enum OodleLZ_CompressionLevel
{
    OodleLZ_CompressionLevel_None=0,        // don't compress, just copy raw bytes
    OodleLZ_CompressionLevel_SuperFast=1,   // super fast mode, lower compression ratio
    OodleLZ_CompressionLevel_VeryFast=2,    // fastest LZ mode with still decent compression ratio
    OodleLZ_CompressionLevel_Fast=3,        // fast - good for daily use 
    OodleLZ_CompressionLevel_Normal=4,      // standard medium speed LZ mode
    
    // optimal levels are good for distribution - compress rarely and decompress often
    // they provide very high compression ratios but are slow to encode
    // Optimal2 is recommended to start with
    OodleLZ_CompressionLevel_Optimal1=5,    // optimal parse level 1 (faster optimal encoder)
    OodleLZ_CompressionLevel_Optimal2=6,    // optimal parse level 2
    OodleLZ_CompressionLevel_Optimal3=7,    // optimal parse level 3 (slower optimal encoder)
    OodleLZ_CompressionLevel_Optimal4=8,    // optimal parse level 4 (slowest optimal encoder)
    OodleLZ_CompressionLevel_Optimal5=9,    // optimal parse level 4 (slowest optimal encoder)
    OodleLZ_CompressionLevel_Count=10,

    // faster-than-SuperFast levels when you're encoder CPU time constrained or want
    // something closer to symmetric compression vs. decompression time
    // the HyperFast levels are currently only available in Kraken, Mermaid & Selkie
    OodleLZ_CompressionLevel_HyperFast1=-1, // faster than SuperFast, lower ratio
    OodleLZ_CompressionLevel_HyperFast2=-2, // faster than HyperFast1, lower ratio
    OodleLZ_CompressionLevel_HyperFast3=-3, // faster than HyperFast2, lower ratio
    OodleLZ_CompressionLevel_HyperFast4=-4, // faster than HyperFast3, lower ratio
    
    // aliases :
    OodleLZ_CompressionLevel_HyperFast=OodleLZ_CompressionLevel_HyperFast1,
    OodleLZ_CompressionLevel_Optimal = OodleLZ_CompressionLevel_Optimal2,
    OodleLZ_CompressionLevel_Max     = OodleLZ_CompressionLevel_Optimal5,   // maximum compression
    OodleLZ_CompressionLevel_Min     = OodleLZ_CompressionLevel_HyperFast4, // fastest compression

    OodleLZ_CompressionLevel_Force32 = 0x40000000,
    OodleLZ_CompressionLevel_Invalid = OodleLZ_CompressionLevel_Force32
} OodleLZ_CompressionLevel;
/* Selection of compression encoder complexity

    Higher numerical value of CompressionLevel = slower compression, but smaller compressed data.

    The compressed stream is always decodable with the same decompressors.
    CompressionLevel controls the amount of work the encoder does to find the best compressed bit stream.
    
    I recommend starting with OodleLZ_CompressionLevel_Normal, then try up or down if you want
    faster encoding or smaller output files.
    
    OodleLZ_CompressionLevel_Optimal4 produces the smallest compressed data, but is quite slow to encode.
    
    The CompressionLevel does not affect decode speed much.  Higher compression level does not mean
    slower to decode.  To trade off decode speed vs ratio, use _spaceSpeedTradeoffBytes_ in $OodleLZ_CompressOptions
    
*/

RR_COMPILER_ASSERT( sizeof(OodleLZ_CompressionLevel) == 4 );

#define OODLELZ_LOCALDICTIONARYSIZE_MAX     (1<<30) /* Maximum value of maxLocalDictionarySize in OodleLZ_CompressOptions
*/



typedef RADSTRUCT OodleLZ_CompressOptions
{
    OodleLZ_Verbosity   verbosity;      // verbosity
    S32                 minMatchLen;        // minimum match length ; cannot be used to reduce a compressor's default MML, but can be higher.  On some types of data, a large MML (6 or 8) is a space-speed win.
    rrbool              seekChunkReset;     // whether chunks should be independent, for seeking and parallelism
    S32                 seekChunkLen;       // length of independent seek chunks (if seekChunkReset) ; must be a power of 2 and >= $OODLELZ_BLOCK_LEN ; you can use $OodleLZ_MakeSeekChunkLen
    OodleLZ_Profile     profile;            // decoder profile to target
    S32                 dictionarySize;     // sets a maximum offset for matches, if lower than the maximum the format supports.  <= 0 means infinite (use whole buffer).  Often power of 2 but doesn't have to be.
    S32                 spaceSpeedTradeoffBytes;  // this is a number of bytes; I must gain at least this many bytes of compressed size to accept a speed-decreasing decision
    S32                 deprecated_maxHuffmansPerChunk;  // not used by the new compressors, deprecated
    rrbool              sendQuantumCRCs;    // should the encoder send a CRC of each compressed quantum, for integrity checks; this is necessary if you want to use OodleLZ_CheckCRC_Yes on decode
    S32                 maxLocalDictionarySize;     // size of local dictionary before needing a long range matcher.  This does not set a window size for the decoder; it's useful to limit memory use and time taken in the encoder.  maxLocalDictionarySize must be a power of 2.  Must be <= OODLELZ_LOCALDICTIONARYSIZE_MAX
    rrbool              makeLongRangeMatcher;   // should the encoder find matches beyond maxLocalDictionarySize using an LRM
    S32                 matchTableSizeLog2; // when variable, sets the size of the match finder structure (often a hash table) ; use 0 for the compressor's default
} OodleLZ_CompressOptions;
/* Options for the compressor

    Typically filled by calling $OodleLZ_CompressOptions_GetDefault , then individual options may be modified.

    To ensure you have set up the options correctly, call $OodleLZ_CompressOptions_Validate.
    
    _verbosity_ : enables more logging for diagnostic purposes
    
    _minMatchLen_ : rarely useful.  Default value of 0 means let the compressor decide.  On some types of data,
    bumping this up to 4,6, or 8 can improve decode speed with little effect on compression ratio.  Most of the
    Oodle compressors use a default MML of 4 at levels below 7, and MML 3 at levels >= 7.  If you want to keep MML 4
    at the higher levels, set _minMatchLen_ here to 4.

    _seekChunkReset_ must be true if you want the decode to be able to run "Wide", with pieces that can be
    decoded independently (not keeping previous pieces in memory for match references).
    
    _seekChunkLen_ : length of independent seek chunks (if seekChunkReset) ; must be a power of 2 and >= $OODLELZ_BLOCK_LEN ; you can use $OodleLZ_MakeSeekChunkLen
    
    _profile_ : tells the encoder to target alternate bitstream profile
    
    _dictionarySize_ : limits the encoder to partial buffer access for matches.  Can be useful for decoding incrementally
    without keeping the entire output buffer in memory. 
    
    _spaceSpeedTradeoffBytes_ is a way to trade off compression ratio for decode speed.  If you make it smaller,
    you get more compression ratio and slower decodes.  It's the number of bytes that a decision must save to
    be worth a slower decode.  Default is 256.  So that means the encoder must be able to save >= 256 bytes to
    accept something that will slow down decoding (like adding another Huffman table).  The typical range is
    64-1024.  
    
    Lower _spaceSpeedTradeoffBytes_ = more compression, slower decode
    Higher _spaceSpeedTradeoffBytes_ = less compression, faster decode
    
    _spaceSpeedTradeoffBytes_ is the primary parameter for controlling Hydra.  The default value of 256 will make
    Hydra decodes that are just a little bit faster than Kraken.  You get Kraken speeds around 200, and Mermaid
    speeds around 1200.
    
    At the extreme, a _spaceSpeedTradeoffBytes_ of zero means all you care about is compression ratio, not decode
    speed, you want the encoder to make the smallest possible output.  In practice you should always use
    _spaceSpeedTradeoffBytes_ of at least 1 so that nearly-equal size outputs favor decode speed.
    Generally _spaceSpeedTradeoffBytes_ below 16 provides diminishing gains in size with pointless decode speed loss.
    
    _spaceSpeedTradeoffBytes_ is on sort of powers of 2 scale, so you might want to experiment with 32,64,128,256,512
    
    _deprecated_maxHuffmansPerChunk_ : deprecated, do not use, will be removed soon (set to 0)
    
    _sendQuantumCRCs_ : send hashes of the compressed data to verify in the decoder; not recommended, if you need data
    verification, use your own system outside of Oodle.
    
    _maxLocalDictionarySize_ : only applies to optimal parsers at level >= Optimal2.  This limits the encoder memory use.
    Making it larger = more compression, higher memory use.  Matches within maxLocalDictionarySize are found exactly,
    outside the maxLocalDictionarySize window an approximate long range matcher is used.
    
    _makeLongRangeMatcher_ : whether an LRM should be used to find matches outside the _maxLocalDictionarySize_ window
    
    _matchTableSizeLog2_ : for non-optimal levels (level <= Normal), controls the hash table size.  Making this very
    small can sometimes boost encoder speed.  For the very fastest encoding, use the SuperFast level and change
    _matchTableSizeLog2_ to 12 or 13.
    
    _matchTableSizeLog2_ allows you to limit memory use of the non-Optimal encoder levels.  Memory use is roughly
    ( 1 MB + 4 << matchTableSizeLog2 )
    
*/


typedef enum OodleLZ_EncoderHeaders
{
    OodleLZ_EncoderHeaders_Default = 0,    // normal block and quantum headers
    OodleLZ_EncoderHeaders_OnlyFirstBlockHeader = 1, // only first block header, then just quantum headers
    //OodleLZ_EncoderHeaders_ForceBlockHeader = 2, // makes the encoded calls independently decodable
    //OodleLZ_EncoderHeaders_NoHeaders =3,  // no quantum or block header; cannot be decoded normally
    OodleLZ_EncoderHeaders_Force32 = 0x40000000 
} OodleLZ_EncoderHeaders;
/* Selection of headers to include in compressed data

    OodleLZ_EncoderHeaders is for use with the _WithContext class of incremental compressors.

    Use OodleLZ_EncoderHeaders_Default unless I tell you otherwise.
    
    OodleLZ_EncoderHeaders_OnlyFirstBlockHeader is for use in incremental small packet compression,
    as in internet transmission.  It is not used with single large buffer compression.
    
    Compressed data made with OodleLZ_EncoderHeaders_OnlyFirstBlockHeader cannot be decoded
    as a simple buffer with $OodleLZ_Decompress.  You must make a streaming decoder with
    $OodleLZDecoder_Create and pass OODLELZ_DECODER_STREAMING_ONLYFIRSTBLOCKHEADER.
*/

typedef enum OodleLZ_Decode_ThreadPhase
{
    OodleLZ_Decode_ThreadPhase1 = 1,
    OodleLZ_Decode_ThreadPhase2 = 2,
    OodleLZ_Decode_ThreadPhaseAll = 3,
    OodleLZ_Decode_Unthreaded = OodleLZ_Decode_ThreadPhaseAll
} OodleLZ_Decode_ThreadPhase;
/* ThreadPhase for threaded Oodle decode

    Check $OodleLZ_Compressor_CanDecodeThreadPhased
    (currently only used by Kraken)
    
    See $OodleLZ_About_ThreadPhasedDecode
    
*/

typedef enum OodleLZ_FuzzSafe
{
    OodleLZ_FuzzSafe_No = 0,
    OodleLZ_FuzzSafe_Yes = 1
} OodleLZ_FuzzSafe;
/* OodleLZ_FuzzSafe

    About fuzz safety:

    Fuzz Safe decodes will not crash on corrupt data.  They may or may not return failure, and produce garbage output.
    
    Fuzz safe decodes will not read out of bounds.  They won't put data on the stack or previously in memory
    into the output buffer.
    
    If you ask for a fuzz safe decode and the compressor doesn't satisfy OodleLZ_Compressor_CanDecodeFuzzSafe
    then it will return failure.
    
    The _fuzzSafe_ argument defaults to No because some of the old Oodle compressors are not fuzz safe, and thus
    would fail to decode if it was set to Yes.  If you are using the new compresors (Kraken,Mermaid,Selkie,etc.)
    then you should set this to Yes.
*/

#define OODLELZ_BLOCK_LEN   (1<<18) /* The number of raw bytes per "seek chunk"
    Seek chunks can be decompressed independently if $(OodleLZ_CompressOptions:seekChunkReset) is set.
*/

#define OODLELZ_BLOCK_MAXIMUM_EXPANSION (2)
#define OODLELZ_BLOCK_MAX_COMPLEN       (OODLELZ_BLOCK_LEN+OODLELZ_BLOCK_MAXIMUM_EXPANSION) /* Maximum expansion per $OODLELZ_BLOCK_LEN is 1 byte.
    Note that the compressed buffer must be allocated bigger than this (use $OodleLZ_GetCompressedBufferSizeNeeded)
*/

#define OODLELZ_QUANTUM_LEN     (1<<14) /* Minimum decompression quantum

    NOTE : some new compressors use WHOLE BLOCK quantum (OODLELZ_BLOCK_LEN)
    Check $OodleLZ_Compressor_UsesWholeBlockQuantum
*/

// 5 byte expansion per-quantum with CRC's
#define OODLELZ_QUANTUM_MAXIMUM_EXPANSION   (5)

#define OODLELZ_QUANTUM_MAX_COMPLEN     (OODLELZ_QUANTUM_LEN+OODLELZ_QUANTUM_MAXIMUM_EXPANSION)

#define OODLELZ_SEEKCHUNKLEN_MIN        OODLELZ_BLOCK_LEN
#define OODLELZ_SEEKCHUNKLEN_MAX        (1<<29) // half GB

typedef RADSTRUCT OodleLZ_DecodeSome_Out
{
    S32 decodedCount;   // number of uncompressed bytes decoded
    S32 compBufUsed;    // number of compressed bytes consumed
   

    S32 curQuantumRawLen;  // tells you the current quantum size. you must have at least this much room available in the output buffer to be able to decode anything.
    S32 curQuantumCompLen; // if you didn't pass in enough data, nothing will decode (decodedCount will be 0), and this will tell you how much is needed
} OodleLZ_DecodeSome_Out;
/* Output value of $OodleLZDecoder_DecodeSome
*/

//---------------------------------------------

//=======================================================

typedef RADSTRUCT OodleLZ_SeekTable
{
    OodleLZ_Compressor  compressor;             // which compressor was used
    rrbool              seekChunksIndependent;  // are the seek chunks independent, or must they be decompressed in sequence
    
    S64                 totalRawLen;    // total uncompressed data lenth
    S64                 totalCompLen;   // sum of seekChunkCompLens 
    
    S32                 numSeekChunks;  // derived from rawLen & seekChunkLen
    S32                 seekChunkLen;   // multiple of OODLELZ_BLOCK_LEN
    
    U32 *               seekChunkCompLens;  // array of compressed lengths of seek chunks
    U32 *               rawCRCs;            // crc of the raw bytes of the chunk (optional; NULL unless $OodleLZSeekTable_Flags_MakeRawCRCs was specified)
} OodleLZ_SeekTable;

typedef enum OodleLZSeekTable_Flags
{
    OodleLZSeekTable_Flags_None  = 0,       // default 
    OodleLZSeekTable_Flags_MakeRawCRCs = 1,  // make the _rawCRCs_ member of $OodleLZ_SeekTable
    OodleLZSeekTable_Flags_Force32 = 0x40000000 
} OodleLZSeekTable_Flags;

#if defined(__RAD64__)
#define OODLE_MALLOC_MINIMUM_ALIGNMENT  16
#else
#define OODLE_MALLOC_MINIMUM_ALIGNMENT  8
#endif

//=====================================================


typedef RADSTRUCT OodleConfigValues
{
    S32 m_OodleLZ_LW_LRM_step;          // LZHLW LRM : bytes between LRM entries
    S32 m_OodleLZ_LW_LRM_hashLength;    // LZHLW LRM : bytes hashed for each LRM entries
    S32 m_OodleLZ_LW_LRM_jumpbits;      // LZHLW LRM : bits of hash used for jump table

    S32 m_OodleLZ_Decoder_Max_Stack_Size;   // if OodleLZ_Decompress needs to allocator a Decoder object, and it's smaller than this size, it's put on the stack instead of the heap
    S32 m_OodleLZ_Small_Buffer_LZ_Fallback_Size; // for new LZ's that want large buffers (Kraken, etc.), if a compress is done on a buffer smaller than this, a simpler LZ is used (currently LZB16)
    S32 m_OodleLZ_BackwardsCompatible_MajorVersion; // if you need to encode streams that can be read with an older version of Oodle, set this to the Oodle2 MAJOR version number that you need compatibility with.  eg to be compatible with oodle 2.7.3 you would put 7 here

    U32 m_oodle_header_version; // = OODLE_HEADER_VERSION
    
} OodleConfigValues;
/* OodleConfigValues

    Struct of user-settable low level config values.  See $Oodle_SetConfigValues.

    May have different defaults per platform.
*/

OOFUNC1 void OOFUNC2 Oodle_GetConfigValues(OodleConfigValues * ptr);
/* Get $OodleConfigValues

    $:ptr   filled with OodleConfigValues
    
    Gets the current $OodleConfigValues.

    May be different per platform.
*/

OOFUNC1 void OOFUNC2 Oodle_SetConfigValues(const OodleConfigValues * ptr);
/* Set $OodleConfigValues

    $:ptr   your desired OodleConfigValues
    
    Sets the global $OodleConfigValues from your struct.

    You should call $Oodle_GetConfigValues to fill the struct, then change the values you
    want to change, then call $Oodle_SetConfigValues.

    This should generally be done before doing anything with Oodle (eg. even before OodleX_Init).
    Changing OodleConfigValues after Oodle has started has undefined effects.
*/

typedef enum Oodle_UsageWarnings
{
    Oodle_UsageWarnings_Enabled = 0,
    Oodle_UsageWarnings_Disabled = 1,
    Oodle_UsageWarnings_Force32 = 0x40000000
} Oodle_UsageWarnings;
/* Whether Oodle usage warnings are enable or disabled. */

OOFUNC1 void OOFUNC2 Oodle_SetUsageWarnings(Oodle_UsageWarnings state);
/* Enables or disables Oodle usage warnings.

    $:state    whether usage warnings should be enabled or disabled.

   Usage warnings are enabled by default and try to be low-noise, but in case you want to
   disable them, this is how.

   This should generally be done once at startup.  Setting this state while there are Oodle
   calls running on other threads has undefined results.
*/

// function pointers to mallocs needed :

RADDEFFUNC typedef void * (OODLE_CALLBACK t_fp_OodleCore_Plugin_MallocAligned)( SINTa bytes, S32 alignment);
/* Function pointer type for OodleMallocAligned

    $:bytes     number of bytes to allocate
    $:alignment required alignment of returned pointer
    $:return    pointer to memory allocated (must not be NULL)

    _alignment_ will always be a power of two
    
    _alignment_ will always be >= $OODLE_MALLOC_MINIMUM_ALIGNMENT
    
*/

RADDEFFUNC typedef void (OODLE_CALLBACK t_fp_OodleCore_Plugin_Free)( void * ptr );
/* Function pointer type for OodleFree

    $:return    pointer to memory to free

*/

OOFUNC1 void OOFUNC2 OodleCore_Plugins_SetAllocators(
    t_fp_OodleCore_Plugin_MallocAligned * fp_OodleMallocAligned,
    t_fp_OodleCore_Plugin_Free * fp_OodleFree);
/* Set the function pointers for allocation needed by Oodle2 Core

    If these are not set, the default implementation on most platforms uses the C stdlib.
    On Microsoft platforms the default implementation uses HeapAlloc.
    
    These must not be changed once they are set!  Set them once then don't change them.

    NOTE : if you are using Oodle Ext , do NOT call this.  OodleX will install an allocator for Oodle Core.  Do not mix your own allocator with the OodleX allocator.  See $OodleXAPI_Malloc.

*/


// the main func pointer for log :
RADDEFFUNC typedef void (OODLE_CALLBACK t_fp_OodleCore_Plugin_Printf)(int verboseLevel,const char * file,int line,const char * fmt,...);
/* Function pointer to Oodle Core printf

    $:verboseLevel  verbosity of the message; 0-2 ; lower = more important
    $:file          C file that sent the message
    $:line          C line that sent the message
    $:fmt           vararg printf format string
    
    The logging function installed here must parse varargs like printf.
    
    _verboseLevel_ may be used to omit verbose messages.
*/

OOFUNC1 t_fp_OodleCore_Plugin_Printf * OOFUNC2 OodleCore_Plugins_SetPrintf(t_fp_OodleCore_Plugin_Printf * fp_rrRawPrintf);
/* Install the callback used by Oodle Core for logging

    $:fp_rrRawPrintf    function pointer to your log function; may be NULL to disable all logging
    $:return            returns the previous function pointer
    
    Use this function to install your own printf for Oodle Core.
    
    The default implementation in debug builds, if you install nothing, uses the C stdio printf for logging.
    On Microsoft platforms, it uses OutputDebugString and not stdio.
    
    To disable all logging, call OodleCore_Plugins_SetPrintf(NULL)
    
    WARNING : this function is NOT thread safe!  It should be done only once and done in a place where the caller can guarantee thread safety.
    
    In the debug build of Oodle, you can install OodleCore_Plugin_Printf_Verbose to get more verbose logging
    
*/

RADDEFFUNC typedef rrbool (OODLE_CALLBACK t_fp_OodleCore_Plugin_DisplayAssertion)(const char * file,const int line,const char * function,const char * message);
/* Function pointer to Oodle Core assert callback

    $:file          C file that triggered the assert
    $:line          C line that triggered the assert
    $:function      C function that triggered the assert (may be NULL)
    $:message       assert message
    $:return        true to break execution at the assertion site, false to continue
    
    This callback is called by Oodle Core when it detects an assertion condition.
    
    This will only happen in debug builds.
    
        
*/

OOFUNC1 t_fp_OodleCore_Plugin_DisplayAssertion * OOFUNC2 OodleCore_Plugins_SetAssertion(t_fp_OodleCore_Plugin_DisplayAssertion * fp_rrDisplayAssertion);
/* Install the callback used by Oodle Core for asserts

    $:fp_rrDisplayAssertion function pointer to your assert display function
    $:return            returns the previous function pointer
    
    Use this function to install your own display for Oodle Core assertions.
    This will only happen in debug builds.
        
    The default implementation in debug builds, if you install nothing, uses the C stderr printf for logging,
    except on Microsoft platforms where it uses OutputDebugString.
    
    WARNING : this function is NOT thread safe!  It should be done only once and done in a place where the caller can guarantee thread safety.
    
*/

OOFUNC1 void * OOFUNC2 OodleCore_Plugin_MallocAligned_Default(SINTa size,S32 alignment);
OOFUNC1 void OOFUNC2 OodleCore_Plugin_Free_Default(void * ptr);
OOFUNC1 void OOFUNC2 OodleCore_Plugin_Printf_Default(int verboseLevel,const char * file,int line,const char * fmt,...);
OOFUNC1 void OOFUNC2 OodleCore_Plugin_Printf_Verbose(int verboseLevel,const char * file,int line,const char * fmt,...);
OOFUNC1 rrbool OOFUNC2 OodleCore_Plugin_DisplayAssertion_Default(const char * file,const int line,const char * function,const char * message);

//=============================================================

//----------------------------------------------
// OodleLZH

#define OODLELZ_FAILED      (0) /* Return value of OodleLZH_Decompress on failure 
*/

//=======================================================

OOFUNC1 SINTa OOFUNC2 OodleLZ_Compress(OodleLZ_Compressor compressor,
    const void * rawBuf,SINTa rawLen,void * compBuf,
    OodleLZ_CompressionLevel selection,
    const OodleLZ_CompressOptions * pOptions RADDEFAULT(NULL),
    const void * dictionaryBase RADDEFAULT(NULL),
    const void * lrm RADDEFAULT(NULL),
    void * scratchMem RADDEFAULT(NULL),
    SINTa scratchSize RADDEFAULT(0) );
/* Compress some data from memory to memory, synchronously, with OodleLZ

    $:compressor    which OodleLZ variant to use in compression
    $:rawBuf        raw data to compress
    $:rawLen        number of bytes in rawBuf to compress
    $:compBuf       pointer to write compressed data to ; should be at least $OodleLZ_GetCompressedBufferSizeNeeded
    $:compressSelect    OodleLZ_CompressionLevel controls how much CPU effort is put into maximizing compression
    $:pOptions          (optional) options; if NULL, $OodleLZ_CompressOptions_GetDefault is used
    $:dictionaryBase    (optional) if not NULL, provides preceding data to prime the dictionary; must be contiguous with rawBuf, the data between the pointers _dictionaryBase_ and _rawBuf_ is used as the preconditioning data.  The exact same precondition must be passed to encoder and decoder.
    $:lrm               (optional) long range matcher
    $:scratchMem        (optional) pointer to scratch memory
    $:scratchSize       (optional) size of scratch memory
    $:return    size of compressed data written, or $OODLELZ_FAILED for failure

    Performs synchronous memory to memory LZ compression.

    In tools, you should generally use $OodleXLZ_Compress_AsyncAndWait instead to get parallelism.  (in the Oodle2 Ext lib)

    You can compress a large buffer in several calls by setting _dictionaryBase_ to the start
    of the buffer, and then making _rawBuf_ and _rawLen_ select portions of that buffer.  As long
    as _rawLen_ is a multiple of $OODLELZ_BLOCK_LEN , the compressed chunks can simply be
    concatenated together.
    
    If _scratchMem_ is provided, it will be used for the compressor's scratch memory needs before OodleMalloc is
    called.  If the scratch is big enough, no malloc will be done.  If the scratch is not big enough, the compress
    will not fail, instead OodleMalloc will be used.  OodleMalloc should not return null.  There is currently no way
    to make compress fail cleanly due to using too much memory, it must either succeed or abort the process.
    
    See $OodleLZ_About for tips on setting the compression options.

    If _dictionaryBase_ is provided, the backup distance from _rawBuf_ must be a multiple of $OODLELZ_BLOCK_LEN
    
    If $(OodleLZ_CompressOptions:seekChunkReset) is enabled, and _dictionaryBase_ is not NULL or _rawBuf_ , then the
    seek chunk boundaries are relative to _dictionaryBase_, not to _rawBuf_.
    
*/

// Decompress returns raw (decompressed) len received
// Decompress returns 0 (OODLELZ_FAILED) if it detects corruption
OOFUNC1 SINTa OOFUNC2 OodleLZ_Decompress(const void * compBuf,SINTa compBufSize,void * rawBuf,SINTa rawLen,
                                            OodleLZ_FuzzSafe fuzzSafe,
                                            OodleLZ_CheckCRC checkCRC RADDEFAULT(OodleLZ_CheckCRC_No),
                                            OodleLZ_Verbosity verbosity RADDEFAULT(OodleLZ_Verbosity_None),
                                            void * decBufBase RADDEFAULT(NULL),
                                            SINTa decBufSize RADDEFAULT(0),
                                            OodleDecompressCallback * fpCallback RADDEFAULT(NULL),
                                            void * callbackUserData RADDEFAULT(NULL),
                                            void * decoderMemory RADDEFAULT(NULL),
                                            SINTa decoderMemorySize RADDEFAULT(0),
                                            OodleLZ_Decode_ThreadPhase threadPhase RADDEFAULT(OodleLZ_Decode_Unthreaded)
                                            );
/* Decompress a some data from memory to memory, synchronously.

    $:compBuf       pointer to compressed data
    $:compBufSize   number of compressed bytes available (must be greater or equal to the number consumed)
    $:rawBuf        pointer to output uncompressed data into
    $:rawLen        number of uncompressed bytes to output
    $:fuzzSafe      should the decode fail if it contains non-fuzz safe codecs?
    $:checkCRC      (optional) if data could be corrupted and you want to know about it, pass OodleLZ_CheckCRC_Yes
    $:verbosity     (optional) if not OodleLZ_Verbosity_None, logs some info
    $:decBufBase    (optional) if not NULL, provides preceding data to prime the dictionary; must be contiguous with rawBuf, the data between the pointers _dictionaryBase_ and _rawBuf_ is used as the preconditioning data.   The exact same precondition must be passed to encoder and decoder.  The decBufBase must be a reset point.
    $:decBufSize    (optional) size of circular buffer starting at decBufBase, if 0, _rawLen_ is assumed
    $:fpCallback    (optional) OodleDecompressCallback to call incrementally as decode proceeds
    $:callbackUserData (optional) passed as userData to fpCallback
    $:decoderMemory (optional) pre-allocated memory for the Decoder, of size _decoderMemorySize_
    $:decoderMemorySize (optional) size of the buffer at _decoderMemory_; must be at least $OodleLZDecoder_MemorySizeNeeded bytes to be used
    $:threadPhase   (optional) for threaded decode; see $OodleLZ_About_ThreadPhasedDecode (default OodleLZ_Decode_Unthreaded)
    $:return        the number of decompressed bytes output, $OODLELZ_FAILED (0) if none can be decompressed
    
    Decodes data encoded with any $OodleLZ_Compressor.
    
    Note : _rawLen_ must be the actual number of bytes to output, the same as the number that were encoded with the corresponding
    OodleLZ_Compress size.  You must store this somewhere in your own header and pass it in to this call.  _compBufSize_ does NOT
    need to be the exact number of compressed bytes, is the number of bytes available in the buffer, it must be greater or equal to
    the actual compressed length.
    
    OodleLZ_Decompress is guaranteed not to crash even if the data is corrupted when _fuzzSafe_ is set to OodleLZ_FuzzSafe_Yes.
    When _fuzzSafe_ is Yes, the target buffer (_rawBuf_ and _rawLen_) will never be overrun.  Note that corrupted day might not
    be detected.
    
    Note that the new compressors (Kraken,Mermaid,Selkie,BitKnit) are all fuzz safe and you can use OodleLZ_FuzzSafe_Yes
    with them and no padding of the decode target buffer.
    
    If checkCRC is OodleLZ_CheckCRC_Yes, then corrupt data will be detected and the decode aborted.
    If checkCRC is OodleLZ_CheckCRC_No, then corruption might result in invalid data, but no detection of any error (garbage in, garbage out).

    If corruption is possible, _fuzzSafe_ is No and _checkCRC_ is OodleLZ_CheckCRC_No, $OodleLZ_GetDecodeBufferSize must be used to allocate
    _rawBuf_ large enough to prevent overrun.
    
    $OodleLZ_GetDecodeBufferSize should always be used to ensure _rawBuf_ is large enough, even when corruption is not
    possible (when fuzzSafe is No).
    
    _compBuf_ and _rawBuf_ are allowed to overlap for "in place" decoding, but then _rawBuf_ must be allocated to
    the size given by $OodleLZ_GetInPlaceDecodeBufferSize , and the compressed data must be at the end of that buffer.

    An easy way to take the next step to parallel decoding is with $OodleXLZ_Decompress_MakeSeekTable_Wide_Async (in the Oodle2 Ext lib)

    NOTE : the return value is the *total* number of decompressed bytes output so far.  If rawBuf is > decBufBase, that means
    the initial inset of (rawBuf - decBufBase) is included!  (eg. you won't just get _rawLen_)
    
    About fuzz safety:

    Fuzz Safe decodes will not crash on corrupt data.  They may or may not return failure, and produce garbage output.
    
    Fuzz safe decodes will not read out of bounds.  They won't put data on the stack or previously in memory
    into the output buffer.
    
    Fuzz safe decodes will not output more than the uncompressed size. (eg. the output buffer does not need to
    be padded like OodleLZ_GetDecodeBufferSize)
    
    If you ask for a fuzz safe decode and the compressor doesn't satisfy OodleLZ_Compressor_CanDecodeFuzzSafe
    then it will return failure.
    
    The _fuzzSafe_ argument defaults to No because some of the old Oodle compressors are not fuzz safe, and thus
    would fail to decode if it was set to Yes.  If you are using the new compresors (Kraken,Mermaid,Selkie,etc.)
    then you should set this to Yes.
    
    If _decBufBase_ is provided, the backup distance from _rawBuf_ must be a multiple of $OODLELZ_BLOCK_LEN
    
*/


//-------------------------------------------
// Incremental Decoder functions :


//=======================================================
// streaming decoder :
//  decodes to & from a circular sliding window
//  works nicely with Oodle's CircularBuffer

struct _OodleLZDecoder;
typedef struct _OodleLZDecoder OodleLZDecoder;
/* Opaque type for OodleLZDecoder

    See $OodleLZDecoder_Create
*/


#define OODLELZ_DECODER_STREAMING_ONLYFIRSTBLOCKHEADER  (-1)    /* pass for 'rawLen' to OodleLZDecoder_Create to make a streaming decoder

    use with an encode that was done with OodleLZ_EncoderHeaders_OnlyFirstBlockHeader
*/

OOFUNC1 OodleLZDecoder * OOFUNC2 OodleLZDecoder_Create(OodleLZ_Compressor compressor,S64 rawLen,void * memory, SINTa memorySize);
/*  Create a OodleLZDecoder

    $:compressor the type of data you will decode; use $OodleLZ_Compressor_Invalid if unknown
    $:rawLen    total raw bytes of the decode, or $OODLELZ_DECODER_STREAMING_ONLYFIRSTBLOCKHEADER
    $:memory    (optional) provide memory for the OodleLZDecoder object (not the window)
    $:memorySize (optional) if memory is provided, this is its size in bytes
    $:return    the OodleLZDecoder
    
    If memory is provided, it must be of size $OodleLZDecoder_MemorySizeNeeded.  If it is NULL it will be
    allocated with the malloc specified by $OodleAPI_OodleCore_Plugins.
    
    Free with $OodleLZDecoder_Destroy.  You should Destroy even if you passed in the memory.
    
    Providing _compressor_ lets the OodleLZDecoder be the minimum size needed for that type of data.
    If you pass $OodleLZ_Compressor_Invalid, then any type of data may be decoded, and the Decoder is allocated
    large enought to handle any of them.
    
    If you are going to pass rawLen to OodleLZDecoder_Reset , then you can pass 0 to rawLen here.
    
    See $OodleLZDecoder_DecodeSome for more. 
*/

OOFUNC1 S32 OOFUNC2 OodleLZDecoder_MemorySizeNeeded(OodleLZ_Compressor compressor RADDEFAULT(OodleLZ_Compressor_Invalid), SINTa rawLen RADDEFAULT(-1));
/* If you want to provide the memory needed by $OodleLZDecoder_Create , this tells you how big it must be.
    
    $:compressor the type of data you will decode; use $OodleLZ_Compressor_Invalid if unknown
    $:rawLen    should almost always be -1, which supports any size of raw data decompression
    $:return    bytes to allocate or reserve
    
    NOTE : using $OodleLZ_Compressor_Invalid lets you decode any time of compressed data.
    It requests as much memory as the largest compressor. This may be a *lot* more than your data needs;
    try to use the correct compressor type.
    
    If _rawLen_ is -1 (default) then the Decoder object created can be used on any length of raw data
    decompression.  If _rawLen_ is specified here, then you can only use it to decode data shorter than
    the length you specified here.  This use case is very rare, contact support for details.
*/

OOFUNC1 S32 OOFUNC2 OodleLZ_ThreadPhased_BlockDecoderMemorySizeNeeded(void);
/* Returns the size of the decoder needed for ThreadPhased decode

    For use with $OodleLZ_Decode_ThreadPhase
    See $OodleLZ_About_ThreadPhasedDecode   
*/

OOFUNC1 void OOFUNC2 OodleLZDecoder_Destroy(OodleLZDecoder * decoder);
/* Pairs with $OodleLZDecoder_Create

    You should always call Destroy even if you provided the memory for $OodleLZDecoder_Create
*/

// Reset decoder - can reset to the start of any OODLELZ_BLOCK_LEN chunk
OOFUNC1 rrbool OOFUNC2 OodleLZDecoder_Reset(OodleLZDecoder * decoder, SINTa decPos, SINTa decLen RADDEFAULT(0));
/* Reset an OodleLZDecoder to restart at given pos

    $:decoder   the OodleLZHDecoder, made by $OodleLZDecoder_Create
    $:decPos    position to reset to; must be a multiple of OODLELZ_BLOCK_LEN
    $:decLen    (optional) if not zero, change the length of the data we expect to decode   
    $:return    true for success
    
    If you are seeking in a packed stream, you must seek to a seek chunk reset point, as was made at compress time.

    That is, $(OodleLZ_CompressOptions:seekChunkReset) must have been true, and
    _decPos_ must be a multiple of $(OodleLZ_CompressOptions:seekChunkLen) that was used at compress time.

    You can use $OodleLZ_GetChunkCompressor to verify that you are at a valid
    independent chunk start point.
    
*/

// returns false if corruption detected
OOFUNC1 rrbool OOFUNC2 OodleLZDecoder_DecodeSome(
                                        OodleLZDecoder * decoder,
                                        OodleLZ_DecodeSome_Out * out,

                                        // the decode sliding window : we output here & read from this for matches
                                        void * decBuf,
                                        SINTa decBufPos,
                                        SINTa decBufferSize,  // decBufferSize should be the result of OodleLZDecoder_MakeDecodeBufferSize()
                                        SINTa decBufAvail, // usually Size - Pos, but maybe less if you have pending IO flushes

                                        // compressed data :
                                        const void * compPtr,
                                        SINTa compAvail,
    
                                        OodleLZ_FuzzSafe fuzzSafe RADDEFAULT(OodleLZ_FuzzSafe_No),
                                        OodleLZ_CheckCRC checkCRC RADDEFAULT(OodleLZ_CheckCRC_No),
                                        OodleLZ_Verbosity verbosity RADDEFAULT(OodleLZ_Verbosity_None),
                                        OodleLZ_Decode_ThreadPhase threadPhase RADDEFAULT(OodleLZ_Decode_Unthreaded)

                                        );
/* Incremental decode some LZ compressed data

    $:decoder   the OodleLZHDecoder, made by $OodleLZDecoder_Create
    $:out       filled with results
    $:decBuf    the decode buffer (window)
    $:decBufPos the current position in the buffer
    $:decBufferSize size of decBuf ; this must be either equal to the total decompressed size (_rawLen_ passed to $OodleLZDecoder_Create) or the result of $OodleLZDecoder_MakeValidCircularWindowSize
    $:decBufAvail   the number of bytes available after decBufPos in decBuf ; usually (decBufferSize - decBufPos), but can be less
    $:compPtr   pointer to compressed data to read
    $:compAvail number of compressed bytes available at compPtr
    $:fuzzSafe      (optional) should the decode be fuzz safe
    $:checkCRC      (optional) if data could be corrupted and you want to know about it, pass OodleLZ_CheckCRC_Yes
    $:verbosity     (optional) if not OodleLZ_Verbosity_None, logs some info
    $:threadPhase   (optional) for threaded decode; see $OodleLZ_About_ThreadPhasedDecode (default OodleLZ_Decode_Unthreaded)
    $:return    true if success, false if invalid arguments or data is encountered
    
    Decodes data encoded with an OodleLZ compressor.
    
    Decodes an integer number of quanta; quanta are $OODLELZ_QUANTUM_LEN uncompressed bytes.
    
    _decBuf_ can either be a circular window or the whole _rawLen_ array.
    In either case, _decBufPos_ should be in the range [0,_decBufferSize_).
    If _decBuf_ is a circular window, then _decBufferSize_ should come from $OodleLZDecoder_MakeValidCircularWindowSize.
    
    NOTE : all the new LZ codecs (Kraken, etc.) do not do circular windows.  They can do sliding windows, see lz_test_11 in $example_lz.
    They should always have decBufferSize = total raw size, even if the decode buffer is smaller than that.
    
    NOTE : insuffient data provided (with _compAvail_ > 0 but not enough to decode a quantum) is a *success* case
    (return value of true), even though nothing is decoded.  A return of false always indicates a non-recoverable error.

    If _decBufAvail_ or _compAvail_ is insufficient for any decompression, the "curQuantum" fields of $OodleLZ_DecodeSome_Out
    will tell you how much you must provide to proceed.  That is, if enough compressed bytes are provided to get a quantum header, but not enough to decode a quantum, this
    function returns true and fills out the $OodleLZ_DecodeSome_Out structure with the size of the quantum.
    
    See $OodleLZ_Decompress about fuzz safety.
    
    NOTE : DecodeSome expect to decode either one full quantum (of len $OODLELZ_QUANTUM_LEN) or up to the length of the total buffer specified in the
call to $OodleLZDecoder_Create or $OodleLZDecoder_Reset.  That total buffer length
must match what was use during compression (or be a seek-chunk portion thereof).
That is, you cannot decompress partial streams in intervals smaller than 
$OODLELZ_QUANTUM_LEN except for the final partial quantum at the end of the stream.
    
*/

// pass in how much you want to alloc and it will tell you a valid size as close that as possible
//  the main use is just to call OodleLZHDecoder_MakeDecodeBufferSize(0) to get the min size; the min size is a good size
OOFUNC1 S32 OOFUNC2 OodleLZDecoder_MakeValidCircularWindowSize(OodleLZ_Compressor compressor,S32 minWindowSize RADDEFAULT(0));
/* Get a valid "Window" size for an LZ

    $:compressor    which compressor you will be decoding
    $:minWindowSize (optional) minimum size of the window

    Most common usage is OodleLZDecoder_MakeValidCircularWindowSize(0) to get the minimum window size.
    
    Only compressors which pass $OodleLZ_Compressor_CanDecodeInCircularWindow can be decoded in a circular window.
    
    WARNING : this is NOT the size to malloc the window! you need to call $OodleLZ_GetDecodeBufferSize() and
    pass in the window size to get the malloc size.
*/
                                                                        
//=======================================================

//=======================================================
// remember if you want to IO the SeekEntries you need to make them endian-independent
//  see WriteOOZHeader for example

#define OODLELZ_SEEKPOINTCOUNT_DEFAULT  16

OOFUNC1 S32 OOFUNC2 OodleLZ_MakeSeekChunkLen(S64 rawLen, S32 desiredSeekPointCount);                                  
/* Compute a valid seekChunkLen

    $:rawLen    total length of uncompressed data
    $:desiredSeekPointCount desired number of seek chunks
    $:return    a valid seekChunkLen for use in $OodleLZ_CreateSeekTable
    
    Returns a seekChunkLen which is close to (rawLen/desiredSeekPointCount) but is a power of two multiple of $OODLELZ_BLOCK_LEN

    _desiredSeekPointCount_ = 16 is good for parallel decompression.
    (OODLELZ_SEEKPOINTCOUNT_DEFAULT)
*/
    
OOFUNC1 S32 OOFUNC2 OodleLZ_GetNumSeekChunks(S64 rawLen, S32 seekChunkLen);
/* Compute the number of seek chunks

    $:rawLen        total length of uncompressed data
    $:seekChunkLen  the length of a seek chunk (eg from $OodleLZ_MakeSeekChunkLen)
    $:return        the number of seek chunks
    
    returns (rawLen+seekChunkLen-1)/seekChunkLen
*/

OOFUNC1 SINTa OOFUNC2 OodleLZ_GetSeekTableMemorySizeNeeded(S32 numSeekChunks,OodleLZSeekTable_Flags flags);
/* Tells you the size in bytes to allocate the seekTable before calling $OodleLZ_FillSeekTable

    $:numSeekChunks number of seek chunks (eg from $OodleLZ_GetNumSeekChunks)
    $:flags         options that will be passed to $OodleLZ_CreateSeekTable
    $:return        size in bytes of memory needed for seek table
    
    If you wish to provide the memory for the seek table yourself, you may call this to get the required size,
    allocate the memory, and then simply point a $OodleLZ_SeekTable at your memory.
    Then use $OodleLZ_FillSeekTable to fill it out.
    
    Do NOT use sizeof(OodleLZ_SeekTable) !
*/

OOFUNC1 rrbool OOFUNC2 OodleLZ_FillSeekTable(OodleLZ_SeekTable * pTable,OodleLZSeekTable_Flags flags,S32 seekChunkLen,const void * rawBuf, SINTa rawLen,const void * compBuf,SINTa compLen);
/* scan compressed LZ stream to fill the seek table

    $:pTable    pointer to table to be filled
    $:flags     options
    $:seekChunkLen  the length of a seek chunk (eg from $OodleLZ_MakeSeekChunkLen)
    $:rawBuf    (optional) uncompressed buffer; used to compute the _rawCRCs_ member of $OodleLZ_SeekTable
    $:rawLen    size of rawBuf
    $:compBuf   compressed buffer
    $:compLen   size of compBuf
    $:return    true for success

    _pTable_ must be able to hold at least $OodleLZ_GetSeekTableMemorySizeNeeded
    
    _seekChunkLen_ must be a multiple of $OODLELZ_BLOCK_LEN.
    _seekChunkLen_ must match what was in CompressOptions when the buffer was made, or any integer multiple thereof.
*/


OOFUNC1 OodleLZ_SeekTable * OOFUNC2 OodleLZ_CreateSeekTable(OodleLZSeekTable_Flags flags,S32 seekChunkLen,const void * rawBuf, SINTa rawLen,const void * compBuf,SINTa compLen);
/* allocate a table, then scan compressed LZ stream to fill the seek table

    $:flags     options
    $:seekChunkLen  the length of a seek chunk (eg from $OodleLZ_MakeSeekChunkLen)
    $:rawBuf    (optional) uncompressed buffer; used to compute the _rawCRCs_ member of $OodleLZ_SeekTable
    $:rawLen    size of rawBuf
    $:compBuf   compressed buffer
    $:compLen   size of compBuf
    $:return    pointer to table if succeeded, null if failed

    Same as $OodleLZ_FillSeekTable , but allocates the memory for you.  Use $OodleLZ_FreeSeekTable to free.

    _seekChunkLen_ must be a multiple of $OODLELZ_BLOCK_LEN.
    _seekChunkLen_ must match what was in CompressOptions when the buffer was made, or any integer multiple thereof.
     
*/

OOFUNC1 void OOFUNC2 OodleLZ_FreeSeekTable(OodleLZ_SeekTable * pTable);
/* Frees a table allocated by $OodleLZ_CreateSeekTable
*/
                                
OOFUNC1 rrbool OOFUNC2 OodleLZ_CheckSeekTableCRCs(const void * rawBuf,SINTa rawLen, const OodleLZ_SeekTable * seekTable);
/* Check the CRC's in seekTable vs rawBuf

    $:rawBuf    uncompressed buffer
    $:rawLen    size of rawBuf
    $:seekTable result of $OodleLZ_CreateSeekTable
    $:return    true if the CRC's check out
    
    Note that $OodleLZ_Decompress option of $OodleLZ_CheckCRC checks the CRC of *compressed* data, 
    this call checks the CRC of the *raw* (uncompressed) data.
    
    OodleLZ data contains a CRC of the compressed data if it was made with $(OodleLZ_CompressOptions:sendQuantumCRCs).
    The SeekTable contains a CRC of the raw data if it was made with $OodleLZSeekTable_Flags_MakeRawCRCs.
    
    Checking the CRC of compressed data is faster, but does not verify that the decompress succeeded.
*/

OOFUNC1 S32 OOFUNC2 OodleLZ_FindSeekEntry( S64 rawPos, const OodleLZ_SeekTable * seekTable);
/* Find the seek entry that contains a raw position

    $:rawPos            uncompressed position to look for
    $:seekTable         result of $OodleLZ_CreateSeekTable
    $:return            a seek entry index
    
    returns the index of the chunk that contains _rawPos_
*/

OOFUNC1 S64 OOFUNC2 OodleLZ_GetSeekEntryPackedPos( S32 seekI , const OodleLZ_SeekTable * seekTable );
/* Get the compressed position of a seek entry

    $:seekI         seek entry index , in [0,numSeekEntries)
    $:seekTable     result of $OodleLZ_CreateSeekTable
    $:return        compressed buffer position of the start of this seek entry
    
    
*/

//=============================================================
                                    
OOFUNC1 const char * OOFUNC2 OodleLZ_CompressionLevel_GetName(OodleLZ_CompressionLevel compressSelect);
/* Provides a string naming a $OodleLZ_CompressionLevel compressSelect
*/

OOFUNC1 const char * OOFUNC2 OodleLZ_Compressor_GetName(OodleLZ_Compressor compressor);
/* Provides a string naming a $OodleLZ_Compressor compressor
*/
                                        
                                        
// OodleLZ_CompressOptions_GetDefault - get options for compress LEvel
//  you don't need to use the options for the compressSelect you compress at, these are just suggestions / typical good values
//  eg. you could use the _Fast options and then compress with _Optimal compressor
//  NOTE : unlike choice of compressor, this DOES affect decode speed , in particular if lzLevel >= Optimal2
OOFUNC1 const OodleLZ_CompressOptions * OOFUNC2 OodleLZ_CompressOptions_GetDefault(
                                                    OodleLZ_Compressor compressor,
                                                    OodleLZ_CompressionLevel lzLevel RADDEFAULT(OodleLZ_CompressionLevel_Normal));
/* Provides a pointer to default compression options

    $:compressor    which compressor you are using
    $:lzLevel   $OodleLZ_CompressionLevel compressSelect, provides different default options for different levels

    _lzLevel_ does not need to match the compressSelect you pass to $OodleLZ_Compress , but it is intended to be a good fit if they do match.
    
    
*/

// after you fiddle with options, call this to ensure they are allowed
OOFUNC1 void OOFUNC2 OodleLZ_CompressOptions_Validate(OodleLZ_CompressOptions * pOptions);
/* Clamps the values in _pOptions_ to be in valid range

*/

// inline functions for compressor property queries
RADDEFSTART

rrbool OodleLZ_Compressor_UsesWholeBlockQuantum(OodleLZ_Compressor compressor);
/* OodleLZ_Compressor properties helper.

    Tells you if this compressor is "whole block quantum" ; must decode in steps of 
    $OODLELZ_BLOCK_LEN , not $OODLELZ_QUANTUM_LEN like others.
*/
rrbool OodleLZ_Compressor_UsesLargeWindow(OodleLZ_Compressor compressor);
/* OodleLZ_Compressor properties helper.

    Tells you if this compressor is "LargeWindow" or not, meaning it can benefit from
    a Long-Range-Matcher and windows larger than $OODLELZ_BLOCK_LEN
*/
rrbool OodleLZ_Compressor_CanDecodeInCircularWindow(OodleLZ_Compressor compressor);
/* OodleLZ_Compressor properties helper.

    Tells you if this compressor can be decoded using a fixed size circular window.
*/
rrbool OodleLZ_Compressor_CanEncodeWithContext(OodleLZ_Compressor compressor);
/* OodleLZ_Compressor properties helper.

    Tells you if this compressor can be used with the $OodleLZ_CompressContext functions.
*/
rrbool OodleLZ_Compressor_CanDecodeThreadPhased(OodleLZ_Compressor compressor);
/* OodleLZ_Compressor properties helper.

    Tells you if this compressor can be used with the $OodleLZ_Decode_ThreadPhase.
    
    See $OodleLZ_About_ThreadPhasedDecode
*/
rrbool OodleLZ_Compressor_CanDecodeInPlace(OodleLZ_Compressor compressor);
/* OodleLZ_Compressor properties helper.

    Tells you if this compressor can be used with "in-place" decoding.

    This is now always true (all compressors support in-place decoding).  The function is left
    for backward compatibility.
    
    All compressors in the future will support in-place, you don't need to check this property.

*/
rrbool OodleLZ_Compressor_MustDecodeWithoutResets(OodleLZ_Compressor compressor);
/* OodleLZ_Compressor properties helper.

    Tells you if this compressor must decode contiguous ranges of buffer with the same Decoder.
    
    That is, most of the compressors can be Reset and restart on any block, not just seek blocks,
    as long as the correct window data is provided.  That is, if this returns false then the only
    state required across a non-reset block is the dictionary of previously decoded data.
    
    But if OodleLZ_Compressor_MustDecodeWithoutResets returns true, then you cannot do that,
    because the Decoder object must carry state across blocks (except reset blocks).
    
    This does not apply to seek points - you can always reset and restart decompression at a seek point.
*/
rrbool OodleLZ_Compressor_CanDecodeFuzzSafe(OodleLZ_Compressor compressor);
/* OodleLZ_Compressor properties helper.

    Tells you if this compressor is "fuzz safe" which means it can accept corrupted data
    and won't crash or overrun any buffers.
*/

rrbool OodleLZ_Compressor_RespectsDictionarySize(OodleLZ_Compressor compressor);
/* OodleLZ_Compressor properties helper.

    Tells you if this compressor obeys $(OodleLZ_CompressOptions:dictionarySize) which limits
    match references to a finite bound.  (eg. for sliding window decompression).
    
    All the new codecs do (Kraken,Mermaid,Selkie,Leviathan).  Some old codecs don't.
*/
//=====================================================================

#define OODLELZ_COMPRESSOR_MASK(c)  (((U32)1)<<((S32)(c)))
// OODLELZ_COMPRESSOR_BOOLBIT : extract a value of 1 or 0 so it maps to "bool"
#define OODLELZ_COMPRESSOR_BOOLBIT(s,c) (((s)>>(S32)(c))&1)

static RADINLINE rrbool OodleLZ_Compressor_IsNewLZFamily(OodleLZ_Compressor compressor)
{
    const U32 set = 
        OODLELZ_COMPRESSOR_MASK(OodleLZ_Compressor_Kraken) |
        OODLELZ_COMPRESSOR_MASK(OodleLZ_Compressor_Leviathan) |
        OODLELZ_COMPRESSOR_MASK(OodleLZ_Compressor_Mermaid) |
        OODLELZ_COMPRESSOR_MASK(OodleLZ_Compressor_Selkie) |
        OODLELZ_COMPRESSOR_MASK(OodleLZ_Compressor_Hydra);
    return OODLELZ_COMPRESSOR_BOOLBIT(set,compressor);
}

RADINLINE rrbool OodleLZ_Compressor_CanDecodeFuzzSafe(OodleLZ_Compressor compressor)
{
    #ifdef OODLE_ALLOW_DEPRECATED_COMPRESSORS
    const U32 set = 
        OODLELZ_COMPRESSOR_MASK(OodleLZ_Compressor_Kraken) |
        OODLELZ_COMPRESSOR_MASK(OodleLZ_Compressor_Leviathan) |
        OODLELZ_COMPRESSOR_MASK(OodleLZ_Compressor_Mermaid) |
        OODLELZ_COMPRESSOR_MASK(OodleLZ_Compressor_Selkie) |
        OODLELZ_COMPRESSOR_MASK(OodleLZ_Compressor_Hydra) |
        OODLELZ_COMPRESSOR_MASK(OodleLZ_Compressor_BitKnit) |
        OODLELZ_COMPRESSOR_MASK(OodleLZ_Compressor_LZB16);
    return OODLELZ_COMPRESSOR_BOOLBIT(set,compressor);
    #else
    // all new compressors are fuzz safe
    return compressor != OodleLZ_Compressor_Invalid;
    #endif
}

RADINLINE rrbool OodleLZ_Compressor_RespectsDictionarySize(OodleLZ_Compressor compressor)
{
    #ifdef OODLE_ALLOW_DEPRECATED_COMPRESSORS
    const U32 set = 
        OODLELZ_COMPRESSOR_MASK(OodleLZ_Compressor_Kraken) |
        OODLELZ_COMPRESSOR_MASK(OodleLZ_Compressor_Leviathan) |
        OODLELZ_COMPRESSOR_MASK(OodleLZ_Compressor_Mermaid) |
        OODLELZ_COMPRESSOR_MASK(OodleLZ_Compressor_Selkie) |
        OODLELZ_COMPRESSOR_MASK(OodleLZ_Compressor_Hydra) |
        OODLELZ_COMPRESSOR_MASK(OodleLZ_Compressor_LZNA) |
        OODLELZ_COMPRESSOR_MASK(OodleLZ_Compressor_BitKnit);
    return OODLELZ_COMPRESSOR_BOOLBIT(set,compressor);
    #else
    // all new compressors respect dictionarySize
    return compressor != OodleLZ_Compressor_Invalid;
    #endif
}

RADINLINE rrbool OodleLZ_Compressor_UsesWholeBlockQuantum(OodleLZ_Compressor compressor)
{
    return OodleLZ_Compressor_IsNewLZFamily(compressor);
}

RADINLINE rrbool OodleLZ_Compressor_CanDecodeThreadPhased(OodleLZ_Compressor compressor)
{
    return OodleLZ_Compressor_IsNewLZFamily(compressor);
}

RADINLINE rrbool OodleLZ_Compressor_CanDecodeInPlace(OodleLZ_Compressor compressor)
{
    // all compressors can now decode in place :
    return compressor != OodleLZ_Compressor_Invalid;
}

RADINLINE rrbool OodleLZ_Compressor_CanDecodeInCircularWindow(OodleLZ_Compressor compressor)
{
    #ifdef OODLE_ALLOW_DEPRECATED_COMPRESSORS
    const U32 set =
        OODLELZ_COMPRESSOR_MASK(OodleLZ_Compressor_LZH) |
        OODLELZ_COMPRESSOR_MASK(OodleLZ_Compressor_LZB16);
// LZNIB & LZA can decode in Circular only when generated by WithContext functions
//  || compressor == OodleLZ_Compressor_LZNIB || compressor == OodleLZ_Compressor_LZA;
    #else
    const U32 set = 0;
    #endif
    
    return OODLELZ_COMPRESSOR_BOOLBIT(set,compressor);
}

RADINLINE rrbool OodleLZ_Compressor_UsesLargeWindow(OodleLZ_Compressor compressor)
{
    // all but LZH and LZB16 now are large window
    return ! OodleLZ_Compressor_CanDecodeInCircularWindow(compressor);
}

RADINLINE rrbool OodleLZ_Compressor_CanEncodeWithContext(OodleLZ_Compressor compressor)
{
    #ifdef OODLE_ALLOW_DEPRECATED_COMPRESSORS
    const U32 set =
        OODLELZ_COMPRESSOR_MASK(OodleLZ_Compressor_LZH) |
        OODLELZ_COMPRESSOR_MASK(OodleLZ_Compressor_LZNIB) |
        OODLELZ_COMPRESSOR_MASK(OodleLZ_Compressor_LZA) |
        OODLELZ_COMPRESSOR_MASK(OodleLZ_Compressor_LZNA) |
        OODLELZ_COMPRESSOR_MASK(OodleLZ_Compressor_LZB16);
    #else
    const U32 set = 0;
    #endif
    
    return OODLELZ_COMPRESSOR_BOOLBIT(set,compressor);
}

RADINLINE rrbool OodleLZ_Compressor_MustDecodeWithoutResets(OodleLZ_Compressor compressor)
{
    #ifdef OODLE_ALLOW_DEPRECATED_COMPRESSORS
    const U32 set =
        OODLELZ_COMPRESSOR_MASK(OodleLZ_Compressor_BitKnit) |
        OODLELZ_COMPRESSOR_MASK(OodleLZ_Compressor_LZA) |
        OODLELZ_COMPRESSOR_MASK(OodleLZ_Compressor_LZNA);
    #else
    const U32 set = 0;
    #endif
    
    return OODLELZ_COMPRESSOR_BOOLBIT(set,compressor);
}

RADDEFEND

//=======================================================


// get maximum expanded size for compBuf alloc :
//  (note this is actually larger than the maximum compressed stream, it includes trash padding)
OOFUNC1 SINTa OOFUNC2 OodleLZ_GetCompressedBufferSizeNeeded(SINTa rawSize);
/* Return the size you must malloc the compressed buffer

    $:rawSize   uncompressed size you will compress into this buffer

    The _compBuf_ passed to $OodleLZ_Compress must be allocated at least this big.

    note this is actually larger than the maximum size of a compressed stream, it includes overrun padding.

*/

// decBuf needs to be a little larger than rawLen,
//  this will tell you exactly how much :
OOFUNC1 SINTa OOFUNC2 OodleLZ_GetDecodeBufferSize(SINTa rawSize,rrbool corruptionPossible);
/* Get the size you must malloc the decode (raw) buffer

    $:rawSize   uncompressed (raw) size without padding
    $:corruptionPossible    true if it is possible for the decoder to get corrupted data
    $:return    size of buffer to malloc; slightly larger than rawSize
    
    If you use a FuzzSafe decoder, then the target decode buffer can just be the size of the decompressed data
    (_rawSize_) and no padidng is needed (don't call this function!)

    NOTE: the new compressors (Kraken,Mermaid,Selkie,BitKnit,Leviathan,Hydra) are all fuzz safe and you can use 
    OodleLZ_FuzzSafe_Yes with them and no padding of the decode target buffer.  That is, with the new compressors,
    you can just use _rawSize_ and don't need this function any more.
    
    This padding is necessary for the older compressors when FuzzSafe_No is used.
    
    If corruptionPossible is true, a slightly larger buffer size is returned.
    
    If corruptionPossible is false, then you must ensure that the decoder does not get corrupted data,
    either by passing $OodleLZ_CheckCRC_Yes , or by your own mechanism.

    Note about possible overrun in LZ decoding : as long as the compressed data is not corrupted,
    and you decode either the entire compressed buffer, or an integer number of "seek chunks" ($OODLELZ_BLOCK_LEN),
    then there will be no overrun.  So you can decode LZ data in place and it won't stomp any following bytes.
    If those conditions are not true (eg. decoding only part of a larger compressed stream, decoding
    around a circular window, decoding data that may be corrupted), then there may be some limited amount of
    overrun on decode, as returned by $OodleLZ_GetDecodeBufferSize.
    
    
*/

// OodleLZ_GetInPlaceDecodeBufferSize :
//  after compressing, ask how big the in-place buffer needs to be
OOFUNC1 SINTa OOFUNC2 OodleLZ_GetInPlaceDecodeBufferSize(SINTa compLen, SINTa rawLen);
/* Get the size of buffer needed for "in place" decode

    $:compLen   compressed data length
    $:rawLen    decompressed data length
    $:return    size of buffer needed for "in place" decode ; slighly larger than rawLen
    
    To do an "in place" decode, allocate a buffer of this size (or larger).  Read the compressed data into the end of
    the buffer, and decompress to the front of the buffer.  The size returned here guarantees that the writes to the
    front of the buffer don't conflict with the reads from the end.

    
    See $OodleLZ_Decompress for more.
*/

// GetCompressedStepForRawStep is at OODLELZ_QUANTUM_LEN granularity
//  returns how many packed bytes to step to get the desired raw count step
OOFUNC1 SINTa OOFUNC2 OodleLZ_GetCompressedStepForRawStep(
                                    const void * compPtr, SINTa compAvail, 
                                    SINTa startRawPos, SINTa rawSeekBytes,
                                    SINTa * pEndRawPos RADDEFAULT(NULL),
                                    rrbool * pIndependent RADDEFAULT(NULL) );
/* How many bytes to step a compressed pointer to advance a certain uncompressed amount

    $:compPtr   current compressed pointer
    $:compAvail compressed bytes available at compPtr
    $:startRawPos   initial raw pos (corresponding to compPtr)
    $:rawSeekBytes  the desired step in raw bytes, must be a multiple of $OODLELZ_QUANTUM_LEN or $OODLELZ_BLOCK_LEN
    $:pEndRawPos    (optional) filled with the end raw pos actually reached
    $:pIndependent  (optional) filled with a bool that is true if the current chunk is independent from previous
    $:return        the number of compressed bytes to step
    
    You should try to use GetCompressedStepForRawStep only at block granularity - both _startRawPos_ and
    _rawSeekBytes_ should be multiples of OODLELZ_BLOCK_LEN (except at the end of the stream).  As long as you
    do that, then *pEndRawPos will = startRawPos + rawSeekBytes.
    
    You can use it at quantum granularity (OODLELZ_QUANTUM_LEN), but there are some caveats.  You cannot step
    quanta inside uncompressed blocks, only in normal LZ blocks.  If you try to seek quanta inside an uncompressed
    block, you will get *pEndRawPos = the end of the block. 

    You can only resume seeking from *pEndRawPos .
    
    returns 0 for valid not-enough-data case
    returns -1 for error

    If _compAvail_ is not the whole compressed buffer, then the returned step may be less than the amount you requested.
    eg. if the compressed data in _compAvail_ does not contain enough data to make a step of _rawSeekBytes_ a smaller
    step will be taken.
    NOTE : *can* return comp step > comp avail !


*/

OOFUNC1 OodleLZ_Compressor OOFUNC2 OodleLZ_GetChunkCompressor(const void * compChunkPtr,
                                    rrbool * pIndependent RADDEFAULT(NULL) );
/* ask who compressed this chunk

    $:compChunkPtr  pointer to compressed data; must be the start of compressed buffer, or a step of $OODLELZ_BLOCK_LEN raw bytes
    $:pIndependent  (optional) filled with a bool for whether this chunk is independent of predecessors
    $:return        the $OodleLZ_Compressor used to encode this chunk

    note this is only for this chunk - later chunks may have different compressors
    if you compressed all chunks the same it's up to you to store that info in your header
*/

//=======================================================


typedef struct OodleLZ_CompressContext OodleLZ_CompressContext;
/* Opaque context object for $OodleLZ_CompressContext

    Free with $OodleLZ_CompressContext_Free
*/

#define OODLELZCONTEXT_BITS_USE_DEFAULT (-1) /* Pass for any of the LZ bits paramemters to use default

*/

#define OODLELZCONTEXT_NOT_SLIDING_WINDOW   (0) /* Pass for slidingWindowBits to encode without a sliding window

*/

#define OODLELZ_SLIDING_WINDOW_MIN_BITS     (16) /* Minimum value for slidingWindowBits passed to

    The smallest LZNIB sliding widow is to 2 to this power.
*/

OOFUNC1 OodleLZ_CompressContext * OOFUNC2 OodleLZ_CompressContext_Alloc(
                    OodleLZ_Compressor  compressor,
                    OodleLZ_CompressionLevel level_fast_or_veryfast,
                    S32 slidingWindowBits RADDEFAULT(OODLELZCONTEXT_BITS_USE_DEFAULT),
                    S32 hashTableBits RADDEFAULT(OODLELZCONTEXT_BITS_USE_DEFAULT),
                    const void * window RADDEFAULT(NULL));
/* Allocate a $OodleLZ_CompressContext

    $:compressor        which OodleLZ_Compressor; must be OodleLZ_Compressor_LZH, OodleLZ_Compressor_LZB, or OodleLZ_Compressor_LZNIB (see $OodleLZ_Compressor_CanEncodeWithContext)
    $:level_fast_or_veryfast    level of compression; must be OodleLZ_CompressionLevel_Fast or OodleLZ_CompressionLevel_VeryFast
    $:slidingWindowBits (optional) log2 of the LZ sliding window, typically 16-24 or $OODLELZCONTEXT_BITS_USE_DEFAULT; NOTE : must be sufficient for the format! eg. 17 for LZH, 16 for LZB, 17 or more for LZNib ; for non-circular window, pass OODLELZCONTEXT_NOT_SLIDING_WINDOW
    $:hashTableBits     (optional) log2 of the LZ hash table size used for compression, typically 17-20 or $OODLELZCONTEXT_BITS_USE_DEFAULT
    $:window    (optional) the sliding window memory, if NULL one will be allocated; if not null, must be at lease two-to-the-slidingWindowBits 
    $:return    context  allocated
    
    NOTE : for single buffer WithContext compression, or for incremental buffer compression, make sure you set _slidingWindowBits_ to OODLELZCONTEXT_NOT_SLIDING_WINDOW
    
    _hashTableBits_ can be used to tweak speed; generally smaller hash = faster encoding, but worse compression (no effect on decode speed).
    
    Free with $OodleLZ_CompressContext_Free
    
    AllocContext also does $OodleLZ_CompressContext_Reset.  You should not do an initial reset right after AllocContext, it has been done.
*/

OOFUNC1 void OOFUNC2 OodleLZ_CompressContext_Free(OodleLZ_CompressContext * context);
/* Free a $OodleLZ_CompressContext

    $:context   the OodleLZ_CompressContext to free

*/

OOFUNC1 void OOFUNC2 OodleLZ_CompressContext_Reset(OodleLZ_CompressContext * context, 
        S32 change_slidingWindowBits RADDEFAULT(OODLELZCONTEXT_BITS_USE_DEFAULT),
        const void * change_window RADDEFAULT(NULL));
/* Reset a $OodleLZ_CompressContext
    
    $:context   the OodleLZ_CompressContext to reset
    $:change_slidingWindowBits  (optional) specify size of change_window ; ignored if change_window is null
    $:change_window             (optional) if not null, context sliding window is reset to this memory location
    
    
    Reset the context so that future calls to $OodleLZ_CompressWithContext produce data that is independent of previous data.
    
    When using a $OodleLZ_CompressContext on a different stream, you must reset the context, either by calling this.

    When moving to the next chunk within a single array, you can make it independent of the previous by calling $OodleLZ_CompressContext_Reset.
    
    DO NOT call $OodleLZ_CompressContext_Reset and also pass _seekChunkReset_ in the $OodleLZ_CompressOptions ; if you do, it may get reset twice, which is benign but a waste of time.

    DO NOT call $OodleLZ_CompressContext_Reset after the initial allocation.

*/
 
OOFUNC1 SINTa OOFUNC2 OodleLZ_CompressWithContext (OodleLZ_CompressContext * context,
                                        const void * rawBuf,SINTa rawLen,void * compBuf,
                                        const OodleLZ_CompressOptions * pOptions RADDEFAULT(NULL),
                                        OodleLZ_EncoderHeaders headers RADDEFAULT(OodleLZ_EncoderHeaders_Default));
/* Compress some data, with provided context

    $:context   compression context to use
    $:rawBuf    raw data to compress
    $:rawLen    number of bytes in rawBuf to compress
    $:compBuf   pointer to write compressed data to ; should be at least $OodleLZ_GetCompressedBufferSizeNeeded
    $:pOptions  (optional) options; if NULL, $OodleLZ_CompressOptions_GetDefault is used
    $:headers   (optional) which headers to write; see $OodleLZ_EncoderHeaders
    $:return    size of compressed data written, or $OODLELZ_FAILED for failure
    
    Compress data using LZ and supplied context.
    
    To produce normal OodleLZ encoded data that can be decoded with $OodleLZ_Decompress , use OodleLZ_EncoderHeaders_Default.

    To produce data for streaming network transmission, use $OodleLZ_EncoderHeaders_OnlyFirstBlockHeader , and make sure $(OodleLZ_CompressOptions:seekChunkReset) is off.  Streaming data can only be decoded with $OodleLZDecoder_DecodeSome.
    
*/


//=======================================================

                                
//=======================================================

#define OODLE_HEADER_VERSION        ((46<<24)|(OODLE2_VERSION_MAJOR<<16)|(OODLE2_VERSION_MINOR<<8)|(U32)sizeof(OodleLZ_SeekTable))      /*  OODLE_HEADER_VERSION is used to ensure the Oodle header matches the lib.  Don't copy the value of this macro, it will change when
    the header is rev'ed.

    This is what you pass to $OodleX_Init or $Oodle_CheckVersion
*/

OOFUNC1 rrbool OOFUNC2 Oodle_CheckVersion(U32 oodle_header_version, U32 * pOodleLibVersion RADDEFAULT(NULL));
/* Check the Oodle lib version against the header you are compiling with

    $:oodle_header_version  pass $OODLE_HEADER_VERSION here
    $:pOodleLibVersion      (optional) filled with the Oodle lib version
    $:return                false if $OODLE_HEADER_VERSION is not compatible with this lib
    
    If you use the Oodle2 Ext lib,, $OodleX_Init does it for you.  But if you want to check that you have a
    compatible lib before trying to Init, then use this.
*/

OOFUNC1 void OOFUNC2 Oodle_LogHeader(void);
/* Log the Oodle version & copyright

    Uses the log set with $OodleCore_Plugins_SetPrintf
*/

#ifdef _MSC_VER
#pragma warning(pop)
#pragma pack(pop, Oodle)
#endif

#endif // __OODLE2_H_INCLUDED__
