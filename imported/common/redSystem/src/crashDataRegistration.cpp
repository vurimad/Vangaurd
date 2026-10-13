/*
* Copyright (c) 2018 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"
#include "crashDataRegistration.h"
#include "errorHandlerImplCrashDumpData.h"
#include "unitTestMode.h"

namespace red
{
	namespace err
	{
		static void DefaultCrashHandlerRegisterFunc( void* thisPtr, const RegisterCrashDataParams& params)
		{
			RED_UNUSED(thisPtr);

	#ifdef RED_USE_ERRORHANDLER
			RegisterCrashDataImpl(params);
	#endif
		}

		static CrashDumpHandlerParam gCrashDumpHandler = { nullptr, &DefaultCrashHandlerRegisterFunc };

		void RegisterCrashData(const RegisterCrashDataParams& params)
		{
#ifdef RED_USE_ERRORHANDLER
			(gCrashDumpHandler.registerFunc)(gCrashDumpHandler.thisPtr, params);
#endif
		}

		void DumpCrashDataForDebug()
		{
			DumpCrashDataResult result;
			int dummyThis = 0;
			err::DumpCrashData( &dummyThis,
					[]( void* thisPtr, const char* formattedName, const char* formattedValue )
				{
				}
			, result );
		}

		// default one defined in .cpp so shouldn't worry about static order
		CrashDumpHandlerParam OverrideCrashHandlerRegisterFuncForUnitTests(void* thisPtr, CrashHandlerFunc* func)
		{
			RED_FATAL_ASSERT(red::UnitTestMode());
			RED_FATAL_ASSERT(func);
			CrashDumpHandlerParam oldHandler = gCrashDumpHandler;
			gCrashDumpHandler = CrashDumpHandlerParam{ thisPtr, func };
			return oldHandler;
		}
	}
}
