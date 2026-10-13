/**
* Copyright (c) 2014 CD Projekt Red. All Rights Reserved.
*/
#pragma once

#include "../../redMemory/include/uniqueBuffer.h"
#include "../../redMemory/include/sharedPtr.h"
#include "redIOCommon.h"
#include <functional>

namespace io
{
//////////////////////////////////////////////////////////////////////////
// Forward Declarations
//////////////////////////////////////////////////////////////////////////
struct AsyncReadToken;

/////////////////////////////////////////////////////////////////////////
// FAsyncOpCallback
//////////////////////////////////////////////////////////////////////////
typedef void (*FAsyncOpCallback)( 
	const AsyncReadToken& asyncReadToken,	//!< The async token passed into BeginRead.
	red::EAsyncResult asyncResult,			//!< The async result for this operation.
	Uint32 numberOfBytesTransferred,
	io::ShareableIOMemory internalMemoryForIO,
	Uint32 shareableIOMemoryOffset,
	red::UniqueBuffer internalMemoryForDecompressor
);	//!< Valid if eAsyncResult_Success.

//////////////////////////////////////////////////////////////////////////
// AsyncReadToken
//////////////////////////////////////////////////////////////////////////
class REDIO_API IOContext
{
	RED_USE_MEMORY_POOL(red::PoolEngine);

public:
	typedef std::function<void()> CancelCallback; // Really shouldn't exist in final (non-profiling)
	typedef red::FixedSizeFunction<void()> IOStartCallback;

public:

	// #TBD: just purely for debug
	enum class LoadingState : Uint8
	{
		Cancelling,
		Idle,
		QueuedForIO,
		Loading,
		Throttled,
		Decompression,
		Deserialization,
		WaitingForImports,
		PostLoad,
		Finished
	};

	void RequestCancel();
	Bool IsCancelRequested() const;
	void SetCancelCallback( CancelCallback callback );
	void SetIOStartCallback(IOStartCallback callback);
	void NotifyIOStart();

	Bool TryUpdateDistanceToObserverSquared(Int32 quantizedDistanceSquared, Uint8 generation, Uint8 recursionLevel);
	Int32 GetQuantizedDistanceToObserverSquared() const { return m_quantizedDistanceSquared; }
	Uint8 GetRecursionLevel() const { return m_recursionLevel; }

	void SetLoadingState(LoadingState state);
	LoadingState GetLoadingState() const;

	// Useful for fixing priority inversions
	Uint64 GetAsyncOpId() const { return m_asyncOpId; }
	void SetAsyncOpId(Uint64 id);

private:
	CancelCallback m_cancelCallback{ nullptr };
	
	// NOTE: Please don't start using this callback for tool hooks and then convert it into a std::function!!!!
	// If you need to, just make a new callback. Preferably #def it out in final (non-profiling)
	IOStartCallback m_ioStartCallback{ nullptr };
	
	Uint64 m_asyncOpId{ UINT64_MAX };
	Int32 m_quantizedDistanceSquared{ INT32_MAX };
	Uint8 m_distanceGeneration{ 0 };
	Uint8 m_recursionLevel{ 0 };
	Bool m_cancelled{ false };
	LoadingState m_loadingState{ LoadingState::Idle };
};

enum class RequestSource : Uint8
{
	Unknown,
	Tools,
	ShaderCache,
	UnknownAsyncSource,
	UnknownReadInline,
	ICUInternationalization,
	ResourceSystem,
	ResourceSystem_BufferAsyncProxy,
	ResourceSystem_BufferAsyncProxy_ReadInline,
	VideoSystem_PreReadInline,
	VideoSystem_MainReadInline,
	VideoSystem_PostReadInline,
	AudioSystem_SoundBankManager,
	AudioSystem_WwiseContainerStreaming,
	AudioSystem_WwiseLowLevelIO,
	COUNT,
};

REDIO_API const char* GetRequestSourceDebugText(RequestSource requestSource);

struct REDIO_API AsyncReadToken
{
public:
	FAsyncOpCallback	m_callback;				//!< User callback when IO completes, is canceled, or an error occurs

	void*						m_userData;				//!< User data
	void*						m_buffer;				//!< Buffer to read into
	const char*					m_debugLogicalFileName;
	Int64						m_offset;				//!< File offset to read from
	Uint32						m_numberOfBytesToRead;	//!< Number of bytes to read from the file
	Uint32						m_numberOfBytesToAllocateForDecompressor;
	red::SharedPtr< IOContext > m_ioContext; //!< Try to cancel pending I/O or update distance or observer
	Bool						m_HACK_mightBeTerrainAndNeedsATonOfMemoryForBuffers;
	RequestSource				m_requestSource;

public:
	AsyncReadToken();
	Bool IsCancelRequested() const;
	Int32 GetQuantizedDistanceToObserverSquared() const;
};

} // io
