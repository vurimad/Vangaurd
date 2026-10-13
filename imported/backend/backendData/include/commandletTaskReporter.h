/*
 * Copyright (c) 2018 CD Projekt Red. All Rights Reserved.
 */

#pragma once

#include "dataTaskReporter.h"

#include "../../../common/core/include/processStatusReporter.h"
#include "../../../common/redSystem/include/stopWatch.h"

#include <chrono>

namespace red
{
class IProcessStatusReporter;
}

namespace commandlet
{

class BACKEND_DATA_API CommandletTaskReporter : public data::TaskReporter
{
public:
	explicit CommandletTaskReporter( red::IProcessStatusReporter& commandletReporter );
	~CommandletTaskReporter();

	void OpenOutputDevice( bool isRunningCommunications );
	void CloseOutputDevice();

	// data::TaskReporter
	virtual void SetTasksCount( Uint32 count ) override;
	virtual void UpdateSystemStatus( const red::String& str ) override;
	virtual void UpdateTaskProgress( Uint32 id, String&& message ) override;
	virtual void ClearTaskProgress( Uint32 id ) override;

private:
	void FlushProgressMessage_NoLock();
	bool ShouldFlushProgressMessage_NoLock();

	red::IProcessStatusReporter& m_commandletReporter;

	red::SpinLock m_guard;
	std::chrono::system_clock::time_point m_nextFlushTimestamp;
	red::Map< Uint32, red::String > m_taskStatusMessages;

	Uint32 m_cachedStageId = red::InvalidStageId;
	Uint32 m_taskId = red::InvalidMessageId;
	Uint32 m_messageId = red::InvalidMessageId;

	Uint32 m_finishedTasksCount = 0;
	Uint32 m_tasksCount = 0;

	red::StopWatch m_timer;

	enum class OutputType : Uint32
	{
		NONE,			// No output generated
		INTERACTIVE,	// Output to communications to show in the editor (or other application)
		CONSOLE,		// Output to console in special format
		DIRECT,			// Output directly to stdout, used when redirecting to a file
	};
	OutputType m_outputType;

	HANDLE m_handle;
	COORD m_statusPosition;
	COORD m_progressPosition;
	Uint32 m_lastLineCount;

	bool m_opened;
};


} // commandlet
