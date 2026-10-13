/*
* Copyright (c) 2018 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"
#include "redIOProfilerOrbis.h"

#if defined( RED_PLATFORM_ORBIS ) && defined( USE_PROFILER )

// Should be in assert.h probably...
// order of expect vs actual is same as gtest 
#define PROFILER_ASSERT_EQUALS_U32( expected, actual ) RED_FATAL_ASSERT( (expected) == (actual), #expected " != " #actual ". Expected=%u, actual=%u", (expected), (actual) )

namespace io
{
namespace orbis
{

// #todo: better, much better. First just want to see how useful the data presentation is
static thread_local Uint64 t_timeBegin = 0;

namespace helper
{
	template< typename T >
	void CopyAndConsume( T* dest, const void* buffer, Uint64* offset, Uint64 bufferSize )
	{
		const Uint32 copySize = sizeof( T );
		RED_FATAL_ASSERT( *offset + copySize <= bufferSize );
		red::Memcpy( dest, (const char*)buffer + *offset, copySize );
		*offset += copySize;
	}

	///---

	template< typename TAioCmdInOp >
	void InitAioSubmitCmdInReadEvent( const ScePerfTraceAioSubmitCmdInCommon& aioSubmitCmd, const TAioCmdInOp& cmdInRead, AioSubmitCmdInEvent& outEvent )
	{
		outEvent.submitUniqueID.submitSyscallUniqueID = aioSubmitCmd.s_id;
		outEvent.submitUniqueID.requestID = cmdInRead.reqid;
		outEvent.timestamp = aioSubmitCmd.header.time;
		outEvent.priority = aioSubmitCmd.prio;
		outEvent.fileInfo.offset = cmdInRead.offset;
		outEvent.fileInfo.numberOfBytes = cmdInRead.nbyte;
		outEvent.fileInfo.bufferAddress = cmdInRead.buf;
		outEvent.fileInfo.fileDescriptor = cmdInRead.fd;
	}

	Bool TryConsumeAioSubmitCmdIn( const ScePerfTraceHeader& header, const void* buffer, Uint64* offset, Uint64 bufferSize, IProfileStreamVisitor& visitor )
	{
		if ( header.type != SCE_PERF_TRACE_AIO || header.optional_type != SCE_PERF_TRACE_OPT_TYPE_AIO_SYSCALL_ARG_IN || header.id != 1 /*aio_submit_cmd*/ )
		{
			return false;
		}

		ScePerfTraceAioSubmitCmdInCommon aioSubmitCmdInCommon;
		CopyAndConsume( &aioSubmitCmdInCommon, buffer, offset, bufferSize );

		const Uint32 numCmdIns = aioSubmitCmdInCommon.num;
		for ( Uint32 i = 0; i < numCmdIns; ++i )
		{
			ScePerfTraceAioSubmitCmdIn cmdIn;
			CopyAndConsume( &cmdIn, buffer, offset, bufferSize );
			if ( cmdIn.optyp == SCE_PERF_TRACE_AIO_OP_READ )
			{
				ScePerfTraceAioSubmitCmdInRead cmdInRead;
				PROFILER_ASSERT_EQUALS_U32( sizeof( cmdIn ) + sizeof( cmdInRead ), cmdIn.req_size );
				CopyAndConsume( &cmdInRead, buffer, offset, bufferSize );

				AioSubmitCmdInEvent event;
				InitAioSubmitCmdInReadEvent( aioSubmitCmdInCommon, cmdInRead, event );
				visitor.OnAioSubmitCmdInRead( event );
			}
			else if ( cmdIn.optyp == SCE_PERF_TRACE_AIO_OP_WRITE )
			{
				ScePerfTraceAioSubmitCmdInWrite cmdInWrite;
				PROFILER_ASSERT_EQUALS_U32( sizeof( cmdIn ) + sizeof( cmdInWrite ), cmdIn.req_size );
				CopyAndConsume( &cmdInWrite, buffer, offset, bufferSize );

				AioSubmitCmdInEvent event;
				InitAioSubmitCmdInReadEvent( aioSubmitCmdInCommon, cmdInWrite, event );
				visitor.OnAioSubmitCmdInWrite( event );
			}
			else
			{
				RED_FATAL( "Unexpected ScePerfTraceAioSubmitCmdIn operation type: %u", cmdIn.optyp );
			}
		}

		return true;
	}

	Bool TraceConsumeAioSubmitCmdOut( const ScePerfTraceHeader& header, const void* buffer, Uint64* offset, Uint64 bufferSize )
	{
		if ( header.type != SCE_PERF_TRACE_AIO || header.optional_type != SCE_PERF_TRACE_OPT_TYPE_AIO_SYSCALL_ARG_OUT || header.id != 1 /*aio_submit_cmd*/ )
		{
			return false;
		}

		ScePerfTraceAioSubmitCmdOutCommon aioSubmitCmdOutCommon;
		CopyAndConsume( &aioSubmitCmdOutCommon, buffer, offset, bufferSize );

		const Uint32 numCmdOuts = aioSubmitCmdOutCommon.num;
		for ( Uint32 i = 0; i < numCmdOuts; ++i )
		{
			ScePerfTraceAioSubmitCmdOut cmdOut;
			CopyAndConsume( &cmdOut, buffer, offset, bufferSize );
		}

		return true;
	}

	///---

	Bool TryConsumeAioMultiWaitIn( const ScePerfTraceHeader& header, const void* buffer, Uint64* offset, Uint64 bufferSize )
	{
		if ( header.type != SCE_PERF_TRACE_AIO || header.optional_type != SCE_PERF_TRACE_OPT_TYPE_AIO_SYSCALL_ARG_IN || header.id != 2 /*aio_multi_wait*/ )
		{
			return false;
		}

		ScePerfTraceAioMultiWaitInCommon aioMultiWaitInCommon;
		CopyAndConsume( &aioMultiWaitInCommon, buffer, offset, bufferSize );
		
		const Uint32 numWaitIns = aioMultiWaitInCommon.num;

		PROFILER_ASSERT_EQUALS_U32( header.size, sizeof( aioMultiWaitInCommon ) + numWaitIns * sizeof( ScePerfTraceAioMultiWaitIn ) );

		for ( Uint32 i = 0; i < numWaitIns; ++i )
		{
			ScePerfTraceAioMultiWaitIn waitIn;
			CopyAndConsume( &waitIn, buffer, offset, bufferSize );
		}

		return true;
	}

	Bool TryConsumeAioMultiWaitOut( const ScePerfTraceHeader& header, const void* buffer, Uint64* offset, Uint64 bufferSize )
	{
		if ( header.type != SCE_PERF_TRACE_AIO || header.optional_type != SCE_PERF_TRACE_OPT_TYPE_AIO_SYSCALL_ARG_OUT || header.id != 2 /*aio_multi_wait*/ )
		{
			return false;
		}

		ScePerfTraceAioMultiWaitOutCommon aioMultiWaitOutCommon;
		CopyAndConsume( &aioMultiWaitOutCommon, buffer, offset, bufferSize );
		
		const Uint32 numWaitOuts = aioMultiWaitOutCommon.num;

		for ( Uint32 i = 0; i < numWaitOuts; ++i )
		{
			ScePerfTraceAioMultiWaitOut waitOut;
			CopyAndConsume( &waitOut, buffer, offset, bufferSize );
		}

		return true;
	}

	///---

	Bool TryConsumeAioMultiPollIn( const ScePerfTraceHeader& header, const void* buffer, Uint64* offset, Uint64 bufferSize )
	{
		if ( header.type != SCE_PERF_TRACE_AIO || header.optional_type != SCE_PERF_TRACE_OPT_TYPE_AIO_SYSCALL_ARG_IN || header.id != 3 /*aio_multi_poll*/ )
		{
			return false;
		}
		RED_FATAL( "Not implemented" );
		return true;
	}

	Bool TryConsumeAioMultiPollOut( const ScePerfTraceHeader& header, const void* buffer, Uint64* offset, Uint64 bufferSize )
	{
		if ( header.type != SCE_PERF_TRACE_AIO || header.optional_type != SCE_PERF_TRACE_OPT_TYPE_AIO_SYSCALL_ARG_OUT || header.id != 3 /*aio_multi_poll*/ )
		{
			return false;
		}
		RED_FATAL( "Not implemented" );
		return true;
	}

	///---

	Bool TryConsumeAioMultiCancelIn( const ScePerfTraceHeader& header, const void* buffer, Uint64* offset, Uint64 bufferSize )
	{
		if ( header.type != SCE_PERF_TRACE_AIO || header.optional_type != SCE_PERF_TRACE_OPT_TYPE_AIO_SYSCALL_ARG_IN || header.id != 4 /*aio_multi_cancel*/ )
		{
			return false;
		}
		RED_FATAL( "Not implemented" );
		return true;
	}

	Bool TryConsumeAioMultiCancelOut( const ScePerfTraceHeader& header, const void* buffer, Uint64* offset, Uint64 bufferSize )
	{
		if ( header.type != SCE_PERF_TRACE_AIO || header.optional_type != SCE_PERF_TRACE_OPT_TYPE_AIO_SYSCALL_ARG_OUT || header.id != 4 /*aio_multi_cancel*/ )
		{
			return false;
		}
		RED_FATAL( "Not implemented" );
		return true;
	}

	///---

	Bool TryConsumeAioMultiDeleteIn( const ScePerfTraceHeader& header, const void* buffer, Uint64* offset, Uint64 bufferSize )
	{
		if ( header.type != SCE_PERF_TRACE_AIO || header.optional_type != SCE_PERF_TRACE_OPT_TYPE_AIO_SYSCALL_ARG_IN || header.id != 5 /*aio_multi_delete*/ )
		{
			return false;
		}

		ScePerfTraceAioMultiDeleteInCommon aioMultiDeleteInCommon;
		CopyAndConsume( &aioMultiDeleteInCommon, buffer, offset, bufferSize );

		const Uint32 numDeleteIns = aioMultiDeleteInCommon.num;

		PROFILER_ASSERT_EQUALS_U32( header.size, sizeof( aioMultiDeleteInCommon ) + numDeleteIns * sizeof( ScePerfTraceAioMultiDeleteIn ) );

		for ( Uint32 i = 0; i < numDeleteIns; ++i )
		{
			ScePerfTraceAioMultiDeleteIn deleteIn;
			CopyAndConsume( &deleteIn, buffer, offset, bufferSize );
		}

		return true;
	}

	Bool TryConsumeAioMultiDeleteOut( const ScePerfTraceHeader& header, const void* buffer, Uint64* offset, Uint64 bufferSize )
	{
		if ( header.type != SCE_PERF_TRACE_AIO || header.optional_type != SCE_PERF_TRACE_OPT_TYPE_AIO_SYSCALL_ARG_OUT || header.id != 5 /*aio_multi_delete*/ )
		{
			return false;
		}

		ScePerfTraceAioMultiDeleteOutCommon aioMultiDeleteOutCommon;
		CopyAndConsume( &aioMultiDeleteOutCommon, buffer, offset, bufferSize );

		const Uint32 numDeleteOuts = aioMultiDeleteOutCommon.num;
		
		PROFILER_ASSERT_EQUALS_U32( header.size, sizeof( aioMultiDeleteOutCommon ) + numDeleteOuts * sizeof( ScePerfTraceAioMultiDeleteOut ) );

		for ( Uint32 i = 0; i < numDeleteOuts; ++i )
		{
			ScePerfTraceAioMultiDeleteOut deleteOut;
			CopyAndConsume( &deleteOut, buffer, offset, bufferSize );
		}

		return true;
	}

	///---

	Bool TryConsumeAioInit( const ScePerfTraceHeader& header, const void* buffer, Uint64* offset, Uint64 bufferSize )
	{
		RED_FATAL_ASSERT( ( header.type != SCE_PERF_TRACE_AIO || header.id != 6 /*aio_init*/ ) || header.optional_type == SCE_PERF_TRACE_OPT_TYPE_AIO_SYSCALL_ARG_IN, "ARG_OUT unexpected for aio_init" );
		if ( header.type != SCE_PERF_TRACE_AIO || header.optional_type != SCE_PERF_TRACE_OPT_TYPE_AIO_SYSCALL_ARG_IN || header.id != 6 /*aio_init*/ )
		{
			return false;
		}

		ScePerfTraceAioInitCommon aioInitCommon;
		CopyAndConsume( &aioInitCommon, buffer, offset, bufferSize );

		const Uint32 numInits = aioInitCommon.num;

		PROFILER_ASSERT_EQUALS_U32( header.size, sizeof( aioInitCommon ) + numInits * sizeof( ScePerfTraceAioInit ) );

		for ( Uint32 i = 0; i < numInits; ++i )
		{
			ScePerfTraceAioInit init;
			CopyAndConsume( &init, buffer, offset, bufferSize );

			RED_FATAL_ASSERT( init.type == SCE_PERF_TRACE_AIO_INIT_TYPE_SCHED_PARAM ); // the only documented type
			ScePerfTraceAioInitSchedParam initSchedParam;
			PROFILER_ASSERT_EQUALS_U32( init.psize, sizeof( init ) + sizeof( initSchedParam ) );
			CopyAndConsume( &initSchedParam, buffer, offset, bufferSize );
		}

		return true;
	}

	///---

	void InitAioInEvent( const ScePerfTraceAioIn& aioIn, AioInEvent& outEvent )
	{
		outEvent.submitUniqueID.submitSyscallUniqueID = aioIn.ss_id;
		outEvent.submitUniqueID.requestID = aioIn.reqid;
		outEvent.timestamp = aioIn.header.time;
	}

	Bool TryConsumeAioIn( const ScePerfTraceHeader& header, const void* buffer, Uint64* offset, Uint64 bufferSize, IProfileStreamVisitor& visitor )
	{
		if ( header.type != SCE_PERF_TRACE_AIO || header.optional_type != SCE_PERF_TRACE_OPT_TYPE_AIO_AIO_IN )
		{
			return false;
		}

		ScePerfTraceAioIn aioIn;
		CopyAndConsume( &aioIn, buffer, offset, bufferSize );

		// Documented as "operation type" but basically a union, and id is at this byte offset
		if ( aioIn.header.id == SCE_PERF_TRACE_AIO_OP_READ )
		{
			AioInEvent event;		
			InitAioInEvent( aioIn, event );
			visitor.OnAioInRead( event );
		}
		else if ( aioIn.header.id == SCE_PERF_TRACE_AIO_OP_WRITE )
		{
			AioInEvent event;
			InitAioInEvent( aioIn, event );
			visitor.OnAioInWrite( event );
		}
		else
		{
			RED_FATAL( "Unexpected operation type: ", aioIn.header.id );
		}
		
		return true;
	}

	void InitAioOutEvent( const ScePerfTraceAioOut& aioOut, AioOutEvent& outEvent )
	{
		outEvent.submitUniqueID.submitSyscallUniqueID = aioOut.ss_id;
		outEvent.submitUniqueID.requestID = aioOut.reqid;
		outEvent.timestamp = aioOut.header.time;
		outEvent.returnValue = aioOut.ret;
	}

	Bool TryConsumeAioOut( const ScePerfTraceHeader& header, const void* buffer, Uint64* offset, Uint64 bufferSize, IProfileStreamVisitor& visitor )
	{
		if ( header.type != SCE_PERF_TRACE_AIO || header.optional_type != SCE_PERF_TRACE_OPT_TYPE_AIO_AIO_OUT )
		{
			return false;
		}

		ScePerfTraceAioOut aioOut;
		CopyAndConsume( &aioOut, buffer, offset, bufferSize );

		// Documented as "operation type" but basically a union, and id is at this byte offset
		if ( aioOut.header.id == SCE_PERF_TRACE_AIO_OP_READ )
		{
			AioOutEvent event;
			InitAioOutEvent( aioOut, event );
			visitor.OnAioOutRead( event );
		}
		else if ( aioOut.header.id == SCE_PERF_TRACE_AIO_OP_WRITE )
		{
			AioOutEvent event;
			InitAioOutEvent( aioOut, event );
			visitor.OnAioOutWrite( event );
		}
		else
		{
			RED_FATAL( "Unexpected operation type: ", aioOut.header.id );
		}

		return true;
	}

	///---

	template< typename TScePerfTraceAioAttachedOp >
	void InitAioAttachedEvent( const ScePerfTraceAioAttachedCommon& aioAttachedCommon, const ScePerfTraceAioAttached& aioAttached, const TScePerfTraceAioAttachedOp& aioAttachedOp, AioAttachedEvent& outEvent )
	{
		outEvent.timestamp = aioAttachedCommon.header.time;
		outEvent.submitUniqueID.submitSyscallUniqueID = aioAttached.s_id;
		outEvent.submitUniqueID.requestID = aioAttached.reqid;
		outEvent.aioSubmitID = aioAttached.subid;
		outEvent.isSchedulingTarget = aioAttached.attr.window == 0; // Yes, zero for true here
		outEvent.type = aioAttached.attr.type == 0 ? AioAttachedEvent::Type_GameOrPatch : AioAttachedEvent::Type_Others;
		outEvent.priority = aioAttached.attr.prio;
		outEvent.status = aioAttached.attr.status == 0 ? AioAttachedEvent::Status_Queued : AioAttachedEvent::Status_IssuedSplit;
		outEvent.fileInfo.fileDescriptor = aioAttachedOp.fd;
		outEvent.fileInfo.offset = aioAttachedOp.offset;
		outEvent.fileInfo.numberOfBytes = aioAttachedOp.nbyte;
		outEvent.fileInfo.bufferAddress = aioAttachedOp.buf;
	}

	Bool TryConsumeAioAttached( const ScePerfTraceHeader& header, const void* buffer, Uint64* offset, Uint64 bufferSize, IProfileStreamVisitor& visitor )
	{
		if ( header.type != SCE_PERF_TRACE_AIO || header.optional_type != SCE_PERF_TRACE_OPT_TYPE_AIO_ATTACHED )
		{
			return false;
		}

		ScePerfTraceAioAttachedCommon aioAttachedCommon;
		CopyAndConsume( &aioAttachedCommon, buffer, offset, bufferSize );

		// Documented as "num", but "ScePerfTraceAioAttachedCommon.num" is basically a union, and ScePerfTraceHeader.id as at this byte offset
		const Uint32 numAttacheds = aioAttachedCommon.header.id;	

		for ( Uint32 i = 0; i < numAttacheds; ++i )
		{
			ScePerfTraceAioAttached attached;
			CopyAndConsume( &attached, buffer, offset, bufferSize );
			if ( attached.optyp == SCE_PERF_TRACE_AIO_OP_READ )
			{
				ScePerfTraceAioAttachedRead attachedRead;
				PROFILER_ASSERT_EQUALS_U32( sizeof( attached ) + sizeof( attachedRead ), attached.reqsz );
				CopyAndConsume( &attachedRead, buffer, offset, bufferSize );

				AioAttachedEvent event;
				InitAioAttachedEvent( aioAttachedCommon, attached, attachedRead, event );
				visitor.OnAioAttachedRead( event );
			}
			else if ( attached.optyp == SCE_PERF_TRACE_AIO_OP_WRITE )
			{
				ScePerfTraceAioAttachedWrite attachedWrite;
				PROFILER_ASSERT_EQUALS_U32( sizeof( attached ) + sizeof( attachedWrite ), attached.reqsz );
				CopyAndConsume( &attachedWrite, buffer, offset, bufferSize );

				AioAttachedEvent event;
				InitAioAttachedEvent( aioAttachedCommon, attached, attachedWrite, event );
				visitor.OnAioAttachedWrite( event );
			}
			else
			{
				RED_FATAL( "Unexpected operation type: %u", attached.optyp );
			}
		}

		return true;
	}

	///---

	Bool TryConsumeBIO2Start( const ScePerfTraceHeader& header, const void* buffer, Uint64* offset, Uint64 bufferSize )
	{
		if ( header.type != SCE_PERF_TRACE_IO || header.optional_type != SCE_PERF_TRACE_OPT_TYPE_IO_BIO2 || header.size != 0x40 /* bio2 start*/ )
		{
			return false;
		}

		RED_FATAL( "Untested. Previous PS4 SDK had issues trying to collect these events" );
		
		//?? static_assert( sizeof( ScePerfTraceIoBio2NextEvent ) == 0x40, "" );

		ScePerfTraceIoBio2NextEvent bio2NextEvent;
		CopyAndConsume( &bio2NextEvent, buffer, offset, bufferSize );

		return true;

	}

	Bool TryConsumeBIO2Done( const ScePerfTraceHeader& header, const void* buffer, Uint64* offset, Uint64 bufferSize )
	{
		if ( header.type != SCE_PERF_TRACE_IO || header.optional_type != SCE_PERF_TRACE_OPT_TYPE_IO_BIO2 || header.size != 0x20 /* bio2 done */ )
		{
			return false;
		}
		
		RED_FATAL( "Untested. Previous PS4 SDK had issues trying to collect these events" );

		//?? static_assert( sizeof( ScePerfTraceIoBio2DoneEvent ) == 0x20, "" );

		ScePerfTraceIoBio2DoneEvent bio2DoneEvent;
		CopyAndConsume( &bio2DoneEvent, buffer, offset, bufferSize );

		return true;
	}
} // helper

Profiler::PerfTraceInfo Profiler::InitPerfTrace( const ProfilerSetup& setup )
{
	Profiler::PerfTraceInfo ret;

	if ( m_info.aioTraceID != -1 )
	{
		scePerfTraceDelete( m_info.aioTraceID );
	}

	if ( m_info.bio2TraceID != -1 )
	{
		scePerfTraceDelete( m_info.bio2TraceID );
	}

	m_info = PerfTraceInfo{};

	Int32 res = scePerfTraceCreate( setup.traceBufferSize, &ret.aioTraceID, &ret.aioTraceBuffer );
	RED_FATAL_ASSERT( res == SCE_OK, "scePerfTraceCreate failed: 0x%08X", res );
	res = scePerfTraceEnable( ret.aioTraceID, SCE_PERF_TRACE_AIO, 0 );
	RED_FATAL_ASSERT( res == SCE_OK, "scePerfTraceEnable SCE_PERF_TRACE_AIO failed: 0x%08X", res );

// 	res = scePerfTraceCreate( setup.traceBufferSize, &ret.bio2TraceID, &ret.bio2TraceBuffer );
// 	RED_FATAL_ASSERT( res == SCE_OK, "scePerfTraceCreate failed: 0x%08X", res );
// 	res = scePerfTraceEnable( ret.bio2TraceID, SCE_PERF_TRACE_IO, SCE_PERF_TRACE_MODE_IO_BIO2 );
// 	RED_FATAL_ASSERT( res == SCE_OK, "scePerfTraceEnable SCE_PERF_TRACE_IO:SCE_PERF_TRACE_MODE_IO_BIO2 failed: 0x%08X", res );

	return ret;
}

Profiler::Profiler( const ProfilerSetup& setup )
	: m_ioWorkerCPUEvents( 3, red::PoolDebug() )
	, m_setup( setup )
	, m_traceStartTimestamp( 0 )
	, m_traceState( TS_Clear )
{
}

Profiler::~Profiler()
{
	(void)StopTrace_NoLock();

	if ( m_info.aioTraceID != -1 )
	{
		scePerfTraceDelete( m_info.aioTraceID );
	}

	if ( m_info.bio2TraceID != -1 )
	{
		scePerfTraceDelete( m_info.bio2TraceID );
	}
}

void Profiler::StartTrace()
{
	RED_SCOPE_LOCK( m_traceLock );

	if ( m_traceState == TS_InProgress )
	{
		(void)StopTrace_NoLock();
	}

	RED_FATAL_ASSERT( m_traceState == TS_Clear || m_traceState == TS_Stopped );

	// #tbd: recreate the buffer. Shouldn't have to, but otherwise get undocumented EAGAIN error code
	// Could also use overwrite mode, but more complicated to manage currently
	m_info = InitPerfTrace( m_setup );

	t_timeBegin = sceKernelReadTsc();
	m_traceState = TS_InProgress;

	{
		RED_SCOPE_LOCK( m_decompressionEventsLock );
		m_decompressionEvents.Clear();
	}

	{
		RED_SCOPE_LOCK( m_ioWorkerCPUEventsLock );
		for ( auto& it : m_ioWorkerCPUEvents )
		{
			it.Clear();
		}
	}

	{
		RED_SCOPE_LOCK( m_markEventsLock );
		m_markEvents.Clear();
	}

	red::Memzero( &m_aioInfo, sizeof( m_aioInfo ) );
	red::Memzero( &m_bio2Info, sizeof( m_bio2Info ) );

	m_traceStartTimestamp = sceKernelReadTsc();

	if ( m_info.bio2TraceID != -1 )
	{
		const Int32 res = scePerfTraceStart( m_info.bio2TraceID, SCE_PERF_TRACE_STOP_BUFFER_FULL );
		RED_FATAL_ASSERT( res == SCE_OK, "scePerfTraceStart SCE_PERF_TRACE_STOP_BUFFER_FULL failed: 0x%08X", res );
	}

	if ( m_info.aioTraceID != -1 )
	{
		const Int32 res = scePerfTraceStart( m_info.aioTraceID, SCE_PERF_TRACE_STOP_BUFFER_FULL );
		RED_FATAL_ASSERT( res == SCE_OK, "scePerfTraceStart SCE_PERF_TRACE_STOP_BUFFER_FULL failed: 0x%08X", res );
	}
}

// #tbd: support wrap mode. See the code samples.
Profiler::ResultInfo Profiler::StopTrace()
{
	RED_SCOPE_LOCK( m_traceLock );
	return StopTrace_NoLock();
}

Profiler::ResultInfo Profiler::StopTrace_NoLock()
{
	ResultInfo resultInfo;
	if ( m_traceState != TS_InProgress )
	{
		resultInfo.validTrace = false;
		return resultInfo;
	}

	resultInfo.traceStartedTimestamp = m_traceStartTimestamp;
	m_traceStartTimestamp = 0;

	m_traceState = TS_Stopped;

	if ( m_info.aioTraceID != -1 )
	{
		const Int32 res = scePerfTraceStop( m_info.aioTraceID );
		RED_FATAL_ASSERT( res == SCE_OK, "scePerfTraceStop failed: 0x%08X", res );
	}

	if ( m_info.bio2TraceID != -1 )
	{
		const Int32 res = scePerfTraceStop( m_info.bio2TraceID );
		RED_FATAL_ASSERT( res == SCE_OK, "scePerfTraceStop failed: 0x%08X", res );
	}
	
	if ( m_info.aioTraceID != -1 )
	{
		const Int32 res = scePerfTraceGetInfo( m_info.aioTraceID, &m_aioInfo );
		RED_FATAL_ASSERT( res == SCE_OK, "scePerfTraceGetInfo failed: 0x%08X", res );
		if ( m_aioInfo.flag == SCE_PERF_TRACE_STOP_BUFFER_FULL )
		{
			RED_LOG_WARNING( "AIO: SCE_PERF_TRACE_STOP_BUFFER_FULL occurred" );
			resultInfo.aioWriteBufferFull = true;
		}
	}

	if ( m_info.bio2TraceID != -1 )
	{
		const Int32 res = scePerfTraceGetInfo( m_info.bio2TraceID, &m_bio2Info );
		RED_FATAL_ASSERT( res == SCE_OK, "scePerfTraceGetInfo failed: 0x%08X", res );
		if ( m_bio2Info.flag == SCE_PERF_TRACE_STOP_BUFFER_FULL )
		{
			RED_LOG_WARNING( "BIO2: SCE_PERF_TRACE_STOP_BUFFER_FULL occurred" );
			resultInfo.bio2WriteBufferFull = true;
		}
	}

	resultInfo.validTrace = true;
	return resultInfo;
}

namespace helper
{
	// read the asyncIO docs... depending on init settings, could break up I/O requests
	// Could graph by checking if any overlapping read events, and create a fake timeline to separate them, and try to keep the same file on the same timeline.
	// Or draw a line connecting them if use a different timeline.
	// What exactly is "AioAttached" anyway? Happens in the middle of a trace.
	static Bool TryConsumeAioTrace( const ScePerfTraceHeader& header, const char* data, Uint64* offset, Uint64 bufferSize, IProfileStreamVisitor& visitor )
	{
		if ( header.type != SCE_PERF_TRACE_AIO )
		{
			return false;
		}

		if ( helper::TryConsumeAioSubmitCmdIn( header, data, offset, bufferSize, visitor ) )
		{
			return true;
		}
		
		if ( helper::TraceConsumeAioSubmitCmdOut( header, data, offset, bufferSize ) )
		{
			return true;
		}
		
		if ( helper::TryConsumeAioMultiWaitIn( header, data, offset, bufferSize ) )
		{
			return true;
		}

		if ( helper::TryConsumeAioMultiWaitOut( header, data, offset, bufferSize ) )
		{
			return true;
		}
		
		if ( helper::TryConsumeAioMultiPollIn( header, data, offset, bufferSize ) )
		{
			return true;
		}
		
		if ( helper::TryConsumeAioMultiPollOut( header, data, offset, bufferSize ) )
		{
			return true;
		}
		
		if ( helper::TryConsumeAioMultiCancelIn( header, data, offset, bufferSize ) )
		{
			return true;
		}
		
		if ( helper::TryConsumeAioMultiCancelOut( header, data, offset, bufferSize ) )
		{
			return true;
		}
		
		if ( helper::TryConsumeAioMultiDeleteIn( header, data, offset, bufferSize ) )
		{
			return true;
		}
		
		if ( helper::TryConsumeAioMultiDeleteOut( header, data, offset, bufferSize ) )
		{
			return true;
		}
		
		if ( helper::TryConsumeAioInit( header, data, offset, bufferSize ) )
		{
			return true;
		}
		
		if ( helper::TryConsumeAioIn( header, data, offset, bufferSize, visitor ) )
		{
			return true;
		}
		
		if ( helper::TryConsumeAioOut( header, data, offset, bufferSize, visitor ) )
		{
			return true;
		}
		
		if ( helper::TryConsumeAioAttached( header, data, offset, bufferSize, visitor ) )
		{
			return true;
		}
		
		RED_FATAL( "Unexpected AIO optional type: %u", header.optional_type );

		return true;
	}

	static Bool TryConsumeIoTrace( const ScePerfTraceHeader& header, const char* data, Uint64* offset, Uint64 bufferSize, IProfileStreamVisitor& visitor )
	{
		if ( header.type != SCE_PERF_TRACE_IO )
		{
			return false;
		}

		if ( helper::TryConsumeBIO2Start( header, data, offset, bufferSize ) )
		{
			return true;
		}

		if ( helper::TryConsumeBIO2Done( header, data, offset, bufferSize ) )
		{
			return true;
		}

		RED_FATAL( "Unexpected IO optional type: %u", header.optional_type );

		return true;
	}

	static void ConsumeTrace( const void* traceBuffer, const ScePerfTraceInfo& info, IProfileStreamVisitor& visitor )
	{
		// Pre-offset the data
		const char* data = static_cast< const char* >( traceBuffer ) + info.offset;

		// Relative offset
		Uint64 prevOffset = 0;
		Uint64 offset = 0;
		for ( ;; )
		{
			prevOffset = offset;
			ScePerfTraceHeader header;
			red::Memcpy( &header, data + offset, sizeof( header ) );
			Bool consumedTrace = false;
			if ( header.type == SCE_PERF_TRACE_TRACE_EVENT )
			{
				consumedTrace = true;
				RED_LOG_ERROR( "SCE_PERF_TRACE_TRACE_EVENT: optype %u", header.optional_type );
				offset += header.size;
			}

			if ( !consumedTrace && helper::TryConsumeAioTrace( header, data, &offset, info.size, visitor ) )
			{
				consumedTrace = true;
			}

			if ( !consumedTrace && helper::TryConsumeIoTrace( header, data, &offset, info.size, visitor ) )
			{
				consumedTrace = true;
			}

			RED_FATAL_ASSERT( consumedTrace, "Unexpected trace type: %u", header.type );

			RED_FATAL_ASSERT( offset == prevOffset + header.size );
			RED_FATAL_ASSERT( offset > prevOffset, "offset <= prevOffset: offset=%llu, prevOffset=%llu", offset, prevOffset );

			if ( offset >= info.size )
			{
				break;
			}
		}
	}
} // helper

void Profiler::VisitTrace( IProfileStreamVisitor& visitor )
{
	RED_SCOPE_LOCK( m_traceLock );
	RED_FATAL_ASSERT( m_traceState == TS_Stopped );

	if ( m_info.aioTraceID != -1 )
	{
		helper::ConsumeTrace( m_info.aioTraceBuffer, m_aioInfo, visitor );
	}

	if ( m_info.bio2TraceID != -1 )
	{
		helper::ConsumeTrace( m_info.bio2TraceBuffer, m_bio2Info, visitor );
	}

	{
		RED_SCOPE_LOCK( m_decompressionEventsLock );
		for ( const auto& event : m_decompressionEvents )
		{
			visitor.OnDecompression( event );
		}
	}

	{
		RED_SCOPE_LOCK( m_ioWorkerCPUEventsLock );
		for ( const auto& it : m_ioWorkerCPUEvents )
		{
			for ( const auto& event : it )
			{
				visitor.OnIOWorkerCPU( event );
			}
		}
	}

	{
		RED_SCOPE_LOCK( m_markEventsLock );
		for ( const auto& event : m_markEvents )
		{
			visitor.OnMark( event );
		}
	}
}

void Profiler::ProfileIOWorkerBegin( Uint32 priority )
{
	t_timeBegin = sceKernelReadTsc();
}

void Profiler::ProfileIOWorkerEnd( Uint32 priority )
{

	if ( GetTraceStateAtomicRelaxed() != TS_InProgress )
	{
		return;
	}

	IOWorkerCPUEvent event;
	event.timestampBegin = t_timeBegin;
	event.timestampEnd = sceKernelReadTsc();
	event.priority = priority;

	RED_SCOPE_SHARED_LOCK( m_ioWorkerCPUEventsLock );
	
	RED_FATAL_ASSERT( priority >= SCE_KERNEL_AIO_PRIORITY_LOW && priority <= SCE_KERNEL_AIO_PRIORITY_HIGH );
	m_ioWorkerCPUEvents[ priority - 1 ].PushBack( event );
}

void Profiler::ProfileDecompressStart( const char* resourcePath )
{
	t_timeBegin = sceKernelReadTsc();
}

void Profiler::ProfileDecompressEnd( const char* resourcePath )
{
	if ( GetTraceStateAtomicRelaxed() != TS_InProgress )
	{
		return;
	}

	const auto threadID = red::ThreadId::CurrentThread();

	DecompressionEvent event;
	event.fileName = resourcePath;
	event.threadID = threadID.AsNumber();
	event.timestampBegin = t_timeBegin;
	event.timestampEnd = sceKernelReadTsc();
	RED_SCOPE_LOCK( m_decompressionEventsLock );
	m_decompressionEvents.PushBack( std::move( event ) );
}

void Profiler::ProfileMarkEvent( const char* eventName )
{
	if ( GetTraceStateAtomicRelaxed() != TS_InProgress )
	{
		return;
	}

	MarkEvent event;
	event.eventName = eventName;
	event.timestamp = sceKernelReadTsc();
	RED_SCOPE_LOCK( m_markEventsLock );
	m_markEvents.PushBack( std::move( event ) );
}

} // orbis
} // io

#endif