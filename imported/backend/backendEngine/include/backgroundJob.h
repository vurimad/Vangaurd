/**
* Copyright (c) 2016 CD Projekt Red. All Rights Reserved.
*/

#pragma once

namespace tools
{

enum EJobState
{
	JS_Initialized,			//!< Job created
	JS_Pending,				//!< Job not run yet
	JS_Failed,				//!< We have failed to process the job
	JS_Finished,			//!< We finished processing the job
};

class BACKEND_ENGINE_API IBackgroundJob : private red::NonCopyable
{
public:
	IBackgroundJob();
	virtual ~IBackgroundJob();

	//! Add internal reference
	void AddRef();

	//! Remove internal reference
	void Release();

	RED_FORCE_INLINE EJobState GetState() const { return m_state.GetValue(); }
	RED_FORCE_INLINE Bool IsDone() const { return m_state.GetValue() > JS_Pending; }
	RED_FORCE_INLINE Bool IsOK() const { return m_state.GetValue() == JS_Finished; }
	RED_FORCE_INLINE Bool IsFailed() const { return m_state.GetValue() == JS_Failed; }
	
	//! Job debug information
	virtual const Char* GetDebugName() const { return TXT("Background job"); }

public:
	void ScheduleJob();
	void RunJob();

protected:
	//! Process the job, is called from job thread
	virtual Bool Process()=0;

private:
	red::Atomic< Int32 >				m_refCount;			//!< Internal reference count
	red::Atomic< EJobState >			m_state;			//!< Job state
};

}
