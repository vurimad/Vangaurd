/**
* Copyright (c) 2014 CD Projekt Red. All Rights Reserved.
*/

#ifndef _CORE_INTERNAL_H_
#define _CORE_INTERNAL_H_

//Wtf? Durango pch settings must be messed up somewhere...
#ifdef _MSC_VER
#pragma warning( disable: 4651 )
#endif

// There's an inverse dependency from engine->core that's not necessarily clean to remove at this pont
// Used to get core linking, so we can work on getting the engine ported to Orbis
#if defined( __ORBIS__ ) || defined( _DURANGO )
#define TMP_NO_ENTITY_HANDLE_FOR_CORE_LINKAGE
#endif

#define _CRT_SECURE_NO_WARNINGS

// Turn off security checks in iterators and stl containers
#ifndef _SECURE_SCL
	#define _SECURE_SCL 0
#endif
#define _SCL_SECURE_NO_WARNINGS

//////////////////////////////////////////////////////////////////////////
// Red System Integration
#include "../../redSystem/include/redSystemPublic.h"
#include "../../redSystem/include/typetraits.h"
#include "../../redSystem/include/utility.h"
#include "../../redSystem/include/redThreadsThread.h"
#include "../../redSystem/include/redThreadsAtomic.h"
#include "../../redSystem/include/redThreadsRedSystem.h"

#include "../../redMemory/include/redMemoryPublic.h"

#include "../../redMath/include/redMathPublic.h"

#include "../../redCompression/include/redCompressionPublic.h"

#include "../../redCore/include/redCorePublic.h"

#include "../../redContainers/include/redContainersPublic.h"

#include "../../redCrypto/include/redCryptoPublic.h"

// As things improve, these warning suppressors should be disabled
RED_DISABLE_WARNING_CLANG( "-Wswitch" );					// Don't report about unhandled conditions in switch statements
RED_DISABLE_WARNING_CLANG( "-Wreorder" );					// Don't report initializer lists being in the wrong order
RED_DISABLE_WARNING_CLANG( "-Wunused-function" );			// Don't report unused functions
RED_DISABLE_WARNING_CLANG( "-Wunused-variable" );			// Don't report declared variables that are unused
RED_DISABLE_WARNING_CLANG( "-Woverloaded-virtual" );		// Don't report virtual function overloads (bad, but its used in a bunch of places)
RED_DISABLE_WARNING_CLANG( "-Wformat" );					// Don't report how "%hs" is undefined - people are using raw printfs or other format-string annotated functions

RED_DISABLE_WARNING_MSC( 4503 )	// 'function': decorated name length exceeded, name was truncated

// Prevent direct use of standard library functions by deprecating them here
// RED_DEPRECATED( rand ); // Unfortunately this is blocked by standard library headers (<algorithm>) that call rand() (even if we don't use those particular functions)
RED_DEPRECATED( srand );

// Prevent use of dynamic_cast
// Note: There is a plan to disable native C++ RTTI
#define dynamic_cast do_not_use_dynamic_cast

#ifndef RED_LOGGING_ENABLED
#	define NO_LOG
#endif

#ifndef RED_ASSERTS_ENABLED
#	define NO_ASSERTS
#endif

// Temporary compatibility macros
#define TXT2 MACRO_TXT
#define RESTRICT_RETURN	RED_RESTRICT_RETURN
#define RESTRICT RED_RESTRICT_LOCAL
#define STDCALL RED_STDCALL
#define SYS_DebugBreak RED_BREAKPOINT();

// Asserts
#define ASSERT RED_ASSERT
#define HALT RED_HALT
#define VERIFY RED_VERIFY

#define ASSUME RED_ASSUME

#include "settings.h"


#include "../../redConfig/include/redConfigPublic.h"
#include "../../redFileSystem/include/redFileSystemPublic.h"
#include "../../redReflection/include/redReflectionPublic.h"
#include "telemetryAllocator.h"

namespace red
{
	RED_MEMORY_POOL( PoolGalaxy, TelemetryAllocator, RED_CORE_API );
}

#endif 
