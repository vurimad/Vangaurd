/*
* Copyright (c) 2018 CD Projekt Red. All Rights Reserved.
*/

#pragma once

namespace res
{
	enum class ThrottleReasonForDebug : Uint32;
}

namespace dd
{
	struct ThrottleReasonCrashData
	{
		explicit ThrottleReasonCrashData(const char* group)
			: numWaiting( group, "NumWaiting" )
			, numActive( group, "NumActive" )
		{}

		red::CrashData< Uint32 > numWaiting;
		red::CrashData< Uint32 > numActive;
	};

	ThrottleReasonCrashData& GetCrashData(res::ThrottleReasonForDebug reason);
}