/*
* Copyright © 2017 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"
#include "resourceLoadingFence.h"

#include "../../redJobs2/include/jobRunner.h"
#include "../../redJobs2/include/jobDeferral.h"
#include "../../redCore/include/instrumentationObject.h"
#include "../../redJobs2/include/jobBuilder.h"

namespace res
{

ResourceLoadingFencePtr ResourceLoadingFence::Create()
{
	ResourceLoadingFencePtr ret;
	ret.Reset( RED_NEW( ResourceLoadingFence ) );
	return ret;
}

ResourceLoadingFencePtr ResourceLoadingFence::Create( const job::Counter& waitCounter )
{
	ResourceLoadingFencePtr ret;
	ret.Reset( RED_NEW( ResourceLoadingFence ) );
	ret->InitWithSyncObject( waitCounter );
	return ret;
}

void ResourceLoadingFence::InitWithSyncObject( const job::Counter& waitCounter )
{
	RED_VERIFY( !m_isInitialized.Exchange( true ), "Double initialization attempt!" );

	// Keep self alive while job in progress
	m_refCount.Increment();
	
	job::Builder builder{ job::Priority::Latent };

	builder.DispatchWait( waitCounter );

	builder.DispatchJob( "LoadingFence", [this]( const job::RunContext& ) {
		FinishLoading();
	});
}

Int32 ResourceLoadingFence::Release()
{
	return m_refCount.Decrement();
}

void ResourceLoadingFence::AddRef()
{
	m_refCount.Increment();
}

const job::Counter& ResourceLoadingFence::GetWaitCounter() const
{
	RED_FATAL_ASSERT(m_isInitialized.GetValue());
	return m_waitCounter;
}

ResourceLoadingFence::ResourceLoadingFence()
	: m_waitCounter{ job::Priority::Latent }
	, m_refCount( 1 ) // start at one for intrusive ptr
{
	m_deferral = m_waitCounter.CreateDeferral();
}

ResourceLoadingFence::~ResourceLoadingFence()
{
	RED_FATAL_ASSERT( m_refCount.GetValue() == 0 );
	RED_FATAL_ASSERT( m_isInitialized.GetValue() );
}

#ifdef MUST_PUMP_WIN32_MESSAGES_FOR_DXGI
	static void PumpWindowsMessagesForDXGI()
	{
		// Not bothering to filter as experimentally makes no difference and supported by technical reference. Quit message is posted, so won't miss processing 
		// it here. Doubtful PM_NOYIELD makes any practical difference here if some other process is waiting for the launcher to be initialized with WaitForInputIdle,
		// but using it as we're not processing all messages in the main loop.

		// See "GetMessage and PeekMessage Internals" by Bob Gunderson.
		/*
		Applications that do not desire window-handle filtering may pass a NULL value to GetMessageand PeekMessage in the hwnd parameter.
		Similarly, passing NULL values in bothuMsgFilterMin and uMsgFilterMax parameters disables message-range filtering.

		It is important to realize that only mouse and keyboard hardware messages, posted messages, WM_PAINT messages, and timer messages can be filtered.
		Most messages a window procedure receives are sent directly to the window using SendMessage.
		*/

		// mainthread check mainly for the sake of not making GPumpingMessagesOutsideMainLoop TLS, since PeekMessage would be a no-op anyway
		if ( ::SIsMainThread() )
		{
			MSG msg;
			PeekMessage( &msg, nullptr, 0, 0, PM_NOREMOVE | PM_QS_SENDMESSAGE | PM_NOYIELD );
		}
	}
#endif

void ResourceLoadingFence::Wait()
{
	RED_FATAL_ASSERT( ::SIsMainThread() );
	// #tbd: obviously a better solution is needed
	// but at least this shouldn't happen during regular gameplay or loading
	// #tbd: pump windows messages too...?
	while ( !m_isFinished.GetValue() )
	{

		continue;
	}
}

Bool ResourceLoadingFence::IsFinished() const
{
	return m_isFinished.GetValue();
}

void ResourceLoadingFence::FinishLoading()
{
	m_isFinished.SetValue( true );
	m_deferral.FinishDeferral();

	const Int32 newRefCount = m_refCount.Decrement();
	if ( newRefCount == 0 ) // intrusiveptr can't help us here
	{
		RED_DELETE( this );
	}
}

}
