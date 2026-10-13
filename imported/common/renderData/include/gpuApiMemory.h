/**
* Copyright (c) 2010 CD Projekt Red. All Rights Reserved.
*/
#pragma once

//#include "gpuApiErrorHandling.h"
//#include "gpuApiUtilsApi.h"

#include "../../redMemory/include/gpuAllocator.h"

#define RED_GPU_MEMORY_POOL( poolName, allocator ) \
	RED_MEMORY_POOL( poolName, allocator, RENDER_DATA_API )

// Fake pools and debug allocator don't play well together. With the fake pools, the things we pass to Free are never
// valid pointers, and debug allocator doesn't know anything about that. So just don't do it.
#if !defined( RED_MEMORY_FORCE_DEBUG_ALLOCATOR )
#	define GPUAPI_USE_FAKE_ALLOCATOR
#endif

// #TODO: It could be useful to have at least _some_ sort of breakdown on Xbox Final... PS4 has some, because it uses
// real allocations from specific pools for different coarse types of resources. But Xbox we'll just know how much total
// was used.
#if defined( GPUAPI_USE_FAKE_ALLOCATOR ) // DEBUG - enabled in Final builds to catch OOM source. && ( !defined( RED_CONFIGURATION_FINAL ) || defined( USE_PROFILER ) )
#	define GPUAPI_USE_GPUMIRROR_POOLS
#endif

//#define USE_NEW_SPECIALIZED_GPU_POOLS

#ifdef GPUAPI_USE_FAKE_ALLOCATOR
namespace GpuApi
{
	struct GPUFakeAllocatorMetrics {};

	class GPUFakeAllocator
	{
	public:
		RED_MEMORY_DECLARE_ALLOCATOR( GPUFakeAllocator, GPUFakeAllocatorMetrics, 16 );

		GPUFakeAllocator() { }

		red::memory::Block Allocate( uint32_t size )
		{
			red::memory::Block block = { (red::memory::u64)(size + 1), size };
			m_currentlyAllocated.ExchangeAdd( size );
			return block;
		}

		red::memory::Block AllocateAligned( uint32_t size, uint32_t alignment )
		{
			RED_UNUSED( size );
			RED_MEMORY_ASSERT( alignment <= 16, "Alignment can't be bigger than 16." );
			return Allocate( size );
		}

		red::memory::Block Reallocate( red::memory::Block& block, uint32_t size )
		{
			RED_UNUSED( block );
			RED_UNUSED( size );
			RED_HALT("Reallocate not supported");
			return red::memory::NullBlock();
		}

		red::memory::Block ReallocateAligned( red::memory::Block& block, uint32_t size, uint32_t alignment )
		{
			RED_UNUSED( block );
			RED_UNUSED( size );
			RED_UNUSED( alignment );
			RED_HALT("ReallocateAligned not supported");
			return red::memory::NullBlock();
		}

		void Free( red::memory::Block& block )
		{
			red::memory::i64 blockSize = (red::memory::i64)GetBlockSize( block.address );
			block.size = blockSize;
			RED_MEMORY_ASSERT( m_currentlyAllocated.GetValue() >= blockSize, "Trying to deallocate more than was allocated in GpuMirror pool!" );
			m_currentlyAllocated.ExchangeAdd( -blockSize );
		}

		uint64_t GetBlockSize( uint64_t block ) const
		{
			RED_MEMORY_ASSERT( block > 0, "Invalid block address in GPUFakeAllocator" );
			return (block - 1); // the address is the (size + 1)
		}

	private:
		red::Atomic< Int64 > m_currentlyAllocated;
	};
}


#define GPUAPI_FAKE_ALLOCATE(Pool, size) do {										\
	static_assert(std::is_same<Pool::AllocatorType, GpuApi::GPUFakeAllocator>::value, "GPUAPI_FAKE_ALLOCATE can only be used with a pool backed by GPUFakeAllocator.");	\
	RED_ALLOCATE_WITHOUT_HOOKS(Pool, size, red::memory::HookType::HookType_All);	\
	} while(0)
#define GPUAPI_FAKE_FREE(Pool, size) do {											\
	static_assert(std::is_same<Pool::AllocatorType, GpuApi::GPUFakeAllocator>::value, "GPUAPI_FAKE_FREE can only be used with a pool backed by GPUFakeAllocator.");		\
	void *fakePtr = reinterpret_cast< void * >( size_t( size ) + 1 );							\
	RED_FREE_WITHOUT_HOOKS(Pool, fakePtr, red::memory::HookType::HookType_All);		\
	} while(0)

#endif


namespace GpuApi
{
	// Common memory pools - used in ALL GpuApi-s. If you see that a certain pool is not common anymore - please move it upwards, to it's respective platform.
	RED_GPU_MEMORY_POOL( PoolGpuApi,		red::memory::DefaultAllocator );	// GpuApi root. We don't allocate anything in it directly. It is just a parent to the pools below.
	RED_GPU_MEMORY_POOL( PoolTiny,			red::memory::DefaultAllocator );	// Tiny amounts, miscellaneous stuff. It is child of PoolGpuApi, so it is completely fine to have it.
	RED_GPU_MEMORY_POOL( PoolDeviceData,	red::memory::DefaultAllocator );	// "THE" SDeviceData
	RED_GPU_MEMORY_POOL( PoolCommandLists,	red::memory::DefaultAllocator );
	RED_GPU_MEMORY_POOL( PoolResRefExtra,	red::memory::DefaultAllocator );	// Miscellaneous ResourceRef-related stuff...
	RED_GPU_MEMORY_POOL( PoolQueries,		red::memory::DefaultAllocator );
	RED_GPU_MEMORY_POOL( PoolDebug,			red::memory::DefaultAllocator );
#ifndef RED_PLATFORM_ORBIS
	RED_GPU_MEMORY_POOL( PoolD3D12Allocator, red::memory::DefaultAllocator );
	RED_GPU_MEMORY_POOL( PoolTextureSizeCache, red::memory::DefaultAllocator );
#endif


	// Fake pools, for tracking gpu memory use in different ways
#ifdef GPUAPI_USE_GPUMIRROR_POOLS
	RED_GPU_MEMORY_POOL( PoolGPUMirror, red::memory::NullAllocator );
		RED_GPU_MEMORY_POOL( PoolMirrorTextures, red::memory::NullAllocator );
			RED_GPU_MEMORY_POOL( GPUM_TG_Unknown, GPUFakeAllocator );
			RED_GPU_MEMORY_POOL( PoolMirrorTexturesGeneric, red::memory::NullAllocator );
				RED_GPU_MEMORY_POOL( GPUM_TG_Generic_Color, GPUFakeAllocator );
				RED_GPU_MEMORY_POOL( GPUM_TG_Generic_Grayscale, GPUFakeAllocator );
				RED_GPU_MEMORY_POOL( GPUM_TG_Generic_Normal, GPUFakeAllocator );
				RED_GPU_MEMORY_POOL( GPUM_TG_Generic_Data, GPUFakeAllocator );
				RED_GPU_MEMORY_POOL( GPUM_TG_Generic_UI, GPUFakeAllocator );
				RED_GPU_MEMORY_POOL( GPUM_TG_Generic_Font, GPUFakeAllocator );
				RED_GPU_MEMORY_POOL( GPUM_TG_Generic_LUT, GPUFakeAllocator );
				RED_GPU_MEMORY_POOL( GPUM_TG_Generic_MorphBlend, GPUFakeAllocator );
			RED_GPU_MEMORY_POOL( PoolMirrorTexturesMultilayer, red::memory::NullAllocator );
				RED_GPU_MEMORY_POOL( GPUM_TG_Multilayer_Color, GPUFakeAllocator );
				RED_GPU_MEMORY_POOL( GPUM_TG_Multilayer_Normal, GPUFakeAllocator );
				RED_GPU_MEMORY_POOL( GPUM_TG_Multilayer_Grayscale, GPUFakeAllocator );
				RED_GPU_MEMORY_POOL( GPUM_TG_Multilayer_Microblend, GPUFakeAllocator );
				RED_GPU_MEMORY_POOL( GPUM_TG_Multilayer_MLMask, GPUFakeAllocator );
			RED_GPU_MEMORY_POOL( PoolMirrorTexturesSystem, red::memory::NullAllocator );
				RED_GPU_MEMORY_POOL( GPUM_TG_System_Generic, GPUFakeAllocator );
				RED_GPU_MEMORY_POOL( GPUM_TG_System_RenderTarget, GPUFakeAllocator );
				RED_GPU_MEMORY_POOL( GPUM_TG_System_RenderTargetFlowAllocator, GPUFakeAllocator );
				RED_GPU_MEMORY_POOL( GPUM_TG_System_RenderTargetUI, red::memory::NullAllocator );
					RED_GPU_MEMORY_POOL( GPUM_TG_System_RenderTargetUI_Atlas, GPUFakeAllocator );
					RED_GPU_MEMORY_POOL( GPUM_TG_System_RenderTargetUI_Pool, GPUFakeAllocator );
					RED_GPU_MEMORY_POOL( GPUM_TG_System_RenderTargetUI_Manager, GPUFakeAllocator );
					RED_GPU_MEMORY_POOL( GPUM_TG_System_RenderTargetUI_HUDLayer, GPUFakeAllocator );
					RED_GPU_MEMORY_POOL( GPUM_TG_System_RenderTargetUI_CacheWidget, GPUFakeAllocator );
				RED_GPU_MEMORY_POOL( GPUM_TG_System_RayTracing, GPUFakeAllocator );
				RED_GPU_MEMORY_POOL( GPUM_TG_System_GameplayFX, GPUFakeAllocator );
				RED_GPU_MEMORY_POOL( GPUM_TG_System_PostProcess, GPUFakeAllocator );
				RED_GPU_MEMORY_POOL( GPUM_TG_System_Environment, GPUFakeAllocator );
				RED_GPU_MEMORY_POOL( GPUM_TG_System_EnvProbe, GPUFakeAllocator );
				RED_GPU_MEMORY_POOL( GPUM_TG_System_Shadows, GPUFakeAllocator );
				RED_GPU_MEMORY_POOL( GPUM_TG_System_Terrain, GPUFakeAllocator );
				RED_GPU_MEMORY_POOL( GPUM_TG_System_GI, GPUFakeAllocator );
				RED_GPU_MEMORY_POOL( GPUM_TG_System_MultilayerProxy, GPUFakeAllocator );
				RED_GPU_MEMORY_POOL( GPUM_TG_System_Video, GPUFakeAllocator );
				RED_GPU_MEMORY_POOL( GPUM_TG_System_Garment, GPUFakeAllocator );
				RED_GPU_MEMORY_POOL( GPUM_TG_System_MorphTargets, GPUFakeAllocator );
		RED_GPU_MEMORY_POOL( PoolMirrorBuffers, red::memory::NullAllocator );
			RED_GPU_MEMORY_POOL( GPUM_Buffer_Unknown, GPUFakeAllocator );
			RED_GPU_MEMORY_POOL( GPUM_Buffer_System, GPUFakeAllocator );
			RED_GPU_MEMORY_POOL( GPUM_Buffer_MeshResource, GPUFakeAllocator );
			RED_GPU_MEMORY_POOL( GPUM_Buffer_MeshCustom, GPUFakeAllocator );
			RED_GPU_MEMORY_POOL( GPUM_Buffer_AutoSpawner, GPUFakeAllocator );
			RED_GPU_MEMORY_POOL( GPUM_Buffer_Debug, GPUFakeAllocator );
			RED_GPU_MEMORY_POOL( GPUM_Buffer_DPL, GPUFakeAllocator );
			RED_GPU_MEMORY_POOL( GPUM_Buffer_Weather, GPUFakeAllocator );
			RED_GPU_MEMORY_POOL( GPUM_Buffer_ReflectionProbe, GPUFakeAllocator );
			RED_GPU_MEMORY_POOL( GPUM_Buffer_Skinning, GPUFakeAllocator );
			RED_GPU_MEMORY_POOL( GPUM_Buffer_Lights, GPUFakeAllocator );
			RED_GPU_MEMORY_POOL( GPUM_Buffer_Video, GPUFakeAllocator );
			RED_GPU_MEMORY_POOL( GPUM_Buffer_Particles, GPUFakeAllocator );
			RED_GPU_MEMORY_POOL( GPUM_Buffer_GI, red::memory::NullAllocator );
			RED_GPU_MEMORY_POOL(	GPUM_Buffer_GIManager, red::memory::NullAllocator );
			RED_GPU_MEMORY_POOL(		GPUM_Buffer_GIManagerLitProbes, GPUFakeAllocator );
			RED_GPU_MEMORY_POOL(		GPUM_Buffer_GIManagerLookup, GPUFakeAllocator );
			RED_GPU_MEMORY_POOL(		GPUM_Buffer_GIManagerInterpolation, GPUFakeAllocator );
			RED_GPU_MEMORY_POOL(		GPUM_Buffer_GIManagerLitBricks, GPUFakeAllocator );
			RED_GPU_MEMORY_POOL(		GPUM_Buffer_GIManagerLights, GPUFakeAllocator );
			RED_GPU_MEMORY_POOL(		GPUM_Buffer_GIManagerEnvVolume, GPUFakeAllocator );
			RED_GPU_MEMORY_POOL(	GPUM_Buffer_GIProxy, red::memory::NullAllocator );
			RED_GPU_MEMORY_POOL(		GPUM_Buffer_GIProxyBrick, GPUFakeAllocator );
			RED_GPU_MEMORY_POOL(		GPUM_Buffer_GIProxySurfel, GPUFakeAllocator );
			RED_GPU_MEMORY_POOL(		GPUM_Buffer_GIProxyProbes, GPUFakeAllocator );
			RED_GPU_MEMORY_POOL(		GPUM_Buffer_GIProxyFactors, GPUFakeAllocator );
			RED_GPU_MEMORY_POOL(		GPUM_Buffer_GIProxyAcceleration, GPUFakeAllocator );
			RED_GPU_MEMORY_POOL( GPUM_Buffer_Raytracing, GPUFakeAllocator );
			RED_GPU_MEMORY_POOL( GPUM_Buffer_RaytracingUpload, GPUFakeAllocator );
			RED_GPU_MEMORY_POOL( GPUM_Buffer_RaytracingAS, GPUFakeAllocator );
			RED_GPU_MEMORY_POOL( GPUM_Buffer_Decals, GPUFakeAllocator );
			RED_GPU_MEMORY_POOL( GPUM_Buffer_Instances, GPUFakeAllocator );
			RED_GPU_MEMORY_POOL( GPUM_Buffer_Materials, GPUFakeAllocator );
			RED_GPU_MEMORY_POOL( GPUM_Buffer_Multilayer, GPUFakeAllocator );
			RED_GPU_MEMORY_POOL( GPUM_Buffer_FrameResources, GPUFakeAllocator );
			RED_GPU_MEMORY_POOL( GPUM_Buffer_Misc, GPUFakeAllocator );
			RED_GPU_MEMORY_POOL( GPUM_Buffer_MorphTargets, GPUFakeAllocator );
#endif

#ifdef USE_NEW_SPECIALIZED_GPU_POOLS
	// Incorporating these will take some time. I keep them ifdefed out so they don't cause problems.
	// They will require per platform work anyway.
	RED_GPU_MEMORY_POOL( PoolTextures, red::memory::GpuAllocator );
	RED_GPU_MEMORY_POOL( PoolMatLibTextures, red::memory::GpuAllocator );
	RED_GPU_MEMORY_POOL( PoolMLDataBank, red::memory::GpuAllocator );
	RED_GPU_MEMORY_POOL( PoolColorTargets, red::memory::GpuAllocator );
	RED_GPU_MEMORY_POOL( PoolDepthStencilTargets, red::memory::GpuAllocator );
	RED_GPU_MEMORY_POOL( PoolConstantBuffers, red::memory::GpuAllocator );
	RED_GPU_MEMORY_POOL( PoolResourceBuffers, red::memory::GpuAllocator );
	RED_GPU_MEMORY_POOL( PoolShaders, red::memory::GpuAllocator );
	RED_GPU_MEMORY_POOL( PoolProbes, red::memory::GpuAllocator );
	RED_GPU_MEMORY_POOL( PoolCommandlists, red::memory::GpuAllocator );
	RED_GPU_MEMORY_POOL( PoolQueries, red::memory::GpuAllocator );
	RED_GPU_MEMORY_POOL( PoolDebugGPU, red::memory::GpuAllocator );
#endif


#if defined( RED_PLATFORM_DURANGO )

#ifdef GPUAPI_USE_FAKE_ALLOCATOR
	RED_GPU_MEMORY_POOL( PoolD3D, GPUFakeAllocator );
#endif

	// #TODO: For in-place stuff?
	RED_GPU_MEMORY_POOL( PoolGPUTextures, red::memory::DefaultAllocator );
	RED_GPU_MEMORY_POOL( PoolGPUBuffers, red::memory::DefaultAllocator );

	RED_GPU_MEMORY_POOL( PoolCommandListCB, red::memory::DefaultAllocator );
	RED_GPU_MEMORY_POOL( PoolRenderStateCache, red::memory::DefaultAllocator );	// Cache of the render states (ds + rs + bs)
	RED_GPU_MEMORY_POOL( PoolPSOCache, red::memory::DefaultAllocator );
	RED_GPU_MEMORY_POOL( PoolBreadcrumbs, red::memory::DefaultAllocator );
#ifdef GPUAPI_USE_GPUMIRROR_POOLS
	RED_GPU_MEMORY_POOL( PoolBreadcrumbs_Markers, GPUFakeAllocator );
	RED_GPU_MEMORY_POOL( PoolBreadcrumbs_MarkersUsed, GPUFakeAllocator );
#endif

#elif defined( RED_PLATFORM_ORBIS  )

	struct GpuAllocatorDepotOrbis
	{
		red::memory::LockingDynamicTLSFAllocator onionAllocator;
		red::memory::LockingDynamicTLSFAllocator garlicAllocator;

		red::memory::GpuAllocator			gpuAllocator;
		red::memory::GpuContiguousAllocator swapChainAllocator;
	};

	// Onion bus
	RED_GPU_MEMORY_POOL( PoolGPUOnion, red::memory::LockingDynamicTLSFAllocator );
	RED_GPU_MEMORY_POOL(	PoolGPUCommandBuffer, red::memory::LockingDynamicTLSFAllocator );
	RED_GPU_MEMORY_POOL(	PoolGPUQueryResults, red::memory::LockingDynamicTLSFAllocator );
	RED_GPU_MEMORY_POOL(	PoolGPUConstantBuffers,	red::memory::LockingDynamicTLSFAllocator );
	RED_GPU_MEMORY_POOL(	PoolGPUShaderHeader, red::memory::LockingDynamicTLSFAllocator );
	RED_GPU_MEMORY_POOL(	PoolGPUTextureStaging, red::memory::LockingDynamicTLSFAllocator );
	RED_GPU_MEMORY_POOL(	PoolGPUBuffer_Mapped, red::memory::LockingDynamicTLSFAllocator );
	
	// Garlic bus
	RED_GPU_MEMORY_POOL( PoolGPUGarlic, red::memory::LockingDynamicTLSFAllocator );
	RED_GPU_MEMORY_POOL(	PoolGPUDescriptors, red::memory::LockingDynamicTLSFAllocator );
	RED_GPU_MEMORY_POOL(	PoolGPUShader, red::memory::LockingDynamicTLSFAllocator );
	RED_GPU_MEMORY_POOL(	PoolGPURenderData, red::memory::GpuAllocator );
	RED_GPU_MEMORY_POOL(		PoolGPUSwapChain, red::memory::GpuContiguousAllocator );
	RED_GPU_MEMORY_POOL(		PoolGPURenderTargets, red::memory::GpuAllocator );
	RED_GPU_MEMORY_POOL(		PoolGPUTextureResources, red::memory::GpuAllocator );
	RED_GPU_MEMORY_POOL(		PoolGPUBufferResources,	red::memory::GpuAllocator );
	RED_GPU_MEMORY_POOL(			PoolGPUBuffer_Stream, red::memory::GpuAllocator );
	RED_GPU_MEMORY_POOL(			PoolGPUBuffer_RawAndStructured,	red::memory::GpuAllocator );
	RED_GPU_MEMORY_POOL(			PoolGPUBuffer_Indirect,	red::memory::GpuAllocator );
	RED_GPU_MEMORY_POOL(			PoolGPUBuffer_Typed, red::memory::GpuAllocator );

	RED_GPU_MEMORY_POOL( PoolFetchShader, red::memory::DefaultAllocator );
	RED_GPU_MEMORY_POOL( PoolGNMdescriptors, red::memory::DefaultAllocator );

#elif defined( RED_PLATFORM_WINPC )

	RED_GPU_MEMORY_POOL( PoolGPUTextures, red::memory::DefaultAllocator );
	RED_GPU_MEMORY_POOL( PoolGPUBuffers, red::memory::DefaultAllocator );
	RED_GPU_MEMORY_POOL( PoolGPUInPlaceRenderData, red::memory::DefaultAllocator );
	RED_GPU_MEMORY_POOL( PoolCommandListCB, red::memory::DefaultAllocator );
	RED_GPU_MEMORY_POOL( PoolVendorApi, red::memory::DefaultAllocator );
	RED_GPU_MEMORY_POOL( PoolRenderStateCache, red::memory::DefaultAllocator );	// Cache of the render states (ds + rs + bs)
	RED_GPU_MEMORY_POOL( PoolResidencyMgr, red::memory::DefaultAllocator );
	RED_GPU_MEMORY_POOL( PoolPSOCache, red::memory::DefaultAllocator );
	RED_GPU_MEMORY_POOL( PoolRaytrace, red::memory::DefaultAllocator );
	RED_GPU_MEMORY_POOL( PoolBreadcrumbs, red::memory::DefaultAllocator );
#ifdef GPUAPI_USE_GPUMIRROR_POOLS
	RED_GPU_MEMORY_POOL( PoolBreadcrumbs_Markers, GPUFakeAllocator );
	RED_GPU_MEMORY_POOL( PoolBreadcrumbs_MarkersUsed, GPUFakeAllocator );
#endif

#endif
}

//////////////////////////////////////////////////////////////////
// Red Memory Setup

#define GPU_API_ALLOCATE_REGION( PoolName, Size, Alignment, Lifetime )	red::memory::RegionHandle( RED_ALLOCATE_ALIGNED_WITHOUT_HOOKS( PoolName, Size, Alignment, red::memory::HookType_Memory_Marking ) )
#define GPU_API_UNLOCK_REGION( PoolName, Region )						do {} while (0,0)
#define GPU_API_FREE_REGION( PoolName, Region )							do { RED_FREE_WITHOUT_HOOKS( PoolName, Region.GetRawPtr(), red::memory::HookType_Memory_Marking ); Region = red::memory::RegionHandle(); } while(0,0 )

namespace GpuApi
{
	// Memory will be appropriately aligned, based on pool/class.
	enum EInPlaceResourceType : Uint8
	{
		INPLACE_None,
		INPLACE_Texture,
		INPLACE_Buffer,
		INPLACE_DefragTemp,

		INPLACE_MAX
	};

	// If alignment of -1 is passed, a safe default will be used
	RENDER_DATA_API void* AllocateInPlaceMemory( EInPlaceResourceType type, size_t size, Uint32 alignment );
	RENDER_DATA_API void  ReleaseInPlaceMemory( EInPlaceResourceType type, void* buffer );

	RENDER_DATA_API red::memory::RegionHandle AllocateInPlaceMemoryRegion( EInPlaceResourceType type, size_t size, Uint32 alignment=(Uint32)-1, red::memory::RegionLifetimeHint lifeTimeHint = red::memory::Region_Shortlived );
	RENDER_DATA_API void ReleaseInPlaceMemoryRegion( EInPlaceResourceType type, red::memory::RegionHandle & buffer );
	RENDER_DATA_API void UnlockInPlaceMemoryRegion( EInPlaceResourceType type, red::memory::RegionHandle buffer );

	RENDER_DATA_API void InitializeMemoryPool();
	RENDER_DATA_API void UninitializeMemoryPool();
}
