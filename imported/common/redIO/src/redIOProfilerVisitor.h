/*
* Copyright (c) 2018 CD Projekt Red. All Rights Reserved.
*/

#pragma once

#ifdef RED_PLATFORM_ORBIS

namespace io
{
namespace orbis
{

// FIXME: should just be able to save a pooled string
// struct TimelineEvent
// {
// 	String name;
// 	Uint64 timestamp;
// };

struct DecompressionEvent
{
	String fileName;
	Uint64 timestampBegin{ 0 };
	Uint64 timestampEnd{ 0 };
	Uint32 threadID{ 0 };
};

struct IOWorkerCPUEvent
{
	Uint64 timestampBegin{ 0 };
	Uint64 timestampEnd{ 0 };
	Uint32 priority{ 0 };
};

struct MarkEvent
{
	String eventName;
	Uint64 timestamp{ 0 };
};

// #tbd: is requestID globally unique?
struct SubmitUniqueID
{
	Uint64 submitSyscallUniqueID{ 0 };
	Uint64 requestID{ 0 };

	Bool operator==( const SubmitUniqueID& rhs ) const
	{
		return submitSyscallUniqueID == rhs.submitSyscallUniqueID && requestID == rhs.requestID;
	}

	Bool operator!=( const SubmitUniqueID& rhs ) const
	{
		return !( *this == rhs );
	}

	Bool operator<( const SubmitUniqueID& rhs ) const
	{
		if ( submitSyscallUniqueID != rhs.submitSyscallUniqueID )
		{
			return submitSyscallUniqueID < rhs.submitSyscallUniqueID;
		}

		return requestID < rhs.requestID;
	}
};

struct AioFileInfo
{
	Int32 fileDescriptor{ 0 };
	Uint64 offset{ 0 };
	Uint64 numberOfBytes{ 0 };
	Uint64 bufferAddress{ 0 };
};

struct AioAttachedEvent
{
	enum Type : Uint8
	{
		Type_GameOrPatch,
		Type_Others,
	};

	enum Status : Uint8
	{
		Status_Queued,
		Status_IssuedSplit,
	};

	SubmitUniqueID submitUniqueID;
	Uint64 timestamp{ 0 };
	Uint32 aioSubmitID{ 0 };
	Bool isSchedulingTarget{ false };
	Type type{};
	Status status{};
	Uint32 priority{ 0 };
	AioFileInfo fileInfo;
};

struct AioSubmitCmdInEvent
{
	SubmitUniqueID submitUniqueID;
	Uint64 timestamp{ 0 };
	Uint32 priority{ 0 };
	AioFileInfo fileInfo;
};

struct AioInEvent
{
	SubmitUniqueID submitUniqueID;
	Uint64 timestamp{ 0 };
};

struct AioOutEvent
{
	SubmitUniqueID submitUniqueID;
	Uint64 timestamp{ 0 };
	Uint64 returnValue{ 0 };
};

class IProfileStreamVisitor
{
	RED_USE_MEMORY_POOL( red::PoolDebug );

public:
	virtual ~IProfileStreamVisitor();

	virtual void OnDecompression( const DecompressionEvent& event ) {}

	virtual void OnIOWorkerCPU( const IOWorkerCPUEvent& event ) {}

	virtual void OnMark( const MarkEvent& event ) {}

	virtual void OnAioSubmitCmdInRead( const AioSubmitCmdInEvent& event ) {}
	//virtual void OnAioSubmitCmdOutRead( const AioSubmitCmdInEvent& event ) {}

	virtual void OnAioSubmitCmdInWrite( const AioSubmitCmdInEvent& event ) {}
	//virtual void OnAioSubmitCmdOutWrite( const AioSubmitCmdInEvent& event ) {}

	virtual void OnAioMultiWaitIn() {}
	//virtual void OnAioMultiWaitOut() {}

	virtual void OnAioMultiDeleteIn() {}
	//virtual void OnAioMultiDeleteOut() {}

	virtual void OnAioInit() {}

	virtual void OnAioInRead( const AioInEvent& event ) {}
	virtual void OnAioOutRead( const AioOutEvent& event ) {}

	virtual void OnAioInWrite( const AioInEvent& event ) {}
	virtual void OnAioOutWrite( const AioOutEvent& event ) {}

	virtual void OnAioAttachedRead( const AioAttachedEvent& event ) {}
	virtual void OnAioAttachedWrite( const AioAttachedEvent& event ) {}

	virtual void OnBIO2Start() {}
	virtual void OnBIO2Done() {}

protected:
	IProfileStreamVisitor();
};

} // orbis
} // io

#endif // RED_PLATFORM_ORBIS