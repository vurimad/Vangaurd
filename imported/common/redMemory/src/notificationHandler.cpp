/**
 * Copyright (c) 2016 CD Projekt Red. All Rights Reserved.
 */

#include "build.h"
#include "notificationHandler.h"

namespace red
{
namespace memory
{
namespace
{
	const u32 c_maxMessageSize = 256;
}

	NotificationHandler::NotificationHandler()
	{}

	NotificationHandler::~NotificationHandler()
	{}

	void NotificationHandler::Warning( const char *  ) const
	{}
	
	void NotificationHandler::Error( const char * ) const
	{}

	void NotifyWarning( const NotificationHandler * handler, STATIC_CHECK_PRINTF_MSC const char * message, ... )
	{
		char formatedWarningMessage[ c_maxMessageSize ];

		va_list arglist;
		va_start( arglist, message );
		VSNPrintF( formatedWarningMessage, sizeof( formatedWarningMessage ), message, arglist );
		va_end( arglist );

		handler->Warning( formatedWarningMessage );
	}

	void NotifyError( const NotificationHandler * handler, STATIC_CHECK_PRINTF_MSC const char * message, ... )
	{
		char formatedErrorMessage[ c_maxMessageSize ];

		va_list arglist;
		va_start( arglist, message );
		VSNPrintF( formatedErrorMessage, sizeof( formatedErrorMessage ), message, arglist );
		va_end( arglist );

		handler->Error( formatedErrorMessage );
	}
}
}
