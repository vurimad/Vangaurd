/**
 * Copyright (c) 2015 CD Projekt Red. All Rights Reserved.
 */

#ifndef _RED_MEMORY_POOL_DECLARATION_H_
#define _RED_MEMORY_POOL_DECLARATION_H_

#include "../include/poolTypes.h"

#include "proxy.h"
#include "pool.h"
#include "macroUtils.h"
#include "poolStorage.h"
#include "../include/block.h"
#include "../../redSystem/include/hash.h"

#define _INTERNAL_RED_MEMORY_POOL( poolUniqueName, allocatorType, dllTag )																																									\
	class dllTag poolUniqueName : public red::memory::Pool																																													\
	{																																																										\
	public:																																																									\
		typedef allocatorType AllocatorType;																																																\
		typedef red::memory::PoolStorageProxy< poolUniqueName > ProxyType;																																									\
		RED_MEMORY_INLINE poolUniqueName(){}																																																\
		RED_MEMORY_INLINE ~poolUniqueName(){}																																																\
		RED_MEMORY_DECLARE_PROXY( poolUniqueName, AllocatorType::DefaultAlignmentType::value );																																				\
		RED_MEMORY_INLINE static red::memory::PoolHandle GetHandle() { static constexpr red::THash32 nameHash = red::CalculateHash32( #poolUniqueName ); return nameHash; }																	\
		RED_MEMORY_INLINE static AllocatorType & GetAllocator() { return ProxyType::GetAllocator(); }																																		\
		RED_MEMORY_INLINE static poolUniqueName & GetInstance() { static poolUniqueName pool; return pool; }																																\
	private:																																																								\
		virtual red::memory::Block OnAllocate( red::memory::u32 size ) const override final { return ProxyType::Allocate( size ); }																											\
		virtual red::memory::Block OnAllocateAligned( red::memory::u32 size, red::memory::u32 alignment ) const override final { return ProxyType::AllocateAligned( size, alignment ); }													\
		virtual red::memory::Block OnReallocate( red::memory::Block & block, red::memory::u32 size ) const override final { return ProxyType::Reallocate( block, size ); }																	\
		virtual red::memory::Block OnReallocateAligned( red::memory::Block & block, red::memory::u32 size, red::memory::u32 alignment ) const override final { return ProxyType::ReallocateAligned( block, size, alignment ); }				\
		virtual void OnFree( red::memory::Block & block ) const override final { ProxyType::Free( block ); }																																\
		virtual red::memory::u64 OnGetBlockSize( red::memory::u64 address ) const override final { return ProxyType::GetBlockSize( address ); }																								\
		virtual red::memory::PoolHandle OnGetHandle() const override final { return ProxyType::GetHandle(); }																																\
}

#endif
