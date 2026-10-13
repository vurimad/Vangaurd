/*
* Copyright (c) 2018 CD Projekt Red. All Rights Reserved.
*/

#pragma once

namespace job
{

enum class Priority : Uint8
{
	Latent,
	RenderPath,
	CriticalPath,
	Immediate,
	COUNT,
};

enum class Affinity : Uint8
{
	All,
	ConsoleCore7,
#ifdef USE_RESOURCE_THROTTLER_THREADS
	ResourceThrottler,
#endif
};

struct ScheduleParam
{
	ScheduleParam()
		: priority( Priority::CriticalPath )
		, affinity( Affinity::All )
		, ioPriority( io::EAsyncPriority::eAsyncPriority_INVALID )
	{}

	ScheduleParam( Priority inPriority, Affinity inAffinity = Affinity::All, io::EAsyncPriority inIoPriority = io::EAsyncPriority::eAsyncPriority_INVALID )
		: priority( inPriority )
		, affinity( inAffinity )
		, ioPriority( inIoPriority )
	{}

	Priority priority;
	Affinity affinity;
	io::EAsyncPriority ioPriority;
};

}
