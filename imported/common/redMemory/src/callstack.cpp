/**
* Copyright (c) 2016 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"
#include "callstack.h"

namespace red
{
namespace memory
{
	Callstack::Callstack()
		:	m_hash( 0 ),
			m_depth( 0 ),
			m_size( 0 ),
			m_callstackMask( 0 )
	{
		m_callstackCollector.GetCallstack( m_callstack, m_depth, m_size, m_callstackMask, m_hash );
	}

	Bool Callstack::SerializeCallstack( Serializer & serializer ) const
	{
		const u32 dataSize = sizeof( m_depth ) + m_size;
		if ( serializer.DataAvailableToWrite() < dataSize )
		{
			return false;
		}

		Bool result = serializer.Serialize( m_depth );
		RED_FATAL_ASSERT( result, "Failed to serialize callstack depth" );

		result = serializer.Serialize( m_callstackMask );
		RED_FATAL_ASSERT( result, "Failed to serialize callstack mask" );

		for ( Uint32 index = 0; index != m_depth; ++index  )
		{
			if ( m_callstackMask & ( 1 << index ) )
			{
				result = serializer.Serialize( m_callstack[ index ] );
				RED_FATAL_ASSERT( result, "Failed to serialize callstack" );
			}
			else
			{
				const i32 diff = static_cast< i32 >( m_callstack[ index ] - m_callstack[ 0 ] );
				result = serializer.Serialize( diff );
				RED_FATAL_ASSERT( result, "Failed to serialize callstack" );
			}
		}

		RED_UNUSED( result );
		return true;
	}

	u32 Callstack::GetSerializationSize() const
	{
		u32 dataSize = sizeof( m_depth ) + sizeof( m_callstackMask );
		for ( Uint32 index = 0; index != m_depth; ++index  )
		{
			if ( m_callstackMask & ( 1 << index ) )
			{
				dataSize += sizeof( m_callstack[ index ] );
			}
			else
			{
				dataSize += sizeof( i32 );
			}
		}

		return dataSize;
	}
}
}
