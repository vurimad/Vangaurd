/*
 * Copyright (c) 2017 CD Projekt Red. All Rights Reserved.
 */

#pragma once

#include "dataErrorHandler.h"

namespace data
{

//------------------------------------------------------------------------------

// Global error handler which just prints the message to the log at the correct level
// TODO: Integrate with Witan's error reporting framework
// NOTE: Calling Fail here will do nothing.
class GlobalErrorHandler final : public ErrorHandler
{
public:
	GlobalErrorHandler();
	~GlobalErrorHandler();

	virtual void ReportWarning( STATIC_CHECK_PRINTF_MSC const char* message, ... ) override;
	virtual void ReportError( STATIC_CHECK_PRINTF_MSC const char* message, ... ) override;
	virtual bool HasFailed() const override;
	virtual void Fail() override;
};

//------------------------------------------------------------------------------

// Task specific error handler which handles failing a single task
// It forwards all other messages to the parent error handler (usually the GlobalErrorHandler)
class TaskErrorHandler final : public ErrorHandler
{
public:
	TaskErrorHandler( ErrorHandler& parent, const res::ResourcePath& path );
	~TaskErrorHandler();

	virtual void ReportWarning( STATIC_CHECK_PRINTF_MSC const char* message, ... ) override;
	virtual void ReportError( STATIC_CHECK_PRINTF_MSC const char* message, ... ) override;
	virtual bool HasFailed() const override;
	virtual void Fail() override;

private:
	ErrorHandler& m_parent;
	const res::ResourcePath m_path;
	bool m_failed;
};

} // data
