#include "build.h"
#include "eventDebug.h"

#ifdef RED_ENABLE_EVENT_DEBUG

#include "event.h"
#include "engineTime.h"

namespace red
{
namespace eventDebug
{

class EventDebug
{
public:
	EventDebug()
	{
		ResetFrame( m_currentFrame );
		ResetFrame( m_lastFrame );
		ResetFrame( m_capturedFrames );
		ResetFrame( m_worstFrame );
	}

	void Pause()
	{
		m_isPaused = true;
	}

	void Unpause()
	{
		m_isPaused = false;
	}

	Bool IsPaused() const
	{
		return m_isPaused;
	}

	void StartCapturing()
	{
		m_isCapturing = true;
		m_startCaptureTime = EngineTime::GetNow();
		ResetFrame( m_capturedFrames );
	}

	void StopCapturing()
	{
		m_isCapturing = false;
		m_captureTimeInSecs = ( Float ) ( EngineTime::GetNow() - m_startCaptureTime );
	}

	Bool IsCapturing() const
	{
		return m_isCapturing;
	}

	Float GetCaptureTimeInSecs() const
	{
		return m_captureTimeInSecs;
	}

	void EnableEventNameReporting()
	{
		m_reportEventName = true;
	}
	
	void DisableEventNameReporting()
	{
		m_reportEventName = false;
	}
	
	Bool IsEventNameReported()
	{
		return m_reportEventName;
	}

	void EnableListenerClassReporting()
	{
		m_reportListenerClass = true;
	}

	void DisableListenerClassReporting()
	{
		m_reportListenerClass = false;
	}
	
	Bool IsListenerClassReported()
	{
		return m_reportListenerClass;
	}

	void OnBeginFrame()
	{
		if( m_isPaused )
		{
			return;
		}

		// Store last frame
		{
			RED_SCOPE_LOCK( m_currentFrameLock );
			m_lastFrame = m_currentFrame;
			ResetFrame( m_currentFrame );
			m_currentFrameEvents.Clear();
		}
		
		// Post-process last frame 

		m_lastFrame.totalEventCount = 0;
		m_lastFrame.totalDispatchCount = 0;
		m_lastFrame.totalMs = 0.0f;
		for( EventInfo& info : m_lastFrame.eventInfos )
		{
			m_lastFrame.totalEventCount += info.eventCount;
			m_lastFrame.totalDispatchCount += info.dispatchCount;
			m_lastFrame.totalMs += info.totalMs;
		}
		SortFrameEventInfos( m_lastFrame );

		// Detect worst frame

		if( m_lastFrame.totalMs > m_worstFrame.totalMs )
		{
			m_worstFrame = m_lastFrame;
		}

		// Accumulate active capture

		if( m_isCapturing )
		{
			m_captureTimeInSecs = ( Float ) ( EngineTime::GetNow() - m_startCaptureTime );

			for( const EventInfo& srcEventInfo : m_lastFrame.eventInfos )
			{
				EventInfo& dstEventInfo = GetEventInfo( m_capturedFrames, srcEventInfo.eventClass, srcEventInfo.eventName, srcEventInfo.listenerClass );
				dstEventInfo.eventCount += srcEventInfo.eventCount;
				dstEventInfo.dispatchCount += srcEventInfo.dispatchCount;
				dstEventInfo.totalMs += srcEventInfo.totalMs;
				dstEventInfo.worstMs = Max( dstEventInfo.worstMs, srcEventInfo.worstMs );
			}

			m_capturedFrames.totalEventCount += m_lastFrame.totalEventCount;
			m_capturedFrames.totalDispatchCount += m_lastFrame.totalDispatchCount;
			m_capturedFrames.totalMs += m_lastFrame.totalMs;
			SortFrameEventInfos( m_capturedFrames );
		}
	}

	void OnBeginDispatchEvent( const THandle< red::Event >&, const THandle< ISerializable >& listener )
	{
		if( m_isPaused )
		{
			return;
		}

		m_threadData.startServiceEventTime = EngineTime::GetNow();
		m_threadData.listenerClass = listener->GetClass();
	}

	void OnEndDispatchEvent( const THandle< red::Event >& event )
	{
		if( m_isPaused )
		{
			return;
		}

		const EngineTime serviceEventTime = EngineTime::GetNow() - m_threadData.startServiceEventTime;
		const Float serviceEventMs = ( Float ) serviceEventTime * 1000.0f;
		ReportDispatchEvent( event, m_threadData.listenerClass, serviceEventMs );
	}

	void ResetStatistics()
	{
		ResetFrame( m_worstFrame );
	}

	const FrameInfo& GetLastFrameStatistics() const
	{
		return m_lastFrame;
	}
	
	const FrameInfo& GetWorstFrameStatistics() const
	{
		return m_worstFrame;
	}

	const FrameInfo& GetCapturedFramesStatistics() const
	{
		return m_capturedFrames;
	}

private:
	void ResetFrame( FrameInfo& frame )
	{
		frame.totalEventCount = 0;
		frame.totalDispatchCount = 0;
		frame.totalMs = 0.0f;
		frame.eventInfos.Clear();
	}

	void ReportDispatchEvent( const THandle< red::Event >& event, const rtti::ClassType* listenerClass, const Float serviceEventMs )
	{
		const rtti::ClassType* eventClass = event->GetClass();
		CName eventName;
		if( m_reportEventName )
		{
			static const CName nameName = RED_NAME( "name" );
			if( const rtti::Property* nameProperty = eventClass->FindProperty( nameName ) )
			{
				if( nameProperty->GetType() == GetTypeObject< CName >() )
				{
					nameProperty->Get( event.Get(), &eventName );
				}
			}
		}

		// NOTE: The logic of gathering data is very simple but not efficient
		//		 If needed, refactor into lockless per thread data gathering

		RED_SCOPE_LOCK( m_currentFrameLock );

		const Bool hasBeenReportedThisFrame = !m_currentFrameEvents.Insert( event->GetID() ).IsSuccessful();
		
		EventInfo& info = GetEventInfo( m_currentFrame, eventClass, eventName, m_reportListenerClass ? listenerClass : nullptr );
		if( !hasBeenReportedThisFrame )
		{
			++info.eventCount;
		}
		++info.dispatchCount;
		info.totalMs += serviceEventMs;
		info.worstMs = Max( info.worstMs, serviceEventMs );
	}

	EventInfo& GetEventInfo( FrameInfo& frame, const rtti::ClassType* eventClass, const CName eventName, const rtti::ClassType* listenerClass )
	{
		for( EventInfo& currentInfo : frame.eventInfos )
		{
			if( currentInfo.eventClass == eventClass && currentInfo.eventName == eventName && currentInfo.listenerClass == listenerClass )
			{
				return currentInfo;
			}
		}
		frame.eventInfos.EmplaceBack();
		frame.eventInfos.Back().eventClass = eventClass;
		frame.eventInfos.Back().eventName = eventName;
		frame.eventInfos.Back().listenerClass = listenerClass;
		return frame.eventInfos.Back();
	}

	void SortFrameEventInfos( FrameInfo& frame )
	{
		std::sort(
			frame.eventInfos.Begin(),
			frame.eventInfos.End(),
			[ this ]( const EventInfo& a, const EventInfo& b )
			{
				if( m_sortEventInfosByMs )
				{
					return a.totalMs > b.totalMs;
				}
				const Int32 nameCmp = !red::Strcmp( a.eventClass->GetName().AsChar(), b.eventClass->GetName().AsChar() );
				if( nameCmp )
				{
					return nameCmp < 0;
				}
				return red::Strcmp( a.eventName.AsChar(), b.eventName.AsChar() ) < 0;
			} );
	}

	Bool m_isPaused = true;
	Bool m_isCapturing = false;
	Bool m_reportEventName = true;
	Bool m_reportListenerClass = true;

	Bool m_sortEventInfosByMs = true;

	struct ThreadData
	{
		EngineTime startServiceEventTime;
		const rtti::ClassType* listenerClass;
	};
	static thread_local ThreadData m_threadData;

	red::RWSpinLock m_currentFrameLock;
	FrameInfo m_currentFrame;
	red::HashSet< SerializableID > m_currentFrameEvents{ red::PoolDebug() };

	FrameInfo m_lastFrame;

	EngineTime m_startCaptureTime;
	Float m_captureTimeInSecs;
	FrameInfo m_capturedFrames;

	FrameInfo m_worstFrame;
};

thread_local EventDebug::ThreadData EventDebug::m_threadData;
static EventDebug GEventDebug;

// Public API

void OnBeginFrame()
{
	GEventDebug.OnBeginFrame();
}

void Pause()
{
	GEventDebug.Pause();
}

void Unpause()
{
	GEventDebug.Unpause();
}

Bool IsPaused()
{
	return GEventDebug.IsPaused();
}

void StartCapturing()
{
	GEventDebug.StartCapturing();
}

void StopCapturing()
{
	GEventDebug.StopCapturing();
}

Bool IsCapturing()
{
	return GEventDebug.IsCapturing();
}

Float GetCaptureTimeInSecs()
{
	return GEventDebug.GetCaptureTimeInSecs();
}

void EnableEventNameReporting()
{
	GEventDebug.EnableEventNameReporting();
}

void DisableEventNameReporting()
{
	GEventDebug.DisableEventNameReporting();
}

Bool IsEventNameReported()
{
	return GEventDebug.IsEventNameReported();
}

void EnableListenerClassReporting()
{
	GEventDebug.EnableListenerClassReporting();
}

void DisableListenerClassReporting()
{
	GEventDebug.DisableListenerClassReporting();
}

Bool IsListenerClassReported()
{
	return GEventDebug.IsListenerClassReported();
}

void OnBeginDispatchEvent( const THandle< red::Event >& event, const THandle< ISerializable >& listener )
{
	GEventDebug.OnBeginDispatchEvent( event, listener );
}

void OnEndDispatchEvent( const THandle< red::Event >& event )
{
	GEventDebug.OnEndDispatchEvent( event );
}
	
void ResetStatistics()
{
	GEventDebug.ResetStatistics();
}

const FrameInfo& GetLastFrameStatistics()
{
	return GEventDebug.GetLastFrameStatistics();
}

const FrameInfo& GetCapturedFramesStatistics()
{
	return GEventDebug.GetCapturedFramesStatistics();
}

const FrameInfo& GetWorstFrameStatistics()
{
	return GEventDebug.GetWorstFrameStatistics();
}

} // eventDebug
} // red

#endif