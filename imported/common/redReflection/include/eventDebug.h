#pragma once

#if defined(RED_CONFIGURATION_DEBUG) || defined(RED_CONFIGURATION_NOPTS) || defined(RED_CONFIGURATION_RELEASE)
	#define RED_ENABLE_EVENT_DEBUG
#endif

#ifdef RED_ENABLE_EVENT_DEBUG

#include "event.h"

namespace red
{
namespace eventDebug
{
	RED_REFLECTION_API void Pause();
	RED_REFLECTION_API void Unpause();
	RED_REFLECTION_API Bool IsPaused();

	RED_REFLECTION_API void StartCapturing();
	RED_REFLECTION_API void StopCapturing();
	RED_REFLECTION_API Bool IsCapturing();
	RED_REFLECTION_API Float GetCaptureTimeInSecs();

	RED_REFLECTION_API void EnableEventNameReporting();
	RED_REFLECTION_API void DisableEventNameReporting();
	RED_REFLECTION_API Bool IsEventNameReported();

	RED_REFLECTION_API void EnableListenerClassReporting();
	RED_REFLECTION_API void DisableListenerClassReporting();
	RED_REFLECTION_API Bool IsListenerClassReported();

	RED_REFLECTION_API void OnBeginFrame();

	RED_REFLECTION_API void OnBeginDispatchEvent( const THandle< red::Event >& event, const THandle< ISerializable >& listener );
	RED_REFLECTION_API void OnEndDispatchEvent( const THandle< red::Event >& event );

	RED_REFLECTION_API void ResetStatistics();

	struct EventInfo
	{
		const rtti::ClassType* eventClass = nullptr;
		const rtti::ClassType* listenerClass = nullptr;
		CName eventName;
		Uint32 eventCount = 0;
		Uint32 dispatchCount = 0;
		Float totalMs = 0.0f;
		Float worstMs = 0.0f;
	};
	struct FrameInfo
	{
		Uint32 totalEventCount = 0;
		Uint32 totalDispatchCount = 0;
		Float totalMs = 0.0f;
		red::DynArray< EventInfo > eventInfos{ red::PoolDebug() };
	};
	RED_REFLECTION_API const FrameInfo& GetLastFrameStatistics();
	RED_REFLECTION_API const FrameInfo& GetCapturedFramesStatistics();
	RED_REFLECTION_API const FrameInfo& GetWorstFrameStatistics();
}
}

#endif