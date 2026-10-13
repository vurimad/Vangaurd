/**
 * Copyright (c) 2017 CD Projekt Red. All Rights Reserved.
 */

#ifndef _RED_SYSTEM_DATA_ERROR_HPP_
#define _RED_SYSTEM_DATA_ERROR_HPP_

#include <cstdio>
#include "hash.h"

namespace red
{
	template< typename T, typename... Args >
	InternalDataError::InternalDataError( const char* message, T&& arg, Args&&... args )
		: m_currentSerializedAction( m_serializedActions )
		, m_messageFormatHash( red::CalculateAnsiHash32( message ) )
	{
		red::Memzero( m_serializedDataError, s_serializedDataErrorMaxSize );
		red::Memzero( m_serializedActions, s_serializedActionsMaxSize );

		std::snprintf( m_serializedDataError, s_serializedDataErrorMaxSize, message, std::forward< T >( arg ), std::forward< Args >( args )... );
	}
}

#endif