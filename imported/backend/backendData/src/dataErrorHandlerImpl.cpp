/*
 * Copyright (c) 2017 CD Projekt Red. All Rights Reserved.
 */

#include "build.h"
#include "dataErrorHandlerImpl.h"

namespace data
{

//------------------------------------------------------------------------------

GlobalErrorHandler::GlobalErrorHandler() = default;

GlobalErrorHandler::~GlobalErrorHandler() = default;

void GlobalErrorHandler::ReportWarning( STATIC_CHECK_PRINTF_MSC const char* message, ... )
{
	va_list arglist;
	va_start( arglist, message );
	String formattedMessage = String::PrintfV( message, arglist );
	va_end( arglist );

	RED_LOG_WARNING( "Data Warning: %hs", formattedMessage.AsChar() );
}

void GlobalErrorHandler::ReportError( STATIC_CHECK_PRINTF_MSC const char* message, ... )
{
	va_list arglist;
	va_start( arglist, message );
	String formattedMessage = String::PrintfV( message, arglist );
	va_end( arglist );

	RED_LOG_ERROR( "Data Error: %hs", formattedMessage.AsChar() );
}

bool GlobalErrorHandler::HasFailed() const
{
	return false;
}

void GlobalErrorHandler::Fail()
{
	RED_FATAL( "Fail() called on the global error handler" );
}

//------------------------------------------------------------------------------

TaskErrorHandler::TaskErrorHandler( ErrorHandler& parent, const res::ResourcePath& path )
	: m_parent( parent )
	, m_path( path )
	, m_failed( false )
{
}

TaskErrorHandler::~TaskErrorHandler() = default;

bool TaskErrorHandler::HasFailed() const
{
	return m_failed;
}

void TaskErrorHandler::ReportWarning( STATIC_CHECK_PRINTF_MSC const char* message, ... )
{
	va_list arglist;
	va_start( arglist, message );
	String formattedMessage = String::PrintfV( message, arglist );
	va_end( arglist );

	m_parent.ReportWarning( "Task[%016llX] %hs", m_path.GetHash(), formattedMessage.AsChar() );
}

void TaskErrorHandler::ReportError( STATIC_CHECK_PRINTF_MSC const char* message, ... )
{
	va_list arglist;
	va_start( arglist, message );
	String formattedMessage = String::PrintfV( message, arglist );
	va_end( arglist );

	m_parent.ReportError( "Task[%016llX] %hs", m_path.GetHash(), formattedMessage.AsChar() );
}

void TaskErrorHandler::Fail()
{
	m_failed = true;
}

//------------------------------------------------------------------------------

} // data
