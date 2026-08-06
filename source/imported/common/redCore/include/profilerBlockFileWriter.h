/**
* Copyright (c) 2007-16 CD Projekt Red. All Rights Reserved.
*/
#pragma once

#include "absolutePath.h"
#include "../../redSystem/include/types.h"
#include "../../redSystem/include/redThreadsThread.h"
#include "../../redContainers/include/redContainersPublic.h"

class CProfilerFileWriter;

/// Profiler writer capable of writing generic messages
class CProfilerBlockFileWriter
{
public:
	CProfilerBlockFileWriter();
	~CProfilerBlockFileWriter();

	// typedefs
	typedef Uint8 TBlockID;
	typedef Uint8 TSignalID;
	typedef Uint8 TCounterID;

	// internal event types
	enum EEventType
	{
		// WARNING:
		// do not modify/remove entries in this enum - backward compatibility must be preserved!

		eEvent_Invalid = 0,				// invalid event (not used)

		// config
		eEvent_DefString = 1,			// register string as hash (maps string to hash)
		eEvent_DefBlock = 2,			// register block
		eEvent_DefSignal = 3,			// register signal
		eEvent_DefCounter = 4,			// register counter

		// data
		eEvent_ThreadName = 10,			// register thread name
		eEvent_Signal = 11,				// general signal
		eEvent_StartBlock = 12,			// custom block, one param allowed
		eEvent_EndBlock = 13,			// custom block, one param allowed
		eEvent_IncrementCounter = 14,	// increment a counter value
		eEvent_DecrementCounter = 15,	// decrement a counter value
	};

	// Start/Stop profiling
	Bool Start( const red::AbsolutePath& absoluteFilePath );
	void Stop();

	// Flush current profiling
	void Flush();

	// Are we profiling ?
	const Bool IsProfiling() const;

	// Map generic string to hash
	const Uint32 MapString( const red::AbsolutePath& path );
	const Uint32 MapString( const red::String& path );
	const Uint32 MapString( const red::AnsiChar* path );
	const Uint32 MapString( const red::UniChar* path );

	// Set thread name
	void SetThreadName( const red::AnsiChar* threadName );

	// Register generic types of signals
	void RegisterBlockType( const TBlockID id, const AnsiChar* name, const Uint32 numStartParams, const Uint32 numEndParams );
	void RegisterSignalType( const TSignalID id, const AnsiChar* name, const Uint32 numParams );
	void RegisterCounterType( const TCounterID id, const AnsiChar* name );

	// Block start
	void Start( const TBlockID blockType, const Uint8 numParams, const Uint32* params = nullptr );

	// Block end
	void End( const TBlockID blockType, const Uint8 numParams, const Uint32* params = nullptr );

	// Emit profiling signal
	void Signal( const TSignalID type, const Uint8 numParams, const Uint32* params = nullptr );

	// Update counter value
	void CounterIncrement( const TCounterID counter, const Uint32 value );
	void CounterDecrement( const TCounterID counter, const Uint32 value );

private:
	static const Uint32 MAX_DEFS = 256;

	// internal writer
	CProfilerFileWriter*			m_writer;

	// hashed strings
	typedef red::HashSet< Uint32 >	TMappedFilePaths;
	TMappedFilePaths				m_strings{ red::PoolDebug() };

	// access lock
	red::Mutex		m_lock;

	// profiling block info
	struct BlockInfo
	{
		red::String		m_name;
		Uint8			m_numStartParams;
		Uint8			m_numEndParams;
		Bool			m_defined;
	};

	// profiling signal info
	struct SignalInfo
	{
		red::String		m_name;
		Uint8			m_numParams;
		Bool			m_defined;
		Bool			m_writen;
	};

	// profiling counter info
	struct CounterInfo
	{
		red::String		m_name;
		Bool			m_defined;
		Bool			m_writen;
	};

	// profiling thread name
	struct ThreadInfo
	{
		red::String		m_name;
		Uint32			m_id;
	};

	// defined profiler "layout"
	BlockInfo 			m_blocks[ MAX_DEFS ];
	SignalInfo 			m_signals[ MAX_DEFS ];
	CounterInfo			m_counter[ MAX_DEFS ];

	// threads
	red::DynArray< ThreadInfo >		m_threads{ red::PoolDebug() };

	// write defines
	void WriteBlockDefine( const TBlockID id, const BlockInfo& info );
	void WriteSignalDefine( const TSignalID id, const SignalInfo& info );
	void WriteCounterDefine( const TCounterID id, const CounterInfo& info );

#pragma pack(push)
#pragma pack(1)
	struct Header
	{
		Uint32	m_magic;
		Uint32	m_version;
		Uint32	m_mainThread;
		Uint64	m_tickBase;
		Uint64	m_tickFreq;
		Uint64	m_engineInstanceId;
		Uint64	m_engineInstanceTickBase;
	};
#pragma pack(pop)

#pragma pack(push)
#pragma pack(1)
	struct Message
	{
		Uint16	m_type; // event type
		Uint16	m_id; // block/signal/counter ID
		Uint32	m_threadId;
		Uint64	m_timestamp;
		Uint32	m_data[1];
	};
#pragma pack(pop)

#pragma pack(push)
#pragma pack(1)
	struct Entry
	{
		Uint16	m_type; // event type
		Uint16	m_id; // block/signal/counter ID
		Uint8	m_numParams1;
		Uint8	m_numParams2;
		Uint32	m_nameLength;
	};
#pragma pack(pop)
};
