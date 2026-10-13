/**
 * Copyright (c) 2016 CD Projekt Red. All Rights Reserved.
 */

#ifndef _RED_MEMORY_INCLUDE_POOL_UTILS_H_
#define _RED_MEMORY_INCLUDE_POOL_UTILS_H_

#include "redMemoryApi.h"
#include "redMemoryInternal.h"
#include "poolTypes.h"
#include "allocatorMetricsSerializer.h"
#include "allocatorMetricsLogger.h"
#include "proxyTypeId.h"
#include "../../redSystem/include/hash.h"

namespace red
{
namespace memory
{
	const u32 c_poolNameMaxSize = 32;

	class Serializer;
	class Deserializer;

	struct PoolInfo
	{
		u64 budget;
		u64 childrenBudget;
		PoolStorage* storage;
		PoolInfo* child;
		PoolInfo* sibling;
		char name[c_poolNameMaxSize];
		PoolHandle handle;
		Bool contributeToParentMetrics;
		Bool isMirrored;
	};

	struct RootPoolsSetup
	{
		Uint32 frameAllocatorSizeOverride = 0;
	};

	//////////////////////////////////////////////////////////////////////////
	// Initialize all the root pool. 
	// This function do need to be called, but it will set default budget and will add them to memory report. 
	RED_MEMORY_API void InitializeRootPools( const RootPoolsSetup& setup = RootPoolsSetup() );

	// Initialize and Register PoolType. Optional only if allocator is DefaultAllocator.
	// For convenience, use RED_INITIALIZE_MEMORY_POOL macro.
	template< typename PoolType >
	void InitializePool( const PoolParameter & param, typename PoolType::AllocatorType & allocator );

	// Register Pool in metrics system. Called by InitializePool. No need to do both
	RED_MEMORY_API void RegisterPool( PoolHandle handle, const PoolParameter & param );

	// Set or reset Pool budget.
	RED_MEMORY_API void SetPoolBudget( PoolHandle handle, const char* name, u64 budget );

	// Register Pool Allocator metrics serializer and deserializer.
	RED_MEMORY_API void RegisterAllocatorMetricsProcessor(
		PoolHandle poolHandle,
		ProxyTypeId allocatorId,
		void ( *metricsSerializer )( void * allocator, red::memory::Serializer & serializer ),
		void ( *metricsDeserializer )( ProxyTypeId proxyId, red::memory::Deserializer & deserializer ) );

	// Check if the given pool should use debug allocator.
	RED_MEMORY_API Bool ShouldPoolUseDebugAllocator( PoolHandle handle );

	// Makes allocator storage for the given pool
	RED_MEMORY_API u64 MakeAllocatorStorage( void* allocator, const PoolHandle handle );

	// Check if given pool has debug allocator enabled
	RED_MEMORY_API Bool IsDebugAllocatorEnabled( const PoolStorage& storage );

	//////////////////////////////////////////////////////////////////////////
	// Unit tests only API!!!

	// Force debug allocator on given pool, must be called before pool is initialized.
	RED_MEMORY_API Bool UnitTestForceDebugAllocatorOnPool( PoolHandle handle );

	// Force debug allocator on given pool, must be called before pools are initialized.
	RED_MEMORY_API void UnitTestForceDebugAllocatorOnAllPools();

	// Reset debug pool allocator settings after unit test is finished.
	RED_MEMORY_API void UnitTestResetDebugPoolAllocatorSettings();


	//////////////////////////////////////////////////////////////////////////

	template< typename Pool >
	u64 GetPoolBudget();

	RED_MEMORY_API u64 GetPoolBudget( PoolHandle handle );

	// Check if each pool budget fits correctly inside hierarchy of pools. Log errors if some budget are incorrect.
	RED_MEMORY_API void ValidateAllPoolBudget();

	template< typename Pool >
	const char* GetPoolName();
	
	RED_MEMORY_API const char * GetPoolName( PoolHandle handle );

	bool IsPoolRegistered( PoolHandle handle );

	RED_MEMORY_API u32 GetPoolCount();

	template< typename Pool >
	u64 GetPoolTotalBytesAllocated();

	template< typename Pool >
	void ResetPoolTotalBytesAllocated();

	using PoolInfoVisitor = red::FixedSizeFunction< void(const PoolInfo*, const PoolInfo*) >;
	RED_MEMORY_API void VisitPoolInfos( const red::memory::PoolInfoVisitor& visitor, const PoolHandle poolHandle = PoolRoot::GetHandle() );

	//////////////////////////////////////////////////////////////////////////

	class OOMHandler;

	template< typename PoolType >
	void SetPoolOOMHandler( OOMHandler * oomHandler );

	template< typename PoolType >
	void ForceNoDebugAllocator();

	template< typename PoolType >
	void DisableContributeToParentMetrics();
	RED_MEMORY_API void DisableContributeToParentMetrics( PoolHandle handle );

	template< typename PoolType >
	void SetMirroredPool();
	RED_MEMORY_API void SetMirroredPool( PoolHandle handle );

	//////////////////////////////////////////////////////////////////////////
	// Pool Resolver
	// Resolve at Compile Time the pool bond to a given Type
	// How-to: 
	// PoolResolver< MyObject >::PoolType is the correct Pool type
	// See poolUtils.hpp for implementation details.
	template< typename Type, typename DefaultPoolType = PoolDefault, class Enable = void >
	struct PoolResolver;

namespace internal
{
	template< typename T, Bool = false >
	struct DeleteResolver;

	template< typename PoolType >
	void InitializePool(
		const char* poolName,
		red::memory::PoolStorage* storage,
		Uint64 budget,
		red::memory::PoolHandle handle,
		ProxyTypeId proxyId,
		typename PoolType::AllocatorType& allocator,
		void ( *metricsSerializer )( void * allocator, red::memory::Serializer & serializer ),
		void ( *metricsDeserializer )( ProxyTypeId proxyId, red::memory::Deserializer & deserializer ) );
}
}
}

#define _INTERNAL_RED_POOL_CONTEXT __PoolContext
#define _INTERNAL_RED_POLYMORPHIC_POOL_CONTEXT __PolymorphicPoolContext

#define RED_MEMORY_PROHIBIT_OPERATORS_NEW \
private: \
	void* operator new( std::size_t ) = delete; \
	void* operator new[]( std::size_t ) = delete

//////////////////////////////////////////////////////////////////////////
// Add this helper in a public section of your Object to make operators use the specified pool.
#define RED_USE_MEMORY_POOL( poolName ) \
	RED_MEMORY_PROHIBIT_OPERATORS_NEW; \
public: \
	template< typename, typename, class > friend struct ::red::memory::PoolResolver; \
	template< typename, typename > friend struct ::red::memory::InternalHasPoolContext; \
	template< typename, ::red::Bool > friend struct ::red::memory::internal::DeleteResolver; \
	const ::red::memory::Pool& GetMemoryPool() const { return poolName::GetInstance(); } \
	typedef poolName _INTERNAL_RED_POOL_CONTEXT

#define RED_USE_POLYMORPHIC_MEMORY_POOL( poolName ) \
	RED_MEMORY_PROHIBIT_OPERATORS_NEW; \
public: \
	template< typename, typename, class > friend struct red::memory::PoolResolver; \
	template< typename, typename > friend struct red::memory::InternalHasPoolContext; \
	template< typename, typename > friend struct red::memory::InternalHasPolymorphicPoolContext; \
	template< typename, red::Bool > friend struct red::memory::internal::DeleteResolver; \
	virtual const red::memory::Pool& GetMemoryPool() const { return poolName::GetInstance(); } \
	typedef poolName _INTERNAL_RED_POOL_CONTEXT; \
	typedef poolName _INTERNAL_RED_POLYMORPHIC_POOL_CONTEXT

#define RED_INITIALIZE_MEMORY_POOL_INTERNAL_4( poolType, poolParentType, allocator, budget )	\
	red::memory::internal::InitializePool< poolType >(											\
		#poolType, &red::memory::StaticPoolStorage< poolType >::storage,						\
		budget, poolParentType::GetHandle(), poolType::AllocatorType::TypeId, allocator,		\
		&red::memory::SerializeAllocatorMetrics< typename poolType::AllocatorType >,			\
		&red::memory::CoreAllocatorsMetricsLogger )

#define RED_INITIALIZE_MEMORY_POOL_INTERNAL_5( poolType, poolParentType, allocator, budget, allocatorMetricsLoggerFunc )	\
	red::memory::internal::InitializePool< poolType >(																		\
		#poolType, &red::memory::StaticPoolStorage< poolType >::storage,													\
		budget, poolParentType::GetHandle(), poolType::AllocatorType::TypeId, allocator,									\
		&red::memory::SerializeAllocatorMetrics< typename poolType::AllocatorType >,										\
		allocatorMetricsLoggerFunc )

#define RED_INITIALIZE_MEMORY_POOL( ... ) RED_MEMORY_CONCAT( RED_MEMORY_OVERLOAD( RED_INITIALIZE_MEMORY_POOL_INTERNAL_, __VA_ARGS__ )( __VA_ARGS__ ), RED_MEMORY_EMPTY() )

#define RED_SET_MEMORY_POOL_BUDGET( poolType, budget ) \
	do{  red::memory::SetPoolBudget( red::CalculateHash32( poolType ), poolType, budget ); } while( 0,0 )

#include "poolUtils.hpp"

#endif
