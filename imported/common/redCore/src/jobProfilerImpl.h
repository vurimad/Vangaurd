/**
* Copyright (c) 2016 CD Projekt Red. All Rights Reserved.
*/

#pragma once

#if !defined(RED_FINAL_BUILD) || defined(RED_PROFILE_BUILD)

#include "../include/absolutePath.h"
#include "../include/jobProfiler.h"
#include "../include/profilerBlockFileWriter.h"

/// Profiler for job manager
class REDCORE_API CJobProfiler : public red::profiler::job::IDebugExecutionReporter
{
public:
	CJobProfiler();
	~CJobProfiler();

	enum EBlockType
	{
		eBlockType_JobChain = 1,
		eBlockType_Job = 2,
	};

	enum ESignalType
	{
		eSignalType_JobAdded = 1,
		eSignalType_JobStalled = 2,
		eSignalType_JobResumed = 3,
		eSignalType_JobDependency = 4,
	};

	// General initialize
	void Initilize( const red::AbsolutePath& basePath );
	void Enable( Bool enable );

	// Start/Stop profiling
	void Start();
	void Stop();

	// Are we profiling ?
	const Bool IsProfiling() const;

	// red::profiler::job::IDebugExecutionReporter interface implementation
	RED_FORCE_INLINE void OnJobChainExecuted( Uint64 chainID ) override;
	RED_FORCE_INLINE void OnJobChainFinished( Uint64 chainID ) override;
	RED_FORCE_INLINE void OnJobAddedToDispatcher( Uint64 chainID, Uint16 jobID, const AnsiChar* jobName, Uint32 jobColor ) override;
	RED_FORCE_INLINE void OnJobStarted( Uint64 chainID, Uint16 jobID, const AnsiChar* jobName, Uint32 jobColor, Uint32 threadID ) override;
	RED_FORCE_INLINE void OnJobFinished( Uint64 chainID, Uint16 jobID, const AnsiChar* jobName, Uint32 jobColor, Uint32 threadID ) override;
	RED_FORCE_INLINE void OnJobStalled( Uint64 chainID, Uint16 jobID, const AnsiChar* jobName, Uint32 jobColor, Uint32 threadID ) override;
	RED_FORCE_INLINE void OnJobResumed( Uint64 chainID, Uint16 jobID, const AnsiChar* jobName, Uint32 jobColor, Uint32 threadID ) override;
	RED_FORCE_INLINE void OnJobDependenciesReleased( Uint64 chainID, Uint16 fromJobID, Uint16 toJobIDs[], Uint32 count ) override;

private:
	red::AbsolutePath			m_basePath;
	CProfilerBlockFileWriter	m_writer;

	void AssembleFilePath( red::AbsolutePath& outAbsoluteFilePath ) const;
};

extern CJobProfiler GJobProfiler;

#include "jobProfilerImpl.inl"

#endif // !defined(RED_FINAL_BUILD) || defined(RED_PROFILE_BUILD)
