#include "build.h"
#include "allocatorMetricsSerializerRegistry.h"
#include "assert.h"
#include "../include/allocatorMetricsLogger.h"

namespace red
{
namespace memory
{
namespace
{
	AllocatorMetricsDeserializerPtr s_defaultAllocatorMetricsDeserializer = &CoreAllocatorsMetricsLogger;
}

	AllocatorMetricsProcessorRegistry::AllocatorMetricsProcessorRegistry()
		: m_currentNode( 0 )
	{
		Memzero( m_nodes, sizeof( AllocatorMetricsNode ) * c_poolMaxCount );
	}

	void AllocatorMetricsProcessorRegistry::RegisterMetricsProcessor( PoolHandle poolHandle, ProxyTypeId allocatorId, AllocatorMetricsSerializerPtr serializer, AllocatorMetricsDeserializerPtr deserializer )
	{
		u32 currentNode = m_currentNode.PostIncrement();
		RED_MEMORY_ASSERT( currentNode < c_poolMaxCount, "Failed to register allocator metrics processor. Not enough space" );
		m_nodes[ currentNode ] = { poolHandle, allocatorId, serializer, deserializer };
	}

	void AllocatorMetricsProcessorRegistry::SerializeMetrics( PoolHandle poolHandle, ProxyTypeId allocatorId, void * allocator, Serializer & serializer ) const
	{
		auto it = std::find_if( m_nodes, m_nodes + c_poolMaxCount, FindAllocatorMetricsNodeComparator( poolHandle, allocatorId ) );
		RED_MEMORY_ASSERT( it != m_nodes + c_poolMaxCount, "Failed to find allocator metrics processor." );
		( *it->serializer )( allocator, serializer );
	}

	void AllocatorMetricsProcessorRegistry::DeserializeMetrics( PoolHandle poolHandle, ProxyTypeId allocatorId, Deserializer & deserializer ) const
	{
		auto it = std::find_if( m_nodes, m_nodes + c_poolMaxCount, FindAllocatorMetricsNodeComparator( poolHandle, allocatorId ) );

		if ( it != m_nodes + c_poolMaxCount )
		{
			( *it->deserializer )( allocatorId, deserializer );
		}
		else
		{
			( *s_defaultAllocatorMetricsDeserializer )( allocatorId, deserializer );
		}
	}

	AllocatorMetricsProcessorRegistry::FindAllocatorMetricsNodeComparator::FindAllocatorMetricsNodeComparator( PoolHandle poolHandle, ProxyTypeId allocatorId )
		: m_poolHandle( poolHandle )
		, m_allocatorId( allocatorId )
	{}

	bool AllocatorMetricsProcessorRegistry::FindAllocatorMetricsNodeComparator::operator()( const AllocatorMetricsNode& otherNode ) const
	{
		return m_poolHandle == otherNode.poolHandle && m_allocatorId == otherNode.allocatorId;
	}
}
}