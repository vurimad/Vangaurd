/*
 * Copyright (c) 2019 CD Projekt Red. All Rights Reserved.
 */

#pragma once

#include "../../../common/redCore/include/absolutePath.h"

#include <chrono>

namespace data
{
namespace metrics
{

using Microseconds = std::chrono::duration<Uint32, std::micro>;
using Milliseconds = std::chrono::duration<Uint32, std::milli>;
using Clock = std::chrono::high_resolution_clock;

//------------------------------------------------------------------------------

struct TaskMetrics
{
	// Microseconds, maximum of ~4294 seconds
	using Duration = metrics::Microseconds;

	// Time taken for each state to get processed
	Duration initTime;
	Duration getStoredDepsTime;
	Duration checkStoredDepsTime;
	Duration getUpdatedDepsTime;
	Duration checkUpdatedDepsTime;
	Duration checkBuildDepsTime;
	Duration checkOutputTime;
	Duration downloadOutputTime;
	Duration buildOutputTime;
	Duration updateMetadataTime;
	Duration cleanOutputTime;

	// Time taken for the internal tool functions to process
	Duration toolInitTime;
	Duration toolCollectDepsTime;
	Duration toolGenerateTime;

	// Total wall clock times for the task from start to finish
	Duration resolveWallClockTime;
	Duration buildWallClockTime;
	Duration totalWallClockTime;

	// Other metrics gathered
	Uint32 filesDownloaded;
	Uint32 filesBuilt;
	Uint32 filesDeleted;

	constexpr TaskMetrics();
};

constexpr TaskMetrics::TaskMetrics()
	: initTime( Duration::zero() )
	, getStoredDepsTime( Duration::zero() )
	, checkStoredDepsTime( Duration::zero() )
	, getUpdatedDepsTime( Duration::zero() )
	, checkUpdatedDepsTime( Duration::zero() )
	, checkBuildDepsTime( Duration::zero() )
	, checkOutputTime( Duration::zero() )
	, downloadOutputTime( Duration::zero() )
	, buildOutputTime( Duration::zero() )
	, updateMetadataTime( Duration::zero() )
	, cleanOutputTime( Duration::zero() )
	, toolInitTime( Duration::zero() )
	, toolCollectDepsTime( Duration::zero() )
	, toolGenerateTime( Duration::zero() )
	, resolveWallClockTime( Duration::zero() )
	, buildWallClockTime( Duration::zero() )
	, totalWallClockTime( Duration::zero() )
	, filesDownloaded( 0 )
	, filesBuilt( 0 )
	, filesDeleted( 0 )
{
}

class StopWatch
{
public:
	StopWatch();
	StopWatch( const StopWatch& );

	StopWatch& operator=( const StopWatch& );

	template < typename Duration >
	Duration Get() const;

	template < typename Duration >
	Duration Step();

	void Start();

private:
	Clock::time_point m_start;
};

RED_INLINE StopWatch::StopWatch()
	: m_start( Clock::now() )
{
}

RED_INLINE StopWatch::StopWatch( const StopWatch& ) = default;
RED_INLINE StopWatch& StopWatch::operator=( const StopWatch& ) = default;

RED_INLINE void StopWatch::Start()
{
	m_start = Clock::now();
}

template < typename Duration >
RED_INLINE Duration StopWatch::Get() const
{
	return std::chrono::duration_cast< Duration >( Clock::now() - m_start );
}

template < typename Duration >
RED_INLINE Duration StopWatch::Step()
{
	auto now = Clock::now();
	auto result = std::chrono::duration_cast< Duration >( now - m_start );
	m_start = now;
	return result;
}

//------------------------------------------------------------------------------

class BACKEND_DATA_API Writer
{
public:
	explicit Writer( red::UniquePtr< IFile >&& file );
	~Writer();

	void Write( const String& str );

	static red::AbsolutePath DefaultMetricsDirectory();
	static red::AbsolutePath ComputeMetricsFilePath( const char* prefix, const red::DateTime& startTime );

private:
	void Flush();

	red::UniqueBuffer m_buffer;
	char* m_begin;
	char* m_current;
	char* m_end;

	red::UniquePtr< IFile > m_file;
};

//------------------------------------------------------------------------------

} // metrics
} // data
