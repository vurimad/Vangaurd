/**
 * Copyright (c) 2017 CD Projekt Red. All Rights Reserved.
 */

#ifndef _RED_MEMORY_ALLOCATOR_METRICS_SERIALIZER_REGISTRY_H_
#define _RED_MEMORY_ALLOCATOR_METRICS_SERIALIZER_REGISTRY_H_

#include "poolConstant.h"
#include "../include/proxyTypeId.h"

namespace red
{
namespace memory
{
	class Serializer;
	class Deserializer;

	using AllocatorMetricsSerializerPtr = void(*)( void * allocator, Serializer & serializer );
	using AllocatorMetricsDeserializerPtr = void(*)( ProxyTypeId proxyId, Deserializer & serializer );

	class AllocatorMetricsProcessorRegistry
	{
	public:
		AllocatorMetricsProcessorRegistry();

		void RegisterMetricsProcessor( PoolHandle poolHandle, ProxyTypeId allocatorId, AllocatorMetricsSerializerPtr serializer, AllocatorMetricsDeserializerPtr deserializer );
		void SerializeMetrics( PoolHandle poolHandle, ProxyTypeId allocatorId, void * allocator, Serializer & serializer ) const;
		void DeserializeMetrics( PoolHandle poolHandle, ProxyTypeId allocatorId, Deserializer & deserializer ) const;

	private:

		struct AllocatorMetricsNode
		{
			PoolHandle poolHandle;
			ProxyTypeId allocatorId;
			AllocatorMetricsSerializerPtr serializer;
			AllocatorMetricsDeserializerPtr deserializer;

		};

		struct FindAllocatorMetricsNodeComparator
		{
			FindAllocatorMetricsNodeComparator( PoolHandle poolHandle, ProxyTypeId allocatorId );

			bool operator()( const AllocatorMetricsNode& otherNode ) const;

		private:
			PoolHandle m_poolHandle;
			ProxyTypeId m_allocatorId;
		};

		red::Atomic<u32> m_currentNode;
		AllocatorMetricsNode m_nodes[ c_poolMaxCount ];
	};
}
}

#endif