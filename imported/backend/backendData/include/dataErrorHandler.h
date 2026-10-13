/*
 * Copyright (c) 2017 CD Projekt Red. All Rights Reserved.
 */

#pragma once

namespace data
{

class ErrorHandler
{
	RED_USE_MEMORY_POOL( red::PoolBackend );

public:
	virtual ~ErrorHandler() = 0;

	virtual void ReportWarning( STATIC_CHECK_PRINTF_MSC const char* message, ... ) = 0;
	virtual void ReportError( STATIC_CHECK_PRINTF_MSC const char* message, ... ) = 0;
	virtual bool HasFailed() const = 0;
	virtual void Fail() = 0;
};

} // data
