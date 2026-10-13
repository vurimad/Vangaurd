/*
* Copyright(c) 2018 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"
#include "resourceLoaderThrottler.h"
#include "resourceLoaderThrottlerCrashData.h"

namespace dd
{
	ThrottleReasonCrashData& GetCrashData(res::ThrottleReasonForDebug reason)
	{
		switch (reason)
		{
		case res::ThrottleReasonForDebug::Invalid:
		{
			static ThrottleReasonCrashData crashData{ "ResourceLoaderThrottler/Invalid" };
			return crashData;
		}

		case res::ThrottleReasonForDebug::BinaryLoader_Decompress:
		{
			static ThrottleReasonCrashData crashData{ "ResourceLoaderThrottler/BinaryLoader/Decompress" };
			return crashData;
		}

		case res::ThrottleReasonForDebug::BinaryLoader_LoadObjects:
		{
			static ThrottleReasonCrashData crashData{ "ResourceLoaderThrottler/BinaryLoader/LoadObjects" };
			return crashData;
		}

		case res::ThrottleReasonForDebug::BinaryLoader_PostLoad:
		{
			static ThrottleReasonCrashData crashData{ "ResourceLoaderThrottler/BinaryLoader/PostLoad" };
			return crashData;
		}
		
		case res::ThrottleReasonForDebug::DeferredDataBuffer_Decompress:
		{
			static ThrottleReasonCrashData crashData{ "ResourceLoaderThrottler/DeferredDataBuffer/Decompress" };
			return crashData;
		}
		
		case res::ThrottleReasonForDebug::TextLoader_Decompress:
		{
			static ThrottleReasonCrashData crashData{ "ResourceLoaderThrottler/TextLoader/Decompress" };
			return crashData;
		}

		default:
			break;
		}

		RED_FATAL("Unknown reason %u", (Uint32)reason);
		static ThrottleReasonCrashData nullCrashData{ "ResourceLoaderThrottler/Unknown" };
		return nullCrashData;
	}
}
