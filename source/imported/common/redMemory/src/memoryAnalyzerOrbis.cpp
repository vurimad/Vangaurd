/**
 * Copyright (c) 2018 CD Projekt Red. All Rights Reserved.
 */

#include "build.h"
#include "memoryAnalyzerOrbis.h"
#include "poolConstant.h"
#include "poolStorage.h"
#include "../include/poolUtils.h"
#include "../../redSystem/include/readWriteSpinLock.h"

#if defined( RED_USE_ORBIS_MEMORY_ANALYZER )
#include <mat.h>
#pragma comment(lib, "libSceMat_stub_weak.a")

#endif

namespace red
{
namespace memory
{
namespace
{
#if defined( RED_USE_ORBIS_MEMORY_ANALYZER )

	struct GroupInfo
	{
		PoolHandle handle;
		MatGroup groupId;
	};

	red::Atomic< MatGroup > s_groupId{ 1 };
	GroupInfo s_freeAllocatorsGroupInfo = { c_poolNodeInvalid, 0 };
	const PoolHandle s_poolRootHandle = PoolRoot::GetHandle();
	const char* s_poolRootName = "PoolRoot";

	struct DelayedGroupInfo
	{
		const PoolInfo* pool;
		const PoolInfo* parent;
		MatGroup poolGroupId;
	};

	red::RWSpinLock s_poolGroupsLock;
	SimpleArray< GroupInfo, c_poolMaxCount > s_poolGroups;

	red::RWSpinLock s_delayedPoolGroupsLock;
	SimpleArray< DelayedGroupInfo, c_poolMaxCount > s_delayedPoolGroups;

	struct AcquireGroupInfoComparator
	{
		AcquireGroupInfoComparator( PoolHandle handle )
			: m_handle( handle )
		{}

		bool operator()( const GroupInfo& obj ) const
		{
			return obj.handle == m_handle || obj.handle == c_poolNodeInvalid;
		}

	private:
		const PoolHandle m_handle;
	};

	struct FindGroupInfoComparator
	{
		FindGroupInfoComparator( PoolHandle handle )
			: m_handle( handle )
		{}

		bool operator()( const GroupInfo& obj ) const
		{
			return obj.handle == m_handle;
		}

	private:
		const PoolHandle m_handle;
	};

	GroupInfo* AcquireGroupInfo( PoolHandle handle )
	{
		RED_SCOPE_LOCK( s_poolGroupsLock );
		auto it = std::find_if( s_poolGroups.Begin(), s_poolGroups.End(), AcquireGroupInfoComparator( handle ) );
		RED_MEMORY_ASSERT( it != s_poolGroups.End(), "No more pool group can be created." );
		if ( it->handle == c_poolNodeInvalid )
		{
			it->handle = handle;
		}

		return &( *it );
	}

	const GroupInfo* FindGroupInfo( PoolHandle handle )
	{
		if ( handle == c_poolNodeInvalid )
		{
			return &s_freeAllocatorsGroupInfo;
		}

		{
			RED_SCOPE_SHARED_LOCK( s_poolGroupsLock );
			auto it = std::find_if( s_poolGroups.Begin(), s_poolGroups.End(), FindGroupInfoComparator( handle ) );
			if ( it == s_poolGroups.End() )
			{
				return &s_freeAllocatorsGroupInfo;
			}

			RED_MEMORY_ASSERT( it->handle != c_poolNodeInvalid, "Unregistered pool" );
			return &( *it );
		}
	}

	struct AcquireFreeDelayedGroupInfoComparator
	{
		bool operator()( const DelayedGroupInfo& obj ) const
		{
			return !obj.pool && !obj.parent && !obj.poolGroupId;
		}
	};

	void DelayPoolGroupRegistration( const PoolInfo* pool, const PoolInfo* parent, MatGroup poolGroupId )
	{
		RED_SCOPE_LOCK( s_delayedPoolGroupsLock );

		auto it = std::find_if( s_delayedPoolGroups.Begin(), s_delayedPoolGroups.End(), AcquireFreeDelayedGroupInfoComparator() );
		RED_MEMORY_ASSERT( it != s_delayedPoolGroups.End(), "No more delayed pool group can be created." );

		it->pool = pool;
		it->parent = parent;
		it->poolGroupId = poolGroupId;
	}

	void RegisterDelayedPoolGroups( const PoolInfo* parentPool, MatGroup parentPoolGroupId )
	{
		RED_SCOPE_LOCK( s_delayedPoolGroupsLock );
		for ( auto& delayPool : s_delayedPoolGroups )
		{
			if ( delayPool.parent && delayPool.parent->handle == parentPool->handle )
			{
				sceMatRegisterGroup( delayPool.poolGroupId, delayPool.pool->name, parentPoolGroupId );
				auto* groupInfo = AcquireGroupInfo( delayPool.pool->handle );
				groupInfo->groupId = delayPool.poolGroupId;
				delayPool.pool = nullptr;
				delayPool.parent = nullptr;
				delayPool.poolGroupId = 0;
			}
		}
	}

	void PreAllocationCallback( HookPreParameter & param, void* userData )
	{
		const Block & block = *param.block;
		const u32 & size = *param.size;
	
		if ( block.address && size == 0 )
		{
			sceMatFree( reinterpret_cast< void* >( block.address ) );
		}
		else if ( block.address )
		{
			sceMatReallocBegin( reinterpret_cast< void* >( block.address ), size, 0 );
		}
	}

	void PostAllocationCallback( HookPostParameter & param, void* userData )
	{
		const Block & output = *param.outputBlock;
		
		if ( output.address )
		{
			const Block & input = *param.inputBlock;
			
			if ( input.address )
			{
				sceMatReallocEnd( reinterpret_cast< void* >( output.address ), output.size, 0 /* padding */ );
			}
			else
			{
				const PoolHandle& poolHandle = param.proxy.poolHandle;
				const MatGroup groupId = FindGroupInfo( poolHandle )->groupId;

				sceMatAlloc( reinterpret_cast< void* >( output.address ), // ptr
					output.size, // size
					0, // padding
					groupId ); // group
			}
		}
	}
#endif
}

	MemoryAnalyzerOrbis::MemoryAnalyzerOrbis()
		: m_hookHandler( nullptr )
		, m_hookHandle( 0 )
	{
	#if defined( RED_USE_ORBIS_MEMORY_ANALYZER )
		const int result = sceMatInitialize( SCEMAT_INIT_DEFAULT );
		if ( result != SCE_MAT_OK )
		{
			RED_LOG_ERROR( "Failed to initialize sceMat library, error code=%d", result );
		}
		else
		{
			sceMatRegisterGroup( 0, "Unknown Pool", SCEMAT_GROUP_ROOT );
		}
	#endif
	}

	MemoryAnalyzerOrbis::~MemoryAnalyzerOrbis()
	{
	#if defined( RED_USE_ORBIS_MEMORY_ANALYZER )
		sceMatUninitialize();

		if ( m_hookHandle )
		{
			m_hookHandler->Remove( m_hookHandle );
		}
	#endif
	}

	void MemoryAnalyzerOrbis::Initialize( HookHandler* hookHandler )
	{
		m_hookHandler = hookHandler;
		RED_MEMORY_ASSERT( m_hookHandler != nullptr, "HookHandler cannot be nullptr" );

#if defined( RED_USE_ORBIS_MEMORY_ANALYZER )
		const HookCreationParameter param =
			{
				PreAllocationCallback,
				PostAllocationCallback,
				nullptr,
				HookType::HookType_Memory_Profiler};

		m_hookHandle = m_hookHandler->Create( param );
#endif
	}

	void RegisterPoolInMemoryAnalyzer( const PoolInfo* pool, const PoolInfo* parent )
	{
#if defined( RED_USE_ORBIS_MEMORY_ANALYZER )

		RED_MEMORY_ASSERT( pool, "Pool info does not exist" );

		GroupInfo* groupInfo = AcquireGroupInfo( pool->handle );
		if ( groupInfo->groupId )
		{
			// Pool is already registered. Multiple pool registrations?
			return;
		}

		// Set pool group id
		MatGroup groupId = s_groupId.PostIncrement();

		if ( parent )
		{
			GroupInfo* parentGroupInfo = AcquireGroupInfo( parent->handle );
			if ( !parentGroupInfo->groupId )
			{
				if ( parent->handle == s_poolRootHandle )
				{
					MatGroup parentGroupId = s_groupId.PostIncrement();
					sceMatRegisterGroup( parentGroupId, s_poolRootName, SCEMAT_GROUP_ROOT );
					parentGroupInfo->groupId = parentGroupId;
				}
				else
				{
					// Parent pool wasn't registered yet, so we need to delay current pool registration
					DelayPoolGroupRegistration( pool, parent, groupId );
					return;
				}
			}

			sceMatRegisterGroup( groupId, pool->name, parentGroupInfo->groupId );
		}
		else
		{
			sceMatRegisterGroup( groupId, pool->name, SCEMAT_GROUP_ROOT );
		}

		groupInfo->groupId = groupId;

		// Try to register delayed pools which are children of current pool
		RegisterDelayedPoolGroups( pool, groupId );
#else
		RED_UNUSED( pool );
		RED_UNUSED( parent );
#endif
	}

#if defined( RED_USE_ORBIS_MEMORY_ANALYZER )
	void MemoryAnalyzerFree( const Block& block )
	{
		sceMatFree( reinterpret_cast< void* >( block.address ) );
	}
	
	void MemoryAnalyzerAllocate( const Block& block, const PoolHandle& poolHandle )
	{
		const MatGroup groupId = FindGroupInfo( poolHandle )->groupId;
		sceMatAlloc( reinterpret_cast< void* >( block.address ), block.size, 0, groupId );									 
	}

	void MemoryAnalyzerReallocateBegin( const Block& block )
	{
		sceMatReallocBegin( reinterpret_cast< void* >( block.address ), block.size, 0 );
	}
	
	void MemoryAnalyzerReallocateEnd( const Block& block )
	{
		sceMatReallocEnd( reinterpret_cast< void* >( block.address ), block.size, 0 );
	}
#endif 
}
}

