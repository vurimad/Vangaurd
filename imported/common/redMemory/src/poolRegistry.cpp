/**
 * Copyright (c) 2015 CD Projekt Red. All Rights Reserved.
 */

#include "build.h"
#include "poolRegistry.h"
#include "metricsRegistry.h"
#include "memoryStream.h"
#include "../include/allocatorMetricsLogger.h"
#include "../include/tlsfAllocator.h"
#include "../include/defaultAllocator.h"
#include "../include/fixedSizeAllocator.h"
#include "../include/stackAllocator.h"
#include "../include/linearAllocator.h"
#include "../include/circularAllocator.h"

#ifdef RED_CONFIGURATION_FINAL
#include "vault.h"
#endif

#if defined( RED_USE_ORBIS_MEMORY_ANALYZER )
#include "memoryAnalyzerOrbis.h"
#endif

namespace red
{
namespace memory
{
#ifdef RED_MEMORY_ENABLE_REPORT
	extern FILE* s_oomJsonFile;
#endif

#ifdef RED_MEMORY_ENABLE_POOL_INITALIZATION_VALIDATION
	// It is set to true after all pool should be initialized and we can start to validate pool usage.
	bool g_validatePoolInitialization = false;
#endif

	namespace
	{
		const char* c_poolUnkownName = "<Unknown Pool>";

		const char* TrimNamespacesFromPoolName( const char* poolName )
		{
			RED_MEMORY_ASSERT( poolName, "Pool name does not exist" );

			const char* ptr = poolName;
			while ( 1 )
			{
				if ( const char* it = red::Strstr( ptr, "::" ) )
				{
					ptr = ( it + 2 );
				}
				else
				{
					break;
				}
			}

			return ptr;
		}
	}

	PoolRegistry::PoolRegistry()
		: m_memoryStream( Flags_CPU_Read_Write )
	{
		std::memset( &m_nodes, 0, sizeof( m_nodes ) );
	}

	PoolRegistry::~PoolRegistry()
	{}

	void PoolRegistry::Initialize( SystemAllocator* systemAllocator )
	{
		m_memoryStream.Initialize( systemAllocator, 0, RED_KILO_BYTE( 128 ) );
		m_deserializer.Initialize( &m_memoryStream );
		m_serializer.Initialize( &m_memoryStream );
	}

	void PoolRegistry::Uninitialize()
	{
		m_serializer.Uninitialize();
		m_deserializer.Uninitialize();
		m_memoryStream.Uninitialize();
	}

	void PoolRegistry::Register( PoolHandle handle, const PoolParameter& param )
	{
		RED_SCOPE_LOCK( m_nodesLock );
		SetupPoolInfo( handle, param );
	}

	void PoolRegistry::SetupPoolBudget( PoolHandle handle, const char* name, u64 budget )
	{
		RED_SCOPE_LOCK( m_nodesLock );
		SetupPoolInfo( handle, name, budget );
	}

	void PoolRegistry::RegisterAllocatorMetricsProcessor(
		PoolHandle poolHandle,
		ProxyTypeId allocatorId,
		AllocatorMetricsSerializerPtr serializer,
		AllocatorMetricsDeserializerPtr deserializer )
	{
		m_allocatorMetricsProcessorRegistry.RegisterMetricsProcessor( poolHandle, allocatorId, serializer, deserializer );
	}

	u64 PoolRegistry::GetPoolBudget( PoolHandle handle ) const
	{
		const auto* info = FindPoolInfo( handle );
		RED_MEMORY_ASSERT( info != nullptr, "Unable to find pool info." );
		return info->budget;
	}

	const char* PoolRegistry::GetPoolName( PoolHandle handle ) const
	{
		const auto* info = FindPoolInfo( handle );
		RED_MEMORY_ASSERT( info != nullptr, "Unable to find pool info." );
		return info->name;
	}


	void PoolRegistry::DisableContributeToParentMetrics( PoolHandle handle )
	{
		auto* info = FindPoolInfo( handle );
		RED_MEMORY_ASSERT( info != nullptr, "Unable to find pool info." );
		info->contributeToParentMetrics = false;
	}

	Bool PoolRegistry::GetContributeToParentMetrics( PoolHandle handle ) const
	{
		auto* info = FindPoolInfo( handle );
		RED_MEMORY_ASSERT( info != nullptr, "Unable to find pool info." );
		return info->contributeToParentMetrics;
	}

	void PoolRegistry::SetMirroredPool( PoolHandle handle )
	{
		auto* info = FindPoolInfo( handle );
		RED_MEMORY_ASSERT( info != nullptr, "Unable to find pool info." );
		info->isMirrored = true;
	}

	bool PoolRegistry::IsPoolRegistered( PoolHandle handle ) const
	{
		RED_SCOPE_SHARED_LOCK( m_nodesLock );
		return FindPoolInfo( handle ) != nullptr;
	}

	u32 PoolRegistry::GetPoolCount() const
	{
		return static_cast< u32 >( std::count_if( m_nodes.Begin(), m_nodes.End(), []( const PoolInfo& info ) { return info.handle != c_poolNodeInvalid; } ) );
	}

	u64 PoolRegistry::GetTotalBytesAllocated() const
	{
		u64 bytes = 0;
		for( auto iter = m_nodes.Begin(), end = m_nodes.End(); iter != end; ++iter )
		{
			if ( !iter->isMirrored )
			{
				const PoolStorage* storage = iter->storage;
				if ( storage )
				{
					bytes += storage->bytesAllocated;
				}
			}
		}

		return bytes;
	}

	i64 PoolRegistry::GetTotalBytesAllocated( PoolHandle handle, Bool includeChildren ) const
	{
		const auto* info = FindPoolInfo( handle );
		RED_MEMORY_ASSERT( info != nullptr, "Memory pool does not exist." );

		i64 bytes = info->storage->bytesAllocated;

		if ( !includeChildren )
		{
			return bytes;
		}

		const PoolInfo* currentChild = info->child;
		while( currentChild )
		{
			if ( currentChild->contributeToParentMetrics )
			{
				bytes += GetTotalBytesAllocated( currentChild->handle );
			}
			currentChild = currentChild->sibling;
		}

		return bytes;
	}

	void PoolRegistry::SetupPoolInfo( PoolHandle handle, const PoolParameter& param )
	{
		RED_MEMORY_ASSERT( param.name != nullptr, "Memory pool does not have name." );

		PoolInfo* info = AcquirePoolInfo( handle );
		PoolInfo* parent = nullptr;

		if( info->storage )
		{
			// do nothing if already registered
			return;
		}
		else
		{
			info->budget = param.budget;
			info->storage = param.storage;
			info->contributeToParentMetrics = true;
			info->isMirrored = false;

			const char* poolName = TrimNamespacesFromPoolName( param.name );
			Memcpy( info->name, poolName, std::min( c_poolNameMaxSize - 1, static_cast< u32 >( Strlen( poolName ) ) ) );

			if( param.parentHandle != c_poolNodeInvalid )
			{
				parent = AcquirePoolInfo( param.parentHandle );
				PoolInfo* lastChild = parent->child;

				if( lastChild )
				{
					while( lastChild->sibling )
					{
						lastChild = lastChild->sibling;
					}
					lastChild->sibling = info;
				}
				else
				{
					parent->child = info;
				}
			}

#if defined( RED_USE_ORBIS_MEMORY_ANALYZER )
			RegisterPoolInMemoryAnalyzer( info, parent );
#endif
		}
	}

	void PoolRegistry::SetupPoolInfo( PoolHandle handle, const char* name, u64 budget )
	{
		auto* info = AcquirePoolInfo( handle );
		info->budget = budget;
		Memcpy( info->name, name, std::min( c_poolNameMaxSize - 1, static_cast< u32 >( Strlen( name ) ) ) );
	}

	void PoolRegistry::WritePoolReportToLog( const MetricsRegistry& registry ) const
	{
#ifdef RED_MEMORY_ENABLE_REPORT

		PoolMetrics metrics;
		Memzero( &metrics, sizeof( metrics ) );
		ComputePoolMetrics( registry, metrics );

		const char* headerPattern = "Memory: | %-41s | %12s | %12s | %12s | %12s | %12s | %12s |";

		RED_MEMORY_LOG( "Memory: Pool Informations" );
		RED_MEMORY_LOG( "Memory: ========================================================================================================================" );
		RED_MEMORY_LOG( headerPattern, "Pool", "Incl. KB", "Excl. KB", "Budget", "Incl. Blocks", "Excl. Blocks", "Excl. Max KB" );
		RED_MEMORY_LOG( "Memory: ========================================================================================================================" );
		WritePoolMetricsToLog( PoolRoot::GetHandle(), metrics, 0 );
		RED_MEMORY_LOG( "Memory: ========================================================================================================================" );
#endif
		RED_UNUSED( registry );
	}

	void PoolRegistry::WritePoolReportToJson( FILE* file, const MetricsRegistry& registry ) const
	{
#ifdef RED_MEMORY_ENABLE_REPORT

		PoolMetrics metrics;
		Memzero( &metrics, sizeof( metrics ) );
		ComputePoolMetrics( registry, metrics );

		WritePoolMetricsToJson( file, nullptr, PoolRoot::GetHandle(), metrics, 0 );
#endif
		RED_UNUSED( file );
		RED_UNUSED( registry );
	}

	void PoolRegistry::WriteBoundAllocatorReportsToLog( const MetricsRegistry& registry )
	{
#ifdef RED_MEMORY_ENABLE_REPORT

		RED_MEMORY_LOG( "Memory: Bound Allocator Informations" );
		RED_MEMORY_LOG( "Memory: ========================================================================================================================" );

		m_memoryStream.ResetMarkers();
		PoolInfosWithUniqueAllocator serializedPoolInfosWithUniqueAllocator;
		Memzero( &serializedPoolInfosWithUniqueAllocator, sizeof( serializedPoolInfosWithUniqueAllocator ) );
		u32 currentSerializedPoolInfoIndex = 0;
		GatherBoundAllocatorMetrics( PoolRoot::GetHandle(), m_serializer, m_deserializer, nullptr, serializedPoolInfosWithUniqueAllocator, currentSerializedPoolInfoIndex );
		WriteBoundAllocatorMetricsToLog();

		RED_MEMORY_LOG( "Memory: ========================================================================================================================" );

#endif
		RED_UNUSED( registry );
	}

	void PoolRegistry::WriteBoundAllocatorReportsToJson( const MetricsRegistry& registry )
	{
#ifdef RED_MEMORY_ENABLE_REPORT
		m_memoryStream.ResetMarkers();
		PoolInfosWithUniqueAllocator serializedPoolInfosWithUniqueAllocator;
		Memzero( &serializedPoolInfosWithUniqueAllocator, sizeof( serializedPoolInfosWithUniqueAllocator ) );
		u32 currentSerializedPoolInfoIndex = 0;
		GatherBoundAllocatorMetrics( PoolRoot::GetHandle(), m_serializer, m_deserializer, nullptr, serializedPoolInfosWithUniqueAllocator, currentSerializedPoolInfoIndex );
		WriteBoundAllocatorMetricsToJson();
#endif
		RED_UNUSED( registry );
	}

	void PoolRegistry::ComputePoolMetrics( const MetricsRegistry& registry, PoolMetrics& metrics ) const
	{
		const THash32 root = PoolRoot::GetHandle();
		ComputePoolMetric( root, registry, metrics );
	}

	PoolRegistry::PoolMetric PoolRegistry::ComputePoolMetric( PoolHandle handle, const MetricsRegistry& registry, PoolMetrics& metrics ) const
	{
		const auto* poolInfo = FindPoolInfo( handle );
		RED_MEMORY_ASSERT( poolInfo != nullptr, "Memory pool (handle=%lu) was not registered.", handle );
		RED_MEMORY_ASSERT( poolInfo->storage != nullptr, "Pool storage (handle=%lu) does not exist.", handle );
		const auto poolHandle = poolInfo->storage->handle;
		auto* value = AcquirePoolMetric( handle, metrics );

		const auto* currentChild = poolInfo->child;
		while( currentChild )
		{
			PoolMetric result = ComputePoolMetric( currentChild->handle, registry, metrics );
			if ( currentChild->contributeToParentMetrics )
			{
				value->inclusiveBytesAllocated += result.inclusiveBytesAllocated;
				value->inclusiveAllocationCount += result.inclusiveAllocationCount;
			}
			currentChild = currentChild->sibling;
		}

#ifdef RED_CONFIGURATION_FINAL
		value->exclusiveBytesAllocated = GetTotalBytesAllocated( poolHandle, false );
#else
		value->exclusiveBytesAllocated = registry.GetPoolTotalBytesAllocated( poolHandle );
#endif

		value->exclusiveAllocationCount = registry.GetPoolAllocationCount( poolHandle );

		value->inclusiveBytesAllocated += value->exclusiveBytesAllocated;
		value->inclusiveAllocationCount += value->exclusiveAllocationCount;

		return *value;
	}

	void PoolRegistry::WritePoolMetricsToLog( PoolHandle handle, const PoolMetrics& metrics, u32 ident ) const
	{
#ifdef RED_MEMORY_ENABLE_REPORT
		const auto* info = FindPoolInfo( handle );
		RED_MEMORY_ASSERT( info != nullptr, "Memory pool does not exist." );

		const auto* poolMetric = FindPoolMetric( handle, metrics );
		RED_MEMORY_ASSERT( poolMetric != nullptr, "Memory pool metric does not exist." );

		static const u32 c_nameWidth = 41;
		static const u32 c_identWidth = 2;

		const u32 identWidth = Min( ident * c_identWidth, c_nameWidth );
		const u32 nameWidth = c_nameWidth - identWidth;

		char poolName[ c_nameWidth + 1 ] = { 0 };
		red::Strcpy( poolName, info->name, nameWidth + 1 );

		const float inclusiveKBytes = static_cast< i64 >( poolMetric->inclusiveBytesAllocated ) / 1024.0f;
		const float exclusiveKBytes = static_cast< i64 >( poolMetric->exclusiveBytesAllocated ) / 1024.0f;
		const float maxBytes = static_cast< i64 >( info->storage->maxBytesAllocated ) / 1024.0f;

		const float budget = info->budget / 1024.0f;

		const u32 inclusiveAllocCount = poolMetric->inclusiveAllocationCount;
		const u32 exclusiveAllocCount = poolMetric->exclusiveAllocationCount;

		// Log information only for pools that allocated memory.
		if( inclusiveKBytes != 0 || exclusiveKBytes != 0 || inclusiveAllocCount != 0 || exclusiveAllocCount != 0 || maxBytes != 0 )
		{
			RED_MEMORY_LOG( "Memory: |%*c%-*.*s | %12.2f | %12.2f | %12.2f | %12d | %12d | %12.2f |", 1 + identWidth, ' ', nameWidth, nameWidth, poolName, inclusiveKBytes, exclusiveKBytes, budget, inclusiveAllocCount, exclusiveAllocCount, maxBytes );

			const PoolInfo* child = info->child;
			if( child )
			{
				WritePoolMetricsToLog( child->handle, metrics, ident + 1 );
			}
		}

		const PoolInfo* sibling = info->sibling;
		if( sibling )
		{
			WritePoolMetricsToLog( sibling->handle, metrics, ident );
		}
#endif
		RED_UNUSED( handle );
		RED_UNUSED( metrics );
		RED_UNUSED( ident );
	}

	void PoolRegistry::WritePoolMetricsToJson( FILE* file, const PoolInfo* parentInfo, PoolHandle handle, const PoolMetrics & metrics, u32 ident ) const
	{
#ifdef RED_MEMORY_ENABLE_REPORT
		const auto* info = FindPoolInfo( handle );
		RED_MEMORY_ASSERT( info != nullptr, "Memory pool does not exist." );

		const auto* poolMetric = FindPoolMetric( handle, metrics );
		RED_MEMORY_ASSERT( poolMetric != nullptr, "Memory pool metric does not exist." );

		const float inclusiveKBytes = static_cast< i64 >( poolMetric->inclusiveBytesAllocated ) / 1024.0f;
		const float exclusiveKBytes = static_cast< i64 >( poolMetric->exclusiveBytesAllocated ) / 1024.0f;

		const float budget = info->budget / 1024.0f;

#ifdef RED_MEMORY_ENABLE_EXTENDED_METRICS
		const u32 inclusiveAllocCount = poolMetric->inclusiveAllocationCount;
		const u32 exclusiveAllocCount = poolMetric->exclusiveAllocationCount;
#endif

		if ( parentInfo )
		{

			std::fprintf( file,
				"{\"parent\":\"%s\",\"name\":\"%s\",\"inclusive_kb\":%.2f,\"exclusive_kb\":%.2f,\"budget\":%.2f"
#ifdef RED_MEMORY_ENABLE_EXTENDED_METRICS
				",\"inclusive_alloc_count\":%d,\"exclusive_alloc_count\":%d}",
#else
				"}",
#endif
				parentInfo->name,
				info->name,
				inclusiveKBytes,
				exclusiveKBytes,
				budget
#ifdef RED_MEMORY_ENABLE_EXTENDED_METRICS
				,
				inclusiveAllocCount,
				exclusiveAllocCount
#endif
			);
		}
		else
		{
			std::fprintf(file,
				"{\"name\":\"%s\",\"inclusive_kb\":%.2f,\"exclusive_kb\":%.2f,\"budget\":%.2f"
#ifdef RED_MEMORY_ENABLE_EXTENDED_METRICS
				",\"inclusive_alloc_count\":%d,\"exclusive_alloc_count\":%d}",
#else
				"}",
#endif
				info->name,
				inclusiveKBytes,
				exclusiveKBytes,
				budget
#ifdef RED_MEMORY_ENABLE_EXTENDED_METRICS
				,
				inclusiveAllocCount,
				exclusiveAllocCount
#endif
			);
		}

		const PoolInfo* child = info->child;
		if( child )
		{
			std::fprintf( file, "," );
			WritePoolMetricsToJson( file, info, child->handle, metrics, ident + 1 );
		}

		const PoolInfo* sibling = info->sibling;
		if( sibling )
		{
			std::fprintf( file, "," );
			WritePoolMetricsToJson( file, parentInfo, sibling->handle, metrics, ident );
		}
#endif
		RED_UNUSED( file );
		RED_UNUSED( parentInfo );
		RED_UNUSED( handle );
		RED_UNUSED( metrics );
		RED_UNUSED( ident );
	}

	void PoolRegistry::GatherBoundAllocatorMetrics( PoolHandle handle, Serializer& serializer, Deserializer& deserializer, const char* poolName, PoolInfosWithUniqueAllocator& poolInfosWithUniqueAllocator, u32& currentPoolInfoWithUniqueAllocatorIndex ) const
	{
#ifdef RED_MEMORY_ENABLE_REPORT
		const auto* info = FindPoolInfo( handle );
		RED_MEMORY_ASSERT( info != nullptr, "Memory pool does not exist." );
		RED_MEMORY_ASSERT( info->storage != nullptr, "Pool storage does not exist." );

		if( poolName && !std::strncmp( poolName, info->name, c_poolNameMaxSize ) )
		{
			SerializeAllocatorMetrics( handle, serializer, deserializer, info, poolInfosWithUniqueAllocator, currentPoolInfoWithUniqueAllocatorIndex );
			return;
		}
		else if( !poolName )
		{
			SerializeAllocatorMetrics( handle, serializer, deserializer, info, poolInfosWithUniqueAllocator, currentPoolInfoWithUniqueAllocatorIndex );
		}

		const PoolInfo* child = info->child;
		if( child )
		{
			GatherBoundAllocatorMetrics( child->handle, serializer, deserializer, poolName, poolInfosWithUniqueAllocator, currentPoolInfoWithUniqueAllocatorIndex );
		}

		const PoolInfo* sibling = info->sibling;
		if( sibling )
		{
			GatherBoundAllocatorMetrics( sibling->handle, serializer, deserializer, poolName, poolInfosWithUniqueAllocator, currentPoolInfoWithUniqueAllocatorIndex );
		}

#else

		RED_UNUSED( handle );
		RED_UNUSED( serializer );
		RED_UNUSED( deserializer );
		RED_UNUSED( poolName );
		RED_UNUSED( poolInfosWithUniqueAllocator );
		RED_UNUSED( currentPoolInfoWithUniqueAllocatorIndex );
#endif
	}

	void PoolRegistry::GatherPoolMetrics( PoolHandle handle, const char* parentPoolName, Serializer& serializer )
	{
		const auto* info = FindPoolInfo( handle );
		RED_MEMORY_ASSERT( info != nullptr, "Memory pool does not exist." );
		RED_MEMORY_ASSERT( info->storage != nullptr, "Pool storage does not exist." );

		char parentPoolNameBuffer[ c_poolNameMaxSize ] = {'\0'};
		if( parentPoolName )
		{
			Strcpy( parentPoolNameBuffer, parentPoolName, c_poolNameMaxSize );
		}

		SerializePoolMetrics( info, parentPoolNameBuffer, serializer );

		const auto* child = info->child;
		if( child )
		{
			GatherPoolMetrics( child->handle, info->name, serializer );
		}

		const auto* sibling = info->sibling;
		if( sibling )
		{
			GatherPoolMetrics( sibling->handle, parentPoolNameBuffer, serializer );
		}
	}

	/*
	Serialized allocator metrics binary format:
	---------------------------------------------------------------------------------------------------------------------------
	| position | appearance |             name             |    type    | comment                                             |
	---------------------------------------------------------------------------------------------------------------------------
	|    1     |  required  |          pool handle         |     u32    | 0 if it is standalone allocator                     |
	---------------------------------------------------------------------------------------------------------------------------
	|    2     |  optional  |            pool name         |  char [32] | does not exist if pool handle is 0                  |
	---------------------------------------------------------------------------------------------------------------------------
	|    3     |  optional  |          pool budget         |     u64    | does not exist if pool handle is 0                  |
	---------------------------------------------------------------------------------------------------------------------------
	|    4     |  optional  |        committed memory      |     u64    | does not exist if pool handle is 0                  |
	---------------------------------------------------------------------------------------------------------------------------
	|    5     |  optional  |         allocation count     |     u32    | does not exist if pool handle is 0                  |
	---------------------------------------------------------------------------------------------------------------------------
	|    6     |  required  |       reference pool handle  |     u32    | reference to pool with allocator metrics definition |
	---------------------------------------------------------------------------------------------------------------------------
	|    7     |  optional  |        reference pool name   |  char [32] | reference to pool with allocator metrics definition |
	---------------------------------------------------------------------------------------------------------------------------
	|    8     |  required  |        allocator handle      |     u32    |                                                     |
	---------------------------------------------------------------------------------------------------------------------------
	|    9     |  required  |       has allocator metrics  |     u8     | 0 if there is no allocator metrics, 1 otherwise     |
	---------------------------------------------------------------------------------------------------------------------------
	|    10    |  optional  |        allocator name        |  char [64] |                                                     |
	---------------------------------------------------------------------------------------------------------------------------
	|    11    |  optional  |  allocator metrics blob size |     u32    |                                                     |
	---------------------------------------------------------------------------------------------------------------------------
	|    12    |  optional  |    allocator metrics blob    |    void *  |                                                     |
	---------------------------------------------------------------------------------------------------------------------------
	*/
	void PoolRegistry::SerializeAllocatorMetrics( PoolHandle handle, Serializer& serializer, Deserializer& deserializer, const PoolInfo* info, PoolInfosWithUniqueAllocator& poolInfosWithUniqueAllocator, u32& currentPoolInfoWithUniqueAllocatorIndex ) const
	{
		// serialize pool data
		serializer.Serialize( handle );
		serializer.Serialize( info->name, c_poolNameMaxSize );

		serializer.Serialize( info->budget );

		RuntimePoolMetrics poolMetrics;
		GetRuntimePoolMetrics( info->handle, poolMetrics );
		serializer.Serialize( poolMetrics.bytesAllocated );
		serializer.Serialize( poolMetrics.allocationCount );

		const auto* storage = info->storage;
		auto* allocator = DecodeAllocatorPointer( storage->allocatorStorage );

		const auto serializeRequiredAllocatorData = []( Serializer& serializer, ProxyTypeId allocatorId ) {
			// serialize allocator id
			serializer.Serialize( allocatorId );

			// serialize allocator metrics appearance,
			// data does not contain allocator specific metrics
			serializer.Serialize( static_cast< u8 >( 0 ) );
		};

		auto poolInfoWithUniqueAllocatorPtr = std::find_if( poolInfosWithUniqueAllocator.Begin(), poolInfosWithUniqueAllocator.End(), FindPoolInfoWithUniqueAllocatorComparator( allocator ) );
		if( poolInfoWithUniqueAllocatorPtr != poolInfosWithUniqueAllocator.End() )
		{
			// allocator is shared with other pools,
			// serialize retrieved pool handle as reference to pool with allocator metrics
			const auto* poolInfo = *poolInfoWithUniqueAllocatorPtr;
			serializer.Serialize( poolInfo->handle );
			serializer.Serialize( poolInfo->name, c_poolNameMaxSize );

			serializeRequiredAllocatorData( serializer, storage->allocatorId );
		}
		else
		{
			RED_MEMORY_ASSERT( currentPoolInfoWithUniqueAllocatorIndex < c_poolNameMaxSize, "Out of pool references boundary" );
			poolInfosWithUniqueAllocator[ currentPoolInfoWithUniqueAllocatorIndex++ ] = info;

			// serialize own pool handle as reference to pool with allocator metrics
			serializer.Serialize( handle );

			const u32 dataAvailableToReadBeforeSerializingMetrics = deserializer.DataAvailableToRead();

			// serialize pool metrics
			m_allocatorMetricsProcessorRegistry.SerializeMetrics( handle, storage->allocatorId, allocator, serializer );

			if( dataAvailableToReadBeforeSerializingMetrics == deserializer.DataAvailableToRead() )
			{
				serializeRequiredAllocatorData( serializer, storage->allocatorId );
			}
		}
	}

	/*
	Serialized pool metrics binary format:
	---------------------------------------------------------------------------------------------------------------------------
	| position | appearance |             name             |    type    | comment                                             |
	---------------------------------------------------------------------------------------------------------------------------
	|    1     |  required  |            pool name         |  char [32] |                                                     |
	---------------------------------------------------------------------------------------------------------------------------
	|    2     |  required  |       parent pool name       |  char [32] | empty if pool does not have parent                  |
	---------------------------------------------------------------------------------------------------------------------------
	|    3     |  required  |          pool budget         |     u64    |                                                     |
	---------------------------------------------------------------------------------------------------------------------------
	|    4     |  required  |        committed memory      |     u64    |                                                     |
	---------------------------------------------------------------------------------------------------------------------------
	|    5     |  required  |     committed memory peak    |     u64    |                                                     |
	---------------------------------------------------------------------------------------------------------------------------
	|    6     |  required  |         allocation count     |     u32    |                                                     |
	---------------------------------------------------------------------------------------------------------------------------
	*/
	void PoolRegistry::SerializePoolMetrics( const PoolInfo* info, const char ( &parentPoolName )[ c_poolNameMaxSize ], Serializer& serializer )
	{
		serializer.Serialize( info->name, c_poolNameMaxSize );
		serializer.Serialize( parentPoolName, c_poolNameMaxSize );
		serializer.Serialize( info->budget );

		RuntimePoolMetrics poolMetrics;
		GetRuntimePoolMetrics( info->handle, poolMetrics );
		serializer.Serialize( poolMetrics.bytesAllocated );
		serializer.Serialize( poolMetrics.bytesAllocatedPeak );
		serializer.Serialize( poolMetrics.allocationCount );
	}

	void PoolRegistry::DeserializeAllocatorMetrics( PoolRegistry::AllocatorMetrics& allocatorMetrics )
	{
		m_deserializer.Deserialize( allocatorMetrics.poolHandle );
		if( allocatorMetrics.poolHandle != 0 )
		{
			u32 dataRead{0};
			m_deserializer.Deserialize( allocatorMetrics.poolName, c_poolNameMaxSize, dataRead );
			RED_MEMORY_ASSERT( dataRead == c_poolNameMaxSize, "Inconsistent memory stream data." );
			RED_UNUSED( dataRead );

			m_deserializer.Deserialize( allocatorMetrics.poolBudget );
			m_deserializer.Deserialize( allocatorMetrics.allocatedMemory );
			m_deserializer.Deserialize( allocatorMetrics.allocationCount );
		}

		m_deserializer.Deserialize( allocatorMetrics.poolHandleReference );
		if( allocatorMetrics.poolHandle != allocatorMetrics.poolHandleReference )
		{
			RED_MEMORY_ASSERT( allocatorMetrics.poolHandle != 0, "Invalid memory stream data." );
			RED_MEMORY_ASSERT( allocatorMetrics.poolHandleReference != 0, "Invalid memory stream data." );

			u32 dataRead{0};
			m_deserializer.Deserialize( allocatorMetrics.poolNameReference, c_poolNameMaxSize, dataRead );
			RED_MEMORY_ASSERT( dataRead == c_poolNameMaxSize, "Inconsistent memory stream data." );
			RED_UNUSED( dataRead );
		}

		m_deserializer.Deserialize( allocatorMetrics.allocatorId );
		RED_MEMORY_ASSERT( allocatorMetrics.allocatorId != 0, "Inconsistent memory stream data." );

		m_deserializer.Deserialize( allocatorMetrics.hasAllocatorSpecificMetrics );
		if( allocatorMetrics.hasAllocatorSpecificMetrics )
		{
			u32 dataRead{0};
			m_deserializer.Deserialize( allocatorMetrics.allocatorName, c_allocatorNameMaxSize, dataRead );
			RED_MEMORY_ASSERT( dataRead == c_allocatorNameMaxSize, "Inconsistent memory stream data." );
		}
	}

	void PoolRegistry::WriteAllocatorMetricsToLog( const PoolRegistry::AllocatorMetrics& allocatorMetrics, Deserializer& deserializer )
	{
#ifdef RED_MEMORY_ENABLE_REPORT

		if( !allocatorMetrics.hasAllocatorSpecificMetrics )
		{
			return;
		}

		RED_MEMORY_LOG( "Memory: *********************************************************************" );
		RED_MEMORY_LOG( "Memory: \tPool: %s", allocatorMetrics.poolName );

		if( allocatorMetrics.poolHandle != allocatorMetrics.poolHandleReference )
		{
			RED_MEMORY_LOG( "Memory: \tAllocator metrics reference: %s", allocatorMetrics.poolNameReference );
			return;
		}

		if( allocatorMetrics.hasAllocatorSpecificMetrics )
		{
			RED_MEMORY_LOG( "Memory: \tAllocator: %s", allocatorMetrics.allocatorName );

			m_allocatorMetricsProcessorRegistry.DeserializeMetrics( allocatorMetrics.poolHandle, allocatorMetrics.allocatorId, deserializer );
		}

		RED_MEMORY_LOG( "Memory: *********************************************************************" );

#else

		RED_UNUSED( allocatorMetrics );
		RED_UNUSED( deserializer );

#endif
	}

	void PoolRegistry::WriteAllocatorMetricsToJson( const AllocatorMetrics & allocatorMetrics, Deserializer & deserializer )
	{
#ifdef RED_MEMORY_ENABLE_REPORT
		std::fprintf( s_oomJsonFile, "{\"pool\":\"%s\",\"allocator\":\"%s\",", allocatorMetrics.poolName, allocatorMetrics.allocatorName );

		CoreAllocatorsMetricsJsonWriter( allocatorMetrics.allocatorId, deserializer );

		std::fprintf( s_oomJsonFile, "}" );
		std::fflush( s_oomJsonFile );
#else
		RED_UNUSED( allocatorMetrics );
		RED_UNUSED( deserializer );
#endif
	}

	void PoolRegistry::WriteBoundAllocatorMetricsToLog()
	{
#ifdef RED_MEMORY_ENABLE_REPORT
		while( m_deserializer.DataAvailableToRead() )
		{
			AllocatorMetrics metrics;
			DeserializeAllocatorMetrics( metrics );
			WriteAllocatorMetricsToLog( metrics, m_deserializer );
		}
#endif
	}

	void PoolRegistry::WriteBoundAllocatorMetricsToJson()
	{
#ifdef RED_MEMORY_ENABLE_REPORT
		bool firstEntry = true;
		bool prependComma = false;

		while( m_deserializer.DataAvailableToRead() )
		{
			AllocatorMetrics metrics;
			DeserializeAllocatorMetrics( metrics );

			if( !metrics.hasAllocatorSpecificMetrics )
				continue;

			if( metrics.poolHandle != metrics.poolHandleReference )
				continue;

			if( prependComma && !firstEntry )
			{
				std::fprintf( s_oomJsonFile, "," );
				prependComma = false;
			}

			WriteAllocatorMetricsToJson( metrics, m_deserializer );

			prependComma = true;
			firstEntry = false;
		}
#endif
	}

	void PoolRegistry::WritePools( Serializer& serializer )
	{
		u32 poolCount = GetPoolCount();
		serializer.Serialize( poolCount );

		for( u32 index = 0; index != poolCount; ++index )
		{
			auto& poolInfo = m_nodes[ index ];
			serializer.Serialize( poolInfo.handle );
			const char* name = poolInfo.name;
			name = ( name != nullptr ) ? name : c_poolUnkownName;
			const u32 length = static_cast< u32 >( Strlen( name ) );

			serializer.Serialize( length );
			serializer.Serialize( name, length );
		}
	}

	void PoolRegistry::SerializeAllocatorMetrics( Serializer& serializer, Deserializer& deserializer, const char* poolName )
	{
		PoolInfosWithUniqueAllocator serializedPoolInfosWithUniqueAllocator;
		Memzero( &serializedPoolInfosWithUniqueAllocator, sizeof( serializedPoolInfosWithUniqueAllocator ) );
		u32 currentSerializedPoolInfoIndex = 0;
		GatherBoundAllocatorMetrics( PoolRoot::GetHandle(), serializer, deserializer, poolName, serializedPoolInfosWithUniqueAllocator, currentSerializedPoolInfoIndex );
	}

	void PoolRegistry::SerializePoolMetrics( Serializer& serializer )
	{
		GatherPoolMetrics( PoolRoot::GetHandle(), "PoolRoot", serializer );
	}

	void PoolRegistry::VisitPoolInfos( const PoolInfoVisitor& visitor, const PoolHandle poolHandle ) const
	{
		RED_SCOPE_SHARED_LOCK( m_nodesLock );
		VisitPoolInfos( visitor, poolHandle, nullptr );
	}

	void PoolRegistry::VisitPoolInfos( const PoolInfoVisitor& visitor, const PoolHandle poolHandle, const PoolInfo* parent ) const
	{
		RED_MEMORY_ASSERT( poolHandle != c_poolNodeInvalid, "Memory pool does not exist." );
		const auto* info = FindPoolInfo( poolHandle );
		RED_MEMORY_ASSERT( info != nullptr, "Memory pool does not exist." );

		visitor( info, parent );

		const auto* child = info->child;
		if( child )
		{
			VisitPoolInfos( visitor, child->handle, info );
		}

		const auto* sibling = info->sibling;
		if( sibling )
		{
			VisitPoolInfos( visitor, sibling->handle, parent );
		}
	}

	PoolInfo* PoolRegistry::AcquirePoolInfo( PoolHandle handle )
	{
		auto it = std::find_if( m_nodes.Begin(), m_nodes.End(), PoolRegistry::AcquireComparator< PoolInfo >( handle ) );
		RED_MEMORY_ASSERT( it != m_nodes.End(), "No more pool info can be created." );
		if( it->handle == c_poolNodeInvalid )
			it->handle = handle;
		return &( *it );
	}

	const PoolInfo* PoolRegistry::FindPoolInfo( PoolHandle handle ) const
	{
		auto it = std::find_if( m_nodes.Begin(), m_nodes.End(), PoolRegistry::FindComparator< PoolInfo >( handle ) );
		return it != m_nodes.End() ? &( *it ) : nullptr;
	}

	PoolInfo* PoolRegistry::FindPoolInfo( PoolHandle handle )
	{
		auto it = std::find_if( m_nodes.Begin(), m_nodes.End(), PoolRegistry::FindComparator< PoolInfo >( handle ) );
		return it != m_nodes.End() ? &( *it ) : nullptr;
	}

	PoolRegistry::PoolMetric* PoolRegistry::AcquirePoolMetric( PoolHandle handle, PoolMetrics& metrics ) const
	{
		auto it = std::find_if( metrics.Begin(), metrics.End(), AcquireComparator< PoolMetric >( handle ) );
		RED_MEMORY_ASSERT( it != metrics.End(), "No more pool metric can be created." );
		if( it->handle == c_poolNodeInvalid )
		{
			it->handle = handle;
		}

		return &( *it );
	}

	const PoolRegistry::PoolMetric* PoolRegistry::FindPoolMetric( PoolHandle handle, const PoolMetrics& metrics ) const
	{
		auto it = std::find_if( metrics.Begin(), metrics.End(), FindComparator< PoolMetric >( handle ) );
		return it != metrics.End() ? &( *it ) : nullptr;
	}

	void PoolRegistry::ValidateAllPoolBudget()
	{
		RED_SCOPE_LOCK( m_nodesLock );

		ComputePoolChildrenBudget();

		RED_LOG( "Memory: *********************************************************************" );
		RED_LOG( "Memory: Validating all memory budget..." );

		bool errors = false;

		for( auto iter = m_nodes.Begin(), end = m_nodes.End(); iter != end; ++iter )
		{
			if( iter->handle != c_poolNodeInvalid )
			{
				const u64 totalBudget = iter->budget;
				const u64 childrenBudget = iter->childrenBudget;

				if( totalBudget == 0 )
				{
					RED_LOG_ERROR( "Memory: %hs budget is 0.", iter->name );
					errors = true;
				}

				if( childrenBudget > totalBudget )
				{
					RED_LOG_ERROR( "Memory: %hs budget is smaller than sum of children budget. [%.2f KB < %.2f KB]", iter->name, totalBudget / 1024.0f, childrenBudget / 1024.0f );
					errors = true;
				}
			}
		}

		if( !errors )
		{
			RED_LOG( "Memory: Success!" );
		}

		RED_LOG( "Memory: *********************************************************************" );

#ifdef RED_MEMORY_ENABLE_POOL_INITALIZATION_VALIDATION
		RED_LOG( "Memory: All pools with budgets:" );
		for( auto iter = m_nodes.Begin(), end = m_nodes.End(); iter != end; ++iter )
		{
			if( iter->handle != c_poolNodeInvalid )
			{
				RED_LOG( "Memory: %hs budget [%.2f KB], children budget [%.2f KB].", iter->name, iter->budget / 1024.0f, iter->childrenBudget / 1024.0f );
			}
		}
		RED_LOG( "Memory: *********************************************************************" );

		g_validatePoolInitialization = true;
#endif

		RED_LOG_FLUSH();
	}

	void PoolRegistry::ComputePoolChildrenBudget()
	{
		ComputePoolChildrenBudget( PoolRoot::GetHandle(), nullptr );
	}

	void PoolRegistry::ComputePoolChildrenBudget( PoolHandle poolHandle, PoolInfo* parent )
	{
		PoolInfo* pool = FindPoolInfo( poolHandle );
		RED_MEMORY_ASSERT( pool != nullptr, "Memory pool does not exist." );

		if( parent )
		{
			parent->childrenBudget += pool->budget;
		}

		PoolInfo* childPool = pool->child;
		if( childPool )
		{
			ComputePoolChildrenBudget( childPool->handle, pool );
		}

		PoolInfo* siblingPool = pool->sibling;
		if( siblingPool )
		{
			ComputePoolChildrenBudget( siblingPool->handle, parent );
		}
	}

	PoolRegistry::FindPoolInfoWithUniqueAllocatorComparator::FindPoolInfoWithUniqueAllocatorComparator( const void* allocator )
		: m_allocator( allocator )
	{}

	bool PoolRegistry::FindPoolInfoWithUniqueAllocatorComparator::operator()( const PoolInfo* poolReference ) const
	{
		if( !poolReference )
		{
			return false;
		}

		const auto* storage = poolReference->storage;
		if( !storage )
		{
			return false;
		}

		return DecodeAllocatorPointer( storage->allocatorStorage ) == m_allocator;
	}
}
}
