/**
 * Copyright (c) 2017 CD Projekt Red. All Rights Reserved.
 */

#ifndef _RED_SYSTEM_LOGGER_LOCKLESS_QUEUE_HPP_
#define _RED_SYSTEM_LOGGER_LOCKLESS_QUEUE_HPP_

namespace red
{
	const Uint32 c_loggerQueueMask = c_loggerQueueSize - 1;

	template< typename TLoggerLine >
	LoggerLocklessQueue< TLoggerLine >::LoggerLocklessQueue()
		: m_queuePosition( 0 ),
		m_dequeuePosition( 0 )
	{
		std::memset( &m_entries, 0, sizeof( m_entries ) );

		for ( Uint32 index = 0; index != c_loggerQueueSize; ++index )
		{
			m_entries[ index ].position = index;
		}
	}

	template< typename TLoggerLine >
	LoggerLocklessQueue< TLoggerLine >::~LoggerLocklessQueue()
	{}

	template< typename TLoggerLine >
	bool LoggerLocklessQueue< TLoggerLine >::QueueMessage( TLoggerLine && message )
	{
		Entry * entry = nullptr;
		Uint32 position = m_queuePosition;
		while ( 1 )
		{
			entry = &m_entries[ position & c_loggerQueueMask ];
			Uint32 entryPosition = entry->position;
			const Int32 difference = static_cast< Int32 >( entryPosition ) - static_cast< Int32 >( position );

			if ( difference == 0 )
			{
				if ( atomic::CompareExchange32( &m_queuePosition, position + 1, position ) == position )
					break;
			}
			else if ( difference < 0 )
			{
				return false;
			}

			position = m_queuePosition;
		}

		entry->message = std::move( message );
		atomic::Exchange32( &entry->position, position + 1 );
		return true;
	}

	template< typename TLoggerLine >
	bool LoggerLocklessQueue< TLoggerLine >::DequeueMessage( TLoggerLine & message )
	{
		Entry * entry = nullptr;
		Uint32 position = m_dequeuePosition;
		while ( 1 )
		{
			entry = &m_entries[ position & c_loggerQueueMask ];
			Uint32 entryPosition = entry->position;
			const Int32 difference = static_cast< Int32 >( entryPosition ) - static_cast< Int32 >( position + 1 );
			if ( difference == 0 )
			{
				if ( atomic::CompareExchange32( &m_dequeuePosition, position + 1, position ) == position )
					break;
			}
			else if ( difference < 0 )
			{
				return false;
			}

			position = m_dequeuePosition;
		}

		message = std::move( entry->message );
		atomic::Exchange32( &entry->position, position + c_loggerQueueSize );
		return true;
	}

	REDSYSTEM_API_TEMPLATE template class REDSYSTEM_API LoggerLocklessQueue< LoggerLine >;
	REDSYSTEM_API_TEMPLATE template class REDSYSTEM_API LoggerLocklessQueue< LoggerLineWithDataError >;
}

#endif