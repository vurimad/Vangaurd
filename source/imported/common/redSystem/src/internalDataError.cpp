/**
 * Copyright (c) 2017 CD Projekt Red. All Rights Reserved.
 */

#include "build.h"
#include "internalDataError.h"
#include "dataErrorAction.h"
#include "hash.h"

namespace red
{
	InternalDataError::InternalDataError( const char* message )
		: m_currentSerializedAction( m_serializedActions )
		, m_messageFormatHash( red::CalculateAnsiHash32( message ) )
	{
		red::Memzero( m_serializedDataError, s_serializedDataErrorMaxSize );
		red::Memzero( m_serializedActions, s_serializedActionsMaxSize );

		std::snprintf( m_serializedDataError, s_serializedDataErrorMaxSize, "%s", message );
	}

	AbstractDataError& InternalDataError::AddShowAssetAction( const char* resourcePath )
	{
		return AddShowCustomAssetAction( "Show asset in Asset Browser", resourcePath );
	}

	AbstractDataError& InternalDataError::AddShowEntityAction( const char* resourcePath )
	{
		return AddShowCustomAssetAction( "Show entity in Asset Browser", resourcePath );
	}

	AbstractDataError& InternalDataError::AddShowCustomAssetAction( const char* customDescription, const char* resourcePath )
	{
		RED_ASSERT( m_currentSerializedAction < m_serializedActions + s_serializedActionsMaxSize, "Buffer overflow" );
		const Uint32 freeSize = static_cast< Uint32 >( m_serializedActions + s_serializedActionsMaxSize - m_currentSerializedAction );
		Uint32 bytesStored = 0;
		if ( ShowAssetDataErrorAction( customDescription, resourcePath ).Serialize( m_currentSerializedAction, freeSize, bytesStored ) )
		{
			m_currentSerializedAction += bytesStored;
		}

		return *this;
	}
}