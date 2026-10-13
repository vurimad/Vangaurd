/**
* Copyright (c) 2020 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"
#include "redIOAsyncReadToken.h"

namespace io
{
	AsyncReadToken::AsyncReadToken()
		: m_callback(nullptr)
		, m_userData(nullptr)
		, m_buffer(nullptr)
		, m_debugLogicalFileName(nullptr)
		, m_offset(0)
		, m_numberOfBytesToRead(0)
		, m_numberOfBytesToAllocateForDecompressor(0)
		, m_ioContext()
		, m_HACK_mightBeTerrainAndNeedsATonOfMemoryForBuffers(false)
		, m_requestSource(RequestSource::Unknown)
	{
	}

	Bool AsyncReadToken::IsCancelRequested() const
	{
		return m_ioContext && m_ioContext->IsCancelRequested();
	}

	Int32 AsyncReadToken::GetQuantizedDistanceToObserverSquared() const
	{
		return m_ioContext ? m_ioContext->GetQuantizedDistanceToObserverSquared() : INT32_MAX;
	}

	void IOContext::RequestCancel()
	{
		if (GetAsyncOpId() != UINT64_MAX) // Was never even submitted for I/O. It can happen with "inplace" resource token loading
		{
			if ( !m_cancelled) {
				m_cancelled = true;
				if (m_cancelCallback)
				{
					m_cancelCallback();
				}
			}
		}
	}

	Bool IOContext::IsCancelRequested() const
	{
		return const_cast<volatile const Bool&>(m_cancelled);
	}

	void IOContext::SetCancelCallback( CancelCallback callback )
	{
		m_cancelCallback = callback;
	}

	void IOContext::SetIOStartCallback(IOStartCallback callback)
	{
		m_ioStartCallback = callback;
	}

	void IOContext::NotifyIOStart()
	{
		if (m_ioStartCallback)
		{
			m_ioStartCallback();
		}
	}

	Bool IOContext::TryUpdateDistanceToObserverSquared(Int32 quantizedDistanceSquared, Uint8 generation, Uint8 recursionLevel)
	{
		// #tbd: or within some tolerance to avoid recursive update for small differences. However, the distance is already divided by a factor
		// to quantize it a bit more.	
		if ( m_distanceGeneration != generation )
		{
			//RED_FATAL_ASSERT((m_distanceGeneration == 255 && generation == 0) || (generation == m_distanceGeneration + 1));
			m_distanceGeneration = generation;
			m_quantizedDistanceSquared = quantizedDistanceSquared;
			m_recursionLevel = recursionLevel;
			return true;
		}
		else if (quantizedDistanceSquared < m_quantizedDistanceSquared)
		{
			m_quantizedDistanceSquared = quantizedDistanceSquared;
			m_recursionLevel = recursionLevel;
			return true;
		}

		return false;
	}

	void IOContext::SetLoadingState(LoadingState state)
	{
		m_loadingState = state;
	}

	IOContext::LoadingState IOContext::GetLoadingState() const
	{
		return m_loadingState;
	}

	void IOContext::SetAsyncOpId(Uint64 id)
	{
		m_asyncOpId = id;
	}

#ifdef ENTRY
#error ENTRY already defined
#endif
#define ENTRY(x) case RequestSource::x: return #x
	const char* GetRequestSourceDebugText(RequestSource requestSource)
	{
		switch (requestSource)
		{
			ENTRY(Unknown);
			ENTRY(Tools);
			ENTRY(ShaderCache);
			ENTRY(UnknownAsyncSource);
			ENTRY(UnknownReadInline);
			ENTRY(ICUInternationalization);
			ENTRY(ResourceSystem);
			ENTRY(ResourceSystem_BufferAsyncProxy);
			ENTRY(ResourceSystem_BufferAsyncProxy_ReadInline);
			ENTRY(VideoSystem_PreReadInline);
			ENTRY(VideoSystem_MainReadInline);
			ENTRY(VideoSystem_PostReadInline);
			ENTRY(AudioSystem_SoundBankManager);
			ENTRY(AudioSystem_WwiseContainerStreaming);
			ENTRY(AudioSystem_WwiseLowLevelIO);
		default:
			break;
		}
		return "Unknown";
	}
#undef ENTRY

}