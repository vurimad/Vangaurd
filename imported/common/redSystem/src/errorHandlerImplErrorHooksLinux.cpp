/*
* Copyright (c) 2018 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"

#if defined( RED_PLATFORM_LINUX )

#include <signal.h>

#include "utility.h"
#include "dbgUtils.h"
#include "errorHandler.h"
#include "errorHandlerImplAttachments.h"
#include "errorHandlerImplMessages.h"
#include "errorHandlerImplCrashDumpData.h"
#include "errorHandlerImplErrorHooksLinux.h"

namespace red { namespace err
{
	static atomic::TAtomic32 gRegisteredOnce = 0;
	static atomic::TAtomic32 gHandledErrorOnce = 0;

	static ErrorHandlerHooks::ScriptCallstackVisitorFunc* gScriptCallstackVisitor = nullptr;

	namespace helper
	{
		struct HandleErrorParams
		{
			const ErrorMessage* customErrMsg = nullptr;
		};

		static thread_local Bool tErrorParamValid = false;
		static thread_local red::EErrorReason tErrorReason;
		static thread_local HandleErrorParams tErrorParams;

		static void HandleErrorLinux( EErrorReason errorReason, const HandleErrorParams& params )
		{
			if ( atomic::Exchange32( &gHandledErrorOnce, 1 ) != 0 )
			{
				for ( ;; )
				{
					// Spin forever. We're about to die anyway, but don't spam from multiple threads
					red::SleepOnCurrentThread( 1000 );
				}
			}
		}

		static void AssertHandler( const ErrorMessage& errMsg )
		{
			tErrorParams = HandleErrorParams{};
			tErrorParams.customErrMsg = &errMsg;
			tErrorReason = eErrorReason_Assert;
			tErrorParamValid = true;
		}

		static void RegisterAttachment( const char* pathToRegister )
		{
			// TODO
		}

		static void FastFailAbortProcess()
		{
			// TODO
			RED_DEBUG_BREAK();
		}
	} // helper

	ErrorHandlerHooks RegisterErrorHooksOnceLinux( Uint32 errFlags, const char* appVersion, ErrorHandlerHooks::ScriptCallstackVisitorFunc* scriptCallstackVisitor )
	{
		ErrorHandlerHooks hooks;
		hooks.fnAssertHandler = &helper::AssertHandler;
		hooks.fnRegisterAttachment = &helper::RegisterAttachment;
		hooks.fnFailFastAbortProcess = &helper::FastFailAbortProcess;

		if ( !gScriptCallstackVisitor )
		{
			gScriptCallstackVisitor = scriptCallstackVisitor;
		}

		hooks.fnScriptCallstackVisitor = gScriptCallstackVisitor;

		// TODO

		return hooks;
	}
} } //red/err
#else
RED_NO_EMPTY_FILE();
#endif
