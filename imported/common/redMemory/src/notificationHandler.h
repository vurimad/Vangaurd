/**
 * Copyright (c) 2016 CD Projekt Red. All Rights Reserved.
 */

#ifndef _RED_MEMORY_NOTIFICATION_HANDLER_H_
#define _RED_MEMORY_NOTIFICATION_HANDLER_H_

namespace red
{
namespace memory
{
	class RED_MEMORY_API NotificationHandler
	{
	public:

		NotificationHandler();
		RED_MOCKABLE ~NotificationHandler();

		RED_MOCKABLE void Warning( const char * message ) const;
		RED_MOCKABLE void Error( const char * error ) const;

	private:

	};

	RED_MEMORY_API void NotifyWarning( const NotificationHandler * handler, STATIC_CHECK_PRINTF_MSC const char * message, ... );
	RED_MEMORY_API void NotifyError( const NotificationHandler * handler, STATIC_CHECK_PRINTF_MSC const char * message, ... );

}
}

#define RED_MEMORY_WARNING( notifier, message, ... ) \
	do { if( notifier ) { red::memory::NotifyWarning( notifier, message, ##__VA_ARGS__ ); } } while ( 0, 0 )

#define RED_MEMORY_ERROR( notifier, message, ... ) \
	do { if( notifier ) { red::memory::NotifyError( notifier, message, ##__VA_ARGS__ ); } } while ( 0, 0 )

#endif
