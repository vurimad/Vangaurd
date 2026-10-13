/**
 * Copyright (c) 2015 CD Projekt Red. All Rights Reserved.
 */

#ifndef _RED_MEMORY_POOL_REGISTRY_H_
#define _RED_MEMORY_POOL_REGISTRY_H_

#include "../include/poolTypes.h"
#include "poolConstant.h"
#include "allocatorMetricsSerializerRegistry.h"
#include "assert.h"
#include "memoryStream.h"

namespace red
{
namespace memory
{
	const u32 c_allocatorNameMaxSize = 64;

	class MetricsRegistry;
	struct PoolStorage;

	class RED_MEMORY_API PoolRegistry
	{
	public:

		PoolRegistry();
		~PoolRegistry();

		void Initialize( SystemAllocator * systemAllocator );
		void Uninitialize();

		void Register( PoolHandle handle, const PoolParameter& param );
		void SetupPoolBudget( PoolHandle handle, const char* name, u64 budget );

		void RegisterAllocatorMetricsProcessor(
			PoolHandle poolHandle,
			ProxyTypeId allocatorId,
			AllocatorMetricsSerializerPtr serializer,
			AllocatorMetricsDeserializerPtr deserializer );

		u64 GetPoolBudget( PoolHandle handle ) const;
		const char* GetPoolName( PoolHandle handle ) const;

		void DisableContributeToParentMetrics( PoolHandle handle );
		Bool GetContributeToParentMetrics( PoolHandle handle ) const;

		void SetMirroredPool( PoolHandle handle );

		bool IsPoolRegistered( PoolHandle handle ) const;
		u32 GetPoolCount() const;

		u64 GetTotalBytesAllocated() const;
		i64 GetTotalBytesAllocated( PoolHandle handle, Bool includeChildren = true ) const;

		void WritePoolReportToLog( const MetricsRegistry& registry ) const;
		void WritePoolReportToJson( FILE* file, const MetricsRegistry& registry ) const;
		void WriteBoundAllocatorReportsToLog( const MetricsRegistry& registry );
		void WriteBoundAllocatorReportsToJson( const MetricsRegistry& registry );

		void WritePools( Serializer& serializer );

		void SerializeAllocatorMetrics( Serializer & serializer, Deserializer & deserializer, const char * poolName );
		void SerializePoolMetrics( Serializer & serializer );

		void VisitPoolInfos( const PoolInfoVisitor& visitor, const PoolHandle poolHandle = PoolRoot::GetHandle() ) const;

		void ValidateAllPoolBudget();

	private:

		template< typename T >
		struct AcquireComparator
		{
			AcquireComparator( PoolHandle handle )
				: m_handle( handle )
			{}

			bool operator()( const T& obj ) const
			{
				return obj.handle == m_handle || obj.handle == c_poolNodeInvalid;
			}

		private:
			const PoolHandle m_handle;
		};

		template< typename T >
		struct FindComparator
		{
			FindComparator( PoolHandle handle )
				: m_handle( handle )
			{}

			bool operator()( const T& obj ) const
			{
				return obj.handle == m_handle;
			}

		private:
			const PoolHandle m_handle;
		};

		struct PoolMetric
		{
			PoolHandle handle;
			u64 inclusiveBytesAllocated;
			u64 exclusiveBytesAllocated;

			u32 inclusiveAllocationCount;
			u32 exclusiveAllocationCount;
		};

		struct FindPoolInfoWithUniqueAllocatorComparator
		{
			FindPoolInfoWithUniqueAllocatorComparator( const void * allocator );

			bool operator()( const PoolInfo * poolInfo ) const;

		private:
			const void * m_allocator;
		};

		struct AllocatorMetrics
		{
			char allocatorName[ c_allocatorNameMaxSize ] = { '\0' };
			char poolName[ c_poolNameMaxSize ] = { '\0' };
			char poolNameReference[ c_poolNameMaxSize ] = { '\0' };
			Uint64 poolBudget;
			Uint64 allocatedMemory;
			Uint32 allocationCount;
			u32 poolHandle{ 0 };
			u32 poolHandleReference{ 0 };
			u32 allocatorId{ 0 };
			u8 hasAllocatorSpecificMetrics{ 0 };
		};

		typedef SimpleArray< PoolInfo, c_poolMaxCount > PoolNodes;
		typedef SimpleArray< PoolMetric, c_poolMaxCount > PoolMetrics;
		typedef SimpleArray< const PoolInfo *, c_poolMaxCount > PoolInfosWithUniqueAllocator;

		void SetupPoolInfo( PoolHandle handle, const PoolParameter& param );
		void SetupPoolInfo( PoolHandle handle, const char* name, u64 budget );
		void ComputePoolMetrics( const MetricsRegistry& registry, PoolMetrics& metrics ) const;
		PoolMetric ComputePoolMetric( PoolHandle handle, const MetricsRegistry& registry, PoolMetrics& metrics ) const;
		void WritePoolMetricsToLog( PoolHandle handle, const PoolMetrics & metrics, u32 ident ) const;
		void WritePoolMetricsToJson( FILE* file, const PoolInfo* parentInfo, PoolHandle handle, const PoolMetrics & metrics, u32 ident ) const;
		void GatherBoundAllocatorMetrics( PoolHandle handle, Serializer & serializer, Deserializer & deserializer, const char * poolName, PoolInfosWithUniqueAllocator & poolInfosWithUniqueAllocator, u32 & currentPoolInfoWithUniqueAllocatorIndex ) const;
		void GatherPoolMetrics( PoolHandle handle, const char * parentPoolName, Serializer & serializer );
		void SerializeAllocatorMetrics( PoolHandle handle, Serializer & serializer, Deserializer & deserializer, const  PoolInfo * info, PoolInfosWithUniqueAllocator & poolInfosWithUniqueAllocator, u32 & currentPoolInfoWithUniqueAllocatorIndex ) const;
		void SerializePoolMetrics( const PoolInfo * info, const char ( &parentPoolName )[ c_poolNameMaxSize ], Serializer & serializer );
		void DeserializeAllocatorMetrics( AllocatorMetrics & allocatorMetrics );

		void WriteBoundAllocatorMetricsToLog();
		void WriteBoundAllocatorMetricsToJson();
		void WriteAllocatorMetricsToLog( const AllocatorMetrics & allocatorMetrics, Deserializer & deserializer );
		void WriteAllocatorMetricsToJson( const AllocatorMetrics & allocatorMetrics, Deserializer & deserializer );

		PoolInfo* AcquirePoolInfo( PoolHandle handle );
		const PoolInfo* FindPoolInfo( PoolHandle handle ) const;
		PoolInfo* FindPoolInfo( PoolHandle handle );
		PoolMetric* AcquirePoolMetric( PoolHandle handle, PoolMetrics& metrics ) const;
		const PoolMetric* FindPoolMetric( PoolHandle handle, const PoolMetrics& metrics ) const;

		void VisitPoolInfos( const PoolInfoVisitor& visitor, const PoolHandle poolHandle, const PoolInfo* parent ) const;
		
		void ComputePoolChildrenBudget();
		void ComputePoolChildrenBudget( PoolHandle poolHandle, PoolInfo* parent );

		mutable red::RWSpinLock m_nodesLock;
		PoolNodes m_nodes;
		AllocatorMetricsProcessorRegistry m_allocatorMetricsProcessorRegistry;
		MemoryStream m_memoryStream;
		Serializer m_serializer;
		Deserializer m_deserializer;
	};
}
}

#endif
