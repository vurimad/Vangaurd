/**
* Copyright (c) 2014 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"
#include "profilerFileWriter.h"
#include "profilerBlockFileWriter.h"
#include "../../redSystem/include/threads.h"

namespace helper {

Uint64 CalculateUniqueEngineInstanceID()
{
	red::DateTime dt;
	red::Clock::GetInstance().GetLocalTime( dt );
	return ( static_cast< Uint64 >( dt.GetDateRaw() ) << 32) | (static_cast< Uint64 >( dt.GetTimeRaw() ) );
}

Uint64 GetClockTicks()
{
	Uint64 ticks;
	red::Clock::GetInstance().GetTimer().GetTicks( ticks );
	return ticks;
}

} // helper

CProfilerBlockFileWriter::CProfilerBlockFileWriter()
	: m_writer( nullptr )
{
	red::Memzero( m_blocks, sizeof(m_blocks) );
	red::Memzero( m_signals, sizeof(m_signals) );
	red::Memzero( m_counter, sizeof(m_counter) );
}

CProfilerBlockFileWriter::~CProfilerBlockFileWriter()
{
}

Bool CProfilerBlockFileWriter::Start( const red::AbsolutePath& absoluteFilePath )
{
	static Uint64 GEngineInstanceID = helper::CalculateUniqueEngineInstanceID();
	static Uint64 GEngineInstanceBaseTick = helper::GetClockTicks();

	red::ScopedLock< red::Mutex > lock( m_lock );

	// stop current profiling
	if ( IsProfiling() )
		Stop();

	// cleanup string tables
	m_strings.Clear();

	// create writer
	m_writer = CProfilerFileWriter::Open( absoluteFilePath );
	if ( m_writer )
	{
		// write initial stuff
		{
			CProfilerMessage< Header > msg( m_writer );
			msg->m_magic = 'RDIO';
			msg->m_version = 2;
			msg->m_engineInstanceId = GEngineInstanceID;
			msg->m_engineInstanceTickBase = GEngineInstanceBaseTick;
			msg->m_mainThread = red::ThreadId::CurrentThread().AsNumber();
			red::Clock::GetInstance().GetTimer().GetTicks( msg->m_tickBase );
			red::Clock::GetInstance().GetTimer().GetFrequency( msg->m_tickFreq );
		}

		// flush block defines
		for ( Uint32 i=0; i<MAX_DEFS; ++i )
			if ( m_blocks[i].m_defined )
				WriteBlockDefine( (TBlockID)i, m_blocks[i] );

		// flush counter defines
		for ( Uint32 i=0; i<MAX_DEFS; ++i )
			if ( m_counter[i].m_defined )
				WriteCounterDefine( (TCounterID)i, m_counter[i] );

		// flush signal defines
		for ( Uint32 i=0; i<MAX_DEFS; ++i )
			if ( m_signals[i].m_defined )
				WriteSignalDefine( (TSignalID)i, m_signals[i] );

		// flush the header into the file
		m_writer->Flush();
		return true;
	}
	else
	{
		RED_LOG_ERROR( "Core: Unable to create output file '%hs'", absoluteFilePath.AsChar() );
		return false;
	}
}

void CProfilerBlockFileWriter::Stop()
{
	// close file
	if ( m_writer )
	{
		m_writer->Flush();
		RED_DELETE( m_writer );
		m_writer = nullptr;
	}
}

const Bool CProfilerBlockFileWriter::IsProfiling() const
{
	return (m_writer != nullptr);
}

void CProfilerBlockFileWriter::Flush()
{
	if ( m_writer )
	{
		m_writer->Flush();
	}
}

//----

void CProfilerBlockFileWriter::RegisterBlockType( const TBlockID id, const AnsiChar* name, const Uint32 numStartParams, const Uint32 numEndParams )
{
	red::ScopedLock< red::Mutex > lock( m_lock );

	// ID already registered
	if ( m_blocks[id].m_defined )
	{
		RED_LOG_WARNING( "Core: Invalid ID%d for profiling block %hs or ID already used", id, name );
		return;
	}

	// Define the crap
	m_blocks[id].m_defined = true;
	m_blocks[id].m_name = name;
	m_blocks[id].m_numStartParams = numStartParams;
	m_blocks[id].m_numEndParams = numEndParams;

	// Send the definition info
	if ( m_writer )
	{
		WriteBlockDefine( id, m_blocks[id] );
	}
}

void CProfilerBlockFileWriter::RegisterSignalType( const TSignalID id, const AnsiChar* name, const Uint32 numParams )
{
	red::ScopedLock< red::Mutex > lock( m_lock );

	// ID already registered
	if ( m_signals[id].m_defined )
	{
		RED_LOG_WARNING( "Core: Invalid ID%d for profiling signal %hs or ID already used", id, name );
		return;
	}

	// Define the crap
	m_signals[id].m_defined = true;
	m_signals[id].m_name = name;
	m_signals[id].m_numParams = numParams;

	// Send the definition info
	if ( m_writer )
	{
		WriteSignalDefine( id, m_signals[id] );
	}
}

void CProfilerBlockFileWriter::RegisterCounterType( const TCounterID id, const AnsiChar* name )
{
	red::ScopedLock< red::Mutex > lock( m_lock );

	// ID already registered
	if ( m_counter[id].m_defined )
	{
		RED_LOG_WARNING( "Core: Invalid ID%d for profiling counter %hs or ID already used", id, name );
		return;
	}

	// Define the crap
	m_counter[id].m_defined = true;
	m_counter[id].m_name = name;

	// Send the definition info
	if ( m_writer )
	{
		WriteCounterDefine( id, m_counter[id] );
	}
}

const Uint32 CProfilerBlockFileWriter::MapString( const red::AbsolutePath& path )
{
	return MapString( path.AsChar() );
}

const Uint32 CProfilerBlockFileWriter::MapString( const String& path )
{
	return MapString( path.AsChar() );
}

const Uint32 CProfilerBlockFileWriter::MapString( const UniChar* path )
{
	return MapString( UNICODE_TO_ANSI( path ) );
}

const Uint32 CProfilerBlockFileWriter::MapString( const AnsiChar* path )
{
	// do not map strings when not profiling
	if ( !IsProfiling() )
		return 0;

	// calculate path hash
	const Uint32 stringHash = red::CalculateAnsiHash32LowerCase( path );

	red::ScopedLock< red::Mutex > lock( m_lock );

	// no paths
	if ( !m_writer || !path || !path[0] )
		return 0;

	if ( !m_strings.Exist( stringHash ) )
	{
		// prepare a message specifying path name details
		const Uint32 length = (Uint32) red::Strlen( path );
		const Uint32 size = sizeof(Uint32)*3 + length;
		Uint8* raw = m_writer->AllocMessage( size );

		// message header
		*(Uint16*)(raw+0) = eEvent_DefString;
		*(Uint16*)(raw+2) = 0; // ID
		*(Uint32*)(raw+4) = stringHash;
		*(Uint32*)(raw+8) = length;

		// copy string
		red::Memcpy( raw+12, path, length );

		// finish message
		m_writer->FinishMessage();
	}

	// return string HASH - to be used instead of costly string
	return stringHash;
}

void CProfilerBlockFileWriter::SetThreadName( const AnsiChar* threadName )
{
	red::ScopedLock< red::Mutex > lock( m_lock );

	// get internal thread ID
	const Uint32 threadID = red::ThreadId::CurrentThread().AsNumber();

	// set thread name
	Bool added = false;
	for ( Uint32 i=0; i<m_threads.Size(); ++i )
	{
		if ( m_threads[i].m_id == threadID )
		{
			m_threads[i].m_name = threadName;
			added = true;
			break;
		}
	}

	// add new thread entry
	if ( !added )
	{
		ThreadInfo info;
		info.m_id = threadID;
		info.m_name = threadName;
		m_threads.PushBack( info );
	}

	// no name
	if ( !m_writer || !threadName || !threadName[0] )
		return;

	// prepare a message specifying path name details
	const Uint32 length = (Uint32) red::Strlen( threadName );
	const Uint32 size = sizeof(Uint32)*3 + length;
	Uint8* raw = m_writer->AllocMessage( size );

	// message header
	*(Uint32*)(raw+0) = eEvent_ThreadName;
	*(Uint32*)(raw+4) = threadID;
	*(Uint32*)(raw+8) = length;

	// copy string
	red::Memcpy( raw+12, threadName, length );

	// finish message
	m_writer->FinishMessage();
}

void CProfilerBlockFileWriter::Start( const TBlockID blockType, const Uint8 numParams, const Uint32* params /* = nullptr */ )
{
	// check definition
	RED_ASSERT( m_blocks[blockType].m_defined, "Using undefined block %d", blockType );
	if ( !m_blocks[blockType].m_defined )
		return;

	// check parameter count
	RED_ASSERT( m_blocks[blockType].m_numStartParams == numParams, "Parameter count mismatch for start of block %s, expected %d, provided %d",
		m_blocks[blockType].m_name.AsChar(), m_blocks[blockType].m_numStartParams, numParams );
	if ( m_blocks[blockType].m_numStartParams != numParams )
		return;

	// no writer
	if ( !m_writer )
		return;

	// prepare message
	const Uint32 messageSize = (sizeof(Message)-sizeof(Uint32)) + sizeof(Uint32) * numParams;
	Message* message = (Message*) m_writer->AllocMessage( messageSize );

	// message header
	message->m_type = eEvent_StartBlock;
	message->m_id = blockType;
	message->m_threadId = red::ThreadId::CurrentThread().AsNumber();
	message->m_timestamp = red::Clock::GetInstance().GetTimer().GetTicks();

	// copy data
	for ( Uint32 i = 0; i < numParams; ++i )
		message->m_data[i] = params[i];

	// finish message
	m_writer->FinishMessage();
}

void CProfilerBlockFileWriter::End( const TBlockID blockType, const Uint8 numParams, const Uint32* params /* = nullptr */ )
{
	// check definition
	RED_ASSERT( m_blocks[blockType].m_defined, "Using undefined block %d", blockType );
	if ( !m_blocks[blockType].m_defined )
		return;

	// check parameter count
	RED_ASSERT( m_blocks[blockType].m_numEndParams == numParams, "Parameter count mismatch for end of block %s, expected %d, provided %d",
		m_blocks[blockType].m_name.AsChar(), m_blocks[blockType].m_numEndParams, numParams );
	if ( m_blocks[blockType].m_numEndParams != numParams )
		return;

	// no writer
	if ( !m_writer )
		return;

	// prepare message
	const Uint32 messageSize = (sizeof(Message)-sizeof(Uint32)) + sizeof(Uint32) * numParams;
	Message* message = (Message*) m_writer->AllocMessage( messageSize );

	// message header
	message->m_type = eEvent_EndBlock;
	message->m_id = blockType;
	message->m_threadId = red::ThreadId::CurrentThread().AsNumber();
	message->m_timestamp = red::Clock::GetInstance().GetTimer().GetTicks();

	// copy data
	for ( Uint32 i = 0; i < numParams; ++i )
		message->m_data[i] = params[i];

	// finish message
	m_writer->FinishMessage();
}

void CProfilerBlockFileWriter::Signal( const TSignalID signalType, const Uint8 numParams, const Uint32* params /* = nullptr */ )
{
	// check definition
	RED_ASSERT( m_signals[signalType].m_defined, "Using undefined signal %d", signalType );
	if ( !m_signals[signalType].m_defined )
		return;

	// check parameter count
	RED_ASSERT( m_signals[signalType].m_numParams == numParams, "Parameter count mismatch for signal %s, expected %d, provided %d",
		m_signals[signalType].m_name.AsChar(), m_signals[signalType].m_numParams, numParams );
	if ( m_signals[signalType].m_numParams != numParams )
		return;

	// no writer
	if ( !m_writer )
		return;

	// prepare message
	const Uint32 messageSize = (sizeof(Message)-sizeof(Uint32)) + sizeof(Uint32) * numParams;
	Message* message = (Message*) m_writer->AllocMessage( messageSize );

	// message header
	message->m_type = eEvent_Signal;
	message->m_id = signalType;
	message->m_threadId = red::ThreadId::CurrentThread().AsNumber();
	message->m_timestamp = red::Clock::GetInstance().GetTimer().GetTicks();

	// copy data
	for ( Uint32 i = 0; i < numParams; ++i )
		message->m_data[i] = params[i];

	// finish message
	m_writer->FinishMessage();
}

void CProfilerBlockFileWriter::CounterIncrement( const TCounterID counterType, const Uint32 value )
{
	// check definition
	RED_ASSERT( m_counter[counterType].m_defined, "Using undefined counter %d", counterType );
	if ( !m_counter[counterType].m_defined )
		return;

	// no writer
	if ( !m_writer )
		return;

	// prepare message
	const Uint32 messageSize = sizeof(Message);// + sizeof(Uint32);
	Message* message = (Message*) m_writer->AllocMessage( messageSize );

	// message header
	message->m_type = eEvent_IncrementCounter;
	message->m_id = counterType;
	message->m_threadId = red::ThreadId::CurrentThread().AsNumber();
	message->m_timestamp = red::Clock::GetInstance().GetTimer().GetTicks();
	message->m_data[0] = value;

	// finish message
	m_writer->FinishMessage();
}

void CProfilerBlockFileWriter::CounterDecrement( const TCounterID counterType, const Uint32 value )
{
	// check definition
	RED_ASSERT( m_counter[counterType].m_defined, "Using undefined counter %d", counterType );
	if ( !m_counter[counterType].m_defined )
		return;

	// no writer
	if ( !m_writer )
		return;

	// prepare message
	const Uint32 messageSize = sizeof(Message);// + sizeof(Uint32);
	Message* message = (Message*) m_writer->AllocMessage( messageSize );

	// message header
	message->m_type = eEvent_DecrementCounter;
	message->m_id = counterType;
	message->m_threadId = red::ThreadId::CurrentThread().AsNumber();
	message->m_timestamp = red::Clock::GetInstance().GetTimer().GetTicks();
	message->m_data[0] = value;

	// finish message
	m_writer->FinishMessage();
}

void CProfilerBlockFileWriter::WriteBlockDefine( const TBlockID id, const BlockInfo& info )
{
	// no writer
	if ( !m_writer )
		return;

	// prepare message
	const Uint32 nameLength = info.m_name.Length();
	const Uint32 messageSize = sizeof(Entry) + nameLength;
	Entry* message = (Entry*) m_writer->AllocMessage( messageSize );

	// message header
	message->m_type = eEvent_DefBlock;
	message->m_id = id;
	message->m_numParams1 = (Uint8)info.m_numStartParams;
	message->m_numParams2 = (Uint8)info.m_numEndParams;
	message->m_nameLength = nameLength;

	// name
	red::Memcpy( (Uint8*) message + sizeof(Entry), info.m_name.AsChar(), nameLength );

	// finish message
	m_writer->FinishMessage();
}

void CProfilerBlockFileWriter::WriteSignalDefine( const TSignalID id, const SignalInfo& info )
{
	// no writer
	if ( !m_writer )
		return;

	// prepare message
	const Uint32 nameLength = info.m_name.Length();
	const Uint32 messageSize = sizeof(Entry) + nameLength;
	Entry* message = (Entry*) m_writer->AllocMessage( messageSize );

	// message header
	message->m_type = eEvent_DefSignal;
	message->m_id = id;
	message->m_numParams1 = (Uint8)info.m_numParams;
	message->m_numParams2 = 0;
	message->m_nameLength = nameLength;

	// name
	red::Memcpy( (Uint8*) message + sizeof(Entry), info.m_name.AsChar(), nameLength );

	// finish message
	m_writer->FinishMessage();
}

void CProfilerBlockFileWriter::WriteCounterDefine( const TCounterID id, const CounterInfo& info )
{
	// no writer
	if ( !m_writer )
		return;

	// prepare message
	const Uint32 nameLength = info.m_name.Length();
	const Uint32 messageSize = sizeof(Entry) + nameLength;
	Entry* message = (Entry*) m_writer->AllocMessage( messageSize );

	// message header
	message->m_type = eEvent_DefCounter;
	message->m_id = id;
	message->m_numParams1 = 1;
	message->m_numParams2 = 0;
	message->m_nameLength = nameLength;

	// name
	red::Memcpy( (Uint8*) message + sizeof(Entry), info.m_name.AsChar(), nameLength );

	// finish message
	m_writer->FinishMessage();
}