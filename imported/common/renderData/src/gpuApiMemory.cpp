/**
* Copyright (c) 2010 CD Projekt Red. All Rights Reserved.
*/
#include "build.h"
#include "../../redMemory/include/redMemoryPublic.h"
#include "../../redMath/include/redMathPublic.h"
#include "gpuApiMemory.h"
#include "renderingMemory.h"
#include "../../redMemory/include/flags.h"
#include "../../redMemory/include/oomHandlerIgnore.h"

#if defined( RED_PLATFORM_ORBIS )
	#include <gnm/platform.h>
#endif
#include "renderSettingsVars.h"

namespace GpuApi
{

	// Once the data is all cooked properly, we should have proper alignment values for everything and this will be redundant
	RED_INLINE Uint32 CalculateInplaceMemoryAlignment( EInPlaceResourceType type )
	{
		switch ( type )
		{
#if defined( RED_PLATFORM_DURANGO )
			// In worst case, Xbox can ask for 32k aligned texture memory, but we are using 1D tiling so 256byte should be enough
		case INPLACE_Texture:		return 0x100;
		case INPLACE_DefragTemp:	return 0x100;
		case INPLACE_Buffer:		return 16;
#elif defined( RED_PLATFORM_ORBIS )
			// PS4 likes 256 byte aligned textures.
		case INPLACE_Texture:		return 0x100;
		case INPLACE_DefragTemp:	return 0x100;
		case INPLACE_Buffer:		return 16;
#else
		case INPLACE_Texture:		return 16;
		case INPLACE_Buffer:		return 16;
#endif
		default:
			RED_HALT( "Unsupported in-place resource pool" );
			return 0;
		}
	}

	// this mutex is needed because the inplace memory can be released from the decompression thread in case of the canceled streaming taks
	static red::Mutex	st_inplacePoolLock;

	red::memory::RegionHandle AllocateInPlaceMemoryRegion( EInPlaceResourceType type, size_t size, Uint32 alignment, red::memory::RegionLifetimeHint lifeTimeHint /*= red::MemoryFramework::Region_Shortlived*/ )
	{
#if 0
		red::ScopedLock< red::Mutex > lock( st_inplacePoolLock );

		if ( alignment == (Uint32)-1 )
		{
			alignment = CalculateInplaceMemoryAlignment( type );
		}

		// TODO: figure out whether it makes sense to have those different pools for in place
		switch ( type )
		{
			case INPLACE_Texture:
#if !defined( RED_PLATFORM_LINUX )
				return GPU_API_ALLOCATE_REGION( PoolGPUInPlaceRenderData, size, alignment, lifeTimeHint );
#endif
			case INPLACE_Buffer:
#if defined( RED_PLATFORM_ORBIS )
				return GPU_API_ALLOCATE_REGION( PoolGPURenderData, size, alignment, red::memory::Region_Shortlived );
#elif defined( RED_PLATFORM_DURANGO )
				return GPU_API_ALLOCATE_REGION( PoolGPUInPlaceMeshBuffers, size, alignment, red::memory::Region_Shortlived );
#endif

			default:
				RED_HALT( "Unsupported in-place resource pool" );
				return red::memory::RegionHandle();
		}
#endif

		return red::memory::RegionHandle();
	}

	void UnlockInPlaceMemoryRegion( EInPlaceResourceType type, red::memory::RegionHandle buffer )
	{
#if 0
		switch( type )
		{
		case INPLACE_Texture:
#if !defined( RED_PLATFORM_LINUX )
			GPU_API_UNLOCK_REGION( PoolGPUInPlaceRenderData, buffer );
#endif
			break;
		case INPLACE_Buffer:
#if defined( RED_PLATFORM_ORBIS )
			GPU_API_UNLOCK_REGION( PoolGPUBuffers, buffer );
#elif defined( RED_PLATFORM_DURANGO )
			GPU_API_UNLOCK_REGION( PoolGPUInPlaceMeshBuffers, buffer );
#endif
			break;
#if defined( RED_PLATFORM_CONSOLE )
		case INPLACE_DefragTemp:
			GPU_API_UNLOCK_REGION( PoolGPUInPlaceDefragTemp, buffer );
			break;
#endif
		default:
			RED_HALT( "Unsupported in-place resource pool" );
		}
#endif
	}

	void ReleaseInPlaceMemoryRegion( EInPlaceResourceType type, red::memory::RegionHandle & buffer )
	{
#if 0
		red::ScopedLock< red::Mutex > lock( st_inplacePoolLock );

		// TODO: Figure out whether it makes sense to have a separate in place pool
		switch ( type )
		{
			case INPLACE_Texture:
#if !defined( RED_PLATFORM_LINUX )
				GPU_API_FREE_REGION( PoolGPUInPlaceRenderData, buffer );
#endif
				break;

			case INPLACE_Buffer:
#ifdef RED_PLATFORM_ORBIS
				GPU_API_FREE_REGION( PoolGPURenderData, buffer );
				break;

#elif defined( RED_PLATFORM_DURANGO )
				GPU_API_FREE_REGION( PoolGPUInPlaceMeshBuffers, buffer );
				break;
			case INPLACE_DefragTemp:
				GPU_API_FREE_REGION( PoolGPUInPlaceDefragTemp, buffer );
				break;
#endif
		default:
			RED_HALT( "Unsupported in-place resource pool" );
		}
#endif
	}

	void* AllocateInPlaceMemory( EInPlaceResourceType type, size_t size, Uint32 alignment )
	{
#ifdef RED_PLATFORM_CONSOLE
		red::ScopedLock< red::Mutex > lock( st_inplacePoolLock );

		// THIS FUNCTION SHOULD NEVER BE CALLED
		RED_HALT( "This should not be called!!!" );

		return nullptr;
#else
		size_t actualAlignment = alignment;
		if( alignment == -1 )
		{
			actualAlignment = CalculateInplaceMemoryAlignment( type );
		}

		switch ( type )
		{
		case INPLACE_Buffer:
#if !defined( RED_PLATFORM_LINUX )
			return RED_ALLOCATE_ALIGNED( PoolGPUBuffers, size, actualAlignment );
#endif
		default:
			RED_HALT( "Unsupported in-place resource pool" );
			return nullptr;
		}
#endif
	}

	void ReleaseInPlaceMemory( EInPlaceResourceType type, void* buffer )
	{
#ifdef RED_PLATFORM_CONSOLE
		red::ScopedLock< red::Mutex > lock( st_inplacePoolLock );

		// THIS FUNCTION SHOULD NEVER BE CALLED
		RED_HALT( "This should not be called!!!" );
#else
		switch ( type )
		{
		case INPLACE_Buffer:
#if !defined( RED_PLATFORM_LINUX )
			RED_FREE( PoolGPUBuffers, buffer );		
			break;
#endif

		default:
			RED_HALT( "Unsupported in-place resource pool" );
		}
#endif
	}	


#ifdef GPUAPI_USE_FAKE_ALLOCATOR
	GPUFakeAllocator s_gpuFakeAllocator;
	red::memory::PoolOOMHandlerIgnore s_gpuOOMHandlerIgnore;

#define INIT_FAKE_POOL( Pool, Parent, budget ) do {										\
		RED_INITIALIZE_MEMORY_POOL( Pool, Parent, s_gpuFakeAllocator, budget );			\
		red::memory::SetPoolOOMHandler<Pool>( &s_gpuOOMHandlerIgnore );					\
		red::memory::ForceNoDebugAllocator<Pool>();										\
		} while (0)


	// On consoles we have better memory tracking than PoolGPUMirror, so we want to mark the mirror pools as mirrors, so
	// they aren't included in "total" metrics. On PC we don't have anything better, so it's not mirrored.
#ifdef RED_PLATFORM_CONSOLE
#define INIT_MIRROR_POOL( Pool, Parent, allocator, budget ) do {						\
		RED_INITIALIZE_MEMORY_POOL( Pool, Parent, allocator, budget );					\
		red::memory::SetMirroredPool<Pool>();											\
		} while (0)

#define INIT_MIRROR_FAKE_POOL( Pool, Parent, budget ) do {								\
		RED_INITIALIZE_MEMORY_POOL( Pool, Parent, s_gpuFakeAllocator, budget );			\
		red::memory::SetPoolOOMHandler<Pool>( &s_gpuOOMHandlerIgnore );					\
		red::memory::ForceNoDebugAllocator<Pool>();										\
		red::memory::SetMirroredPool<Pool>();											\
		} while (0)

#else
#define INIT_MIRROR_POOL( Pool, Parent, allocator, budget ) RED_INITIALIZE_MEMORY_POOL( Pool, Parent, allocator, budget )
#define INIT_MIRROR_FAKE_POOL( Pool, Parent, budget ) INIT_FAKE_POOL( Pool, Parent, budget )
#endif

#endif

	void InitializeFakePools()
	{
#ifdef GPUAPI_USE_GPUMIRROR_POOLS

		// #TODO: Proper budgets here, probably per-platform?

		// #NOTE: Not the full PoolGPU budget, since there are other internal resources that won't be in here.
		const auto gpuMirrorBudget = 2900_MB; // red::memory::GetPoolBudget<red::memory::PoolGPU>();
		auto& nullAllocator = red::memory::AcquireNullAllocator();

		INIT_MIRROR_POOL( PoolGPUMirror, red::memory::PoolRoot, nullAllocator, gpuMirrorBudget );
		{
			INIT_MIRROR_POOL( PoolMirrorTextures,				PoolGPUMirror, nullAllocator, 1690_MB );
			{
				INIT_MIRROR_FAKE_POOL( GPUM_TG_Unknown,					PoolMirrorTextures, 0 );
				INIT_MIRROR_POOL( PoolMirrorTexturesGeneric,			PoolMirrorTextures, nullAllocator, 700_MB );
				{
					INIT_MIRROR_FAKE_POOL( GPUM_TG_Generic_Color,					PoolMirrorTexturesGeneric, 0 );
					INIT_MIRROR_FAKE_POOL( GPUM_TG_Generic_Grayscale,				PoolMirrorTexturesGeneric, 0 );
					INIT_MIRROR_FAKE_POOL( GPUM_TG_Generic_Normal,					PoolMirrorTexturesGeneric, 0 );
					INIT_MIRROR_FAKE_POOL( GPUM_TG_Generic_Data,					PoolMirrorTexturesGeneric, 0 );
					INIT_MIRROR_FAKE_POOL( GPUM_TG_Generic_UI,						PoolMirrorTexturesGeneric, 0 );
					INIT_MIRROR_FAKE_POOL( GPUM_TG_Generic_Font,					PoolMirrorTexturesGeneric, 0 );
					INIT_MIRROR_FAKE_POOL( GPUM_TG_Generic_LUT,						PoolMirrorTexturesGeneric, 0 );
					INIT_MIRROR_FAKE_POOL( GPUM_TG_Generic_MorphBlend,				PoolMirrorTexturesGeneric, 0 );
				}
				INIT_MIRROR_POOL( PoolMirrorTexturesMultilayer,			PoolMirrorTextures, nullAllocator, 350_MB );
				{
					INIT_MIRROR_FAKE_POOL( GPUM_TG_Multilayer_Color,				PoolMirrorTexturesMultilayer, 0 );
					INIT_MIRROR_FAKE_POOL( GPUM_TG_Multilayer_Normal,				PoolMirrorTexturesMultilayer, 0 );
					INIT_MIRROR_FAKE_POOL( GPUM_TG_Multilayer_Grayscale,			PoolMirrorTexturesMultilayer, 0 );
					INIT_MIRROR_FAKE_POOL( GPUM_TG_Multilayer_Microblend,			PoolMirrorTexturesMultilayer, 0 );
					INIT_MIRROR_FAKE_POOL( GPUM_TG_Multilayer_MLMask,				PoolMirrorTexturesMultilayer, 0 );
				}
				INIT_MIRROR_POOL( PoolMirrorTexturesSystem,				PoolMirrorTextures, nullAllocator, 640_MB );
				{
					INIT_MIRROR_FAKE_POOL( GPUM_TG_System_Generic,					PoolMirrorTexturesSystem, 0 );
					INIT_MIRROR_FAKE_POOL( GPUM_TG_System_RenderTarget,				PoolMirrorTexturesSystem, 10_MB );
					INIT_MIRROR_FAKE_POOL( GPUM_TG_System_RenderTargetFlowAllocator,PoolMirrorTexturesSystem, 150_MB );
					INIT_MIRROR_POOL( GPUM_TG_System_RenderTargetUI,				PoolMirrorTexturesSystem, nullAllocator, 250_MB );	// too big...
					{
						INIT_MIRROR_FAKE_POOL( GPUM_TG_System_RenderTargetUI_Atlas,			GPUM_TG_System_RenderTargetUI, 0 );
						INIT_MIRROR_FAKE_POOL( GPUM_TG_System_RenderTargetUI_Pool,			GPUM_TG_System_RenderTargetUI, 0 );
						INIT_MIRROR_FAKE_POOL( GPUM_TG_System_RenderTargetUI_Manager,		GPUM_TG_System_RenderTargetUI, 0 );
						INIT_MIRROR_FAKE_POOL( GPUM_TG_System_RenderTargetUI_HUDLayer,		GPUM_TG_System_RenderTargetUI, 0 );
						INIT_MIRROR_FAKE_POOL( GPUM_TG_System_RenderTargetUI_CacheWidget,	GPUM_TG_System_RenderTargetUI, 0 );
					}
					INIT_MIRROR_FAKE_POOL( GPUM_TG_System_RayTracing,				PoolMirrorTexturesSystem, 0 );
					INIT_MIRROR_FAKE_POOL( GPUM_TG_System_GameplayFX,				PoolMirrorTexturesSystem, 10_MB );
					INIT_MIRROR_FAKE_POOL( GPUM_TG_System_PostProcess,				PoolMirrorTexturesSystem, 12_MB );
					INIT_MIRROR_FAKE_POOL( GPUM_TG_System_Environment,				PoolMirrorTexturesSystem, 25_MB );
					INIT_MIRROR_FAKE_POOL( GPUM_TG_System_EnvProbe,					PoolMirrorTexturesSystem, 48_MB );
					INIT_MIRROR_FAKE_POOL( GPUM_TG_System_Shadows,					PoolMirrorTexturesSystem, 48_MB );
					INIT_MIRROR_FAKE_POOL( GPUM_TG_System_Terrain,					PoolMirrorTexturesSystem, 0 );
					INIT_MIRROR_FAKE_POOL( GPUM_TG_System_GI,						PoolMirrorTexturesSystem, 30_MB );
					INIT_MIRROR_FAKE_POOL( GPUM_TG_System_MultilayerProxy,			PoolMirrorTexturesSystem, 0 );
					INIT_MIRROR_FAKE_POOL( GPUM_TG_System_Video,					PoolMirrorTexturesSystem, 20_MB );
					INIT_MIRROR_FAKE_POOL( GPUM_TG_System_Garment,					PoolMirrorTexturesSystem, 5_MB );
					INIT_MIRROR_FAKE_POOL( GPUM_TG_System_MorphTargets,				PoolMirrorTexturesSystem, 32_MB );
				}
			}

			INIT_MIRROR_POOL( PoolMirrorBuffers,				PoolGPUMirror, nullAllocator, 1210_MB );
			{
				INIT_MIRROR_FAKE_POOL( GPUM_Buffer_Unknown,							PoolMirrorBuffers, 0 );
				INIT_MIRROR_FAKE_POOL( GPUM_Buffer_System,							PoolMirrorBuffers, 4_MB );
				INIT_MIRROR_FAKE_POOL( GPUM_Buffer_MeshResource,					PoolMirrorBuffers, 700_MB );
				INIT_MIRROR_FAKE_POOL( GPUM_Buffer_MeshCustom,						PoolMirrorBuffers, 60_MB );
				INIT_MIRROR_FAKE_POOL( GPUM_Buffer_AutoSpawner,						PoolMirrorBuffers, 2_MB );
				INIT_MIRROR_FAKE_POOL( GPUM_Buffer_Debug,							PoolMirrorBuffers, 0 );
				INIT_MIRROR_FAKE_POOL( GPUM_Buffer_DPL,								PoolMirrorBuffers, 8_MB );
				INIT_MIRROR_FAKE_POOL( GPUM_Buffer_Weather,							PoolMirrorBuffers, 2_MB );
				INIT_MIRROR_FAKE_POOL( GPUM_Buffer_ReflectionProbe,					PoolMirrorBuffers, 128_KB );
				INIT_MIRROR_FAKE_POOL( GPUM_Buffer_Skinning,						PoolMirrorBuffers, 40_MB );
				INIT_MIRROR_FAKE_POOL( GPUM_Buffer_Lights,							PoolMirrorBuffers, 4_MB );
				INIT_MIRROR_FAKE_POOL( GPUM_Buffer_Video,							PoolMirrorBuffers, 8_KB );
				INIT_MIRROR_FAKE_POOL( GPUM_Buffer_Particles,						PoolMirrorBuffers, 4_MB );
				INIT_MIRROR_POOL( GPUM_Buffer_GI,					PoolMirrorBuffers, nullAllocator, 300_MB );
				{
					INIT_MIRROR_POOL( GPUM_Buffer_GIManager,			GPUM_Buffer_GI, nullAllocator, 200_MB );
					{
						INIT_MIRROR_FAKE_POOL( GPUM_Buffer_GIManagerLitProbes,		GPUM_Buffer_GIManager, 95_MB );
						INIT_MIRROR_FAKE_POOL( GPUM_Buffer_GIManagerLookup,			GPUM_Buffer_GIManager, 48_MB );
						INIT_MIRROR_FAKE_POOL( GPUM_Buffer_GIManagerInterpolation,	GPUM_Buffer_GIManager, 56_MB );
						INIT_MIRROR_FAKE_POOL( GPUM_Buffer_GIManagerLitBricks,		GPUM_Buffer_GIManager, 512_KB );
						INIT_MIRROR_FAKE_POOL( GPUM_Buffer_GIManagerLights,			GPUM_Buffer_GIManager, 32_KB );
						INIT_MIRROR_FAKE_POOL( GPUM_Buffer_GIManagerEnvVolume,		GPUM_Buffer_GIManager, 32_KB );
					}
					INIT_MIRROR_POOL( GPUM_Buffer_GIProxy,				GPUM_Buffer_GI, nullAllocator, 100_MB );
					{
						INIT_MIRROR_FAKE_POOL( GPUM_Buffer_GIProxyBrick,			GPUM_Buffer_GIProxy, 1_MB );
						INIT_MIRROR_FAKE_POOL( GPUM_Buffer_GIProxySurfel,			GPUM_Buffer_GIProxy, 78_MB );
						INIT_MIRROR_FAKE_POOL( GPUM_Buffer_GIProxyProbes,			GPUM_Buffer_GIProxy, 3_MB );
						INIT_MIRROR_FAKE_POOL( GPUM_Buffer_GIProxyFactors,			GPUM_Buffer_GIProxy, 12_MB );
						INIT_MIRROR_FAKE_POOL( GPUM_Buffer_GIProxyAcceleration,		GPUM_Buffer_GIProxy, 6_MB );
					}
				}
				INIT_MIRROR_FAKE_POOL( GPUM_Buffer_Raytracing,						PoolMirrorBuffers, 0 );
				INIT_MIRROR_FAKE_POOL( GPUM_Buffer_RaytracingUpload,				PoolMirrorBuffers, 0 );
				INIT_MIRROR_FAKE_POOL( GPUM_Buffer_RaytracingAS,					PoolMirrorBuffers, 0 );
				INIT_MIRROR_FAKE_POOL( GPUM_Buffer_Decals,							PoolMirrorBuffers, 512_KB );
				INIT_MIRROR_FAKE_POOL( GPUM_Buffer_Instances,						PoolMirrorBuffers, 15_MB );
				INIT_MIRROR_FAKE_POOL( GPUM_Buffer_Materials,						PoolMirrorBuffers, 256_KB );
				INIT_MIRROR_FAKE_POOL( GPUM_Buffer_Multilayer,						PoolMirrorBuffers, 10_MB );
				INIT_MIRROR_FAKE_POOL( GPUM_Buffer_FrameResources,					PoolMirrorBuffers, 8_MB );
				INIT_MIRROR_FAKE_POOL( GPUM_Buffer_Misc,							PoolMirrorBuffers, 1_MB );
				INIT_MIRROR_FAKE_POOL( GPUM_Buffer_MorphTargets,					PoolMirrorBuffers, 16_MB );
			}
		}

#ifdef RED_PLATFORM_CONSOLE
		// On consoles we have a separate pool to hold actual GPU allocations (PS4 has real allocations, Xbox has XMem
		// hooked to at least track them).
		red::memory::DisableContributeToParentMetrics<PoolGPUMirror>();
#endif

#endif
	}

#ifdef RED_PLATFORM_DURANGO

	struct GpuAllocatorDepotDurango
	{
		//red::memory::GpuAllocator gpuAllocator;
	};

	GpuAllocatorDepotDurango s_gpuAllocatorDepot;

	static void UninitializeMemoryPoolDurango()
	{
		//s_gpuAllocatorDepot.gpuAllocator.Uninitialize();
	}
	
	static void InitializeMemoryPoolDurango()
	{
		// GPU allocations
		{
			const Uint32 bufferBudget		= RED_MEGA_BYTE( 1 );
			const Uint32 texturesBudget		= RED_GIGA_BYTE( 1) + RED_MEGA_BYTE( 400 );

			/*
			red::memory::GpuAllocatorParameter gpuAllocatorParam =
			{
				&red::memory::AcquireSystemAllocator(),
				RED_GIGA_BYTE( 8 ),		// virtual range
				RED_MEGA_BYTE( 8 ),//RED_MEGA_BYTE( 512 ),	// default commited size
				RED_MEGA_BYTE( 16 ),	// chunk size, when commiting
				red::memory::Flags_CPU_Read_Write | red::memory::Flags_GPU_Read_Write
			};

			s_gpuAllocatorDepot.gpuAllocator.Initialize( gpuAllocatorParam );
			*/

#ifdef GPUAPI_USE_FAKE_ALLOCATOR
			const auto gpuBudget = red::memory::GetPoolBudget<red::memory::PoolGPU>();

			INIT_FAKE_POOL( PoolD3D,			red::memory::PoolGPU, gpuBudget );
#endif

			RED_INITIALIZE_MEMORY_POOL( PoolGPUBuffers,		red::memory::PoolGPU, red::memory::AcquireDefaultAllocator(), bufferBudget );
			RED_INITIALIZE_MEMORY_POOL( PoolGPUTextures,	red::memory::PoolGPU, red::memory::AcquireDefaultAllocator(), texturesBudget );
		}

		// CPU allocations
		{
			const Uint32 poolGpuApi_size = RED_MEGA_BYTE( 20 );

			const Uint32 poolTiny_size				= RED_KILO_BYTE( 1 );
			const Uint32 poolDeviceData_size		= RED_MEGA_BYTE( 12 );
			const Uint32 poolCommandList_size		= RED_MEGA_BYTE( 1 );
			const Uint32 poolResRefExtra_size		= RED_KILO_BYTE( 512 );
			const Uint32 poolQueries_size			= 0;					// Currently only used for profiler
			const Uint32 poolBreadcrumbs_size		= 0;
			const Uint32 poolRenderStateCache_size	= RED_KILO_BYTE( 8 );
			const Uint32 poolPSOCache_size			= RED_KILO_BYTE( 128 );
			const Uint32 poolD3D12Allocator_size	= RED_MEGA_BYTE( 2 );
			const Uint32 poolTextureSizeCache_size	= RED_KILO_BYTE( 512 );
			const Uint32 poolCommandListCB_size		= RED_MEGA_BYTE( 2 );

			RED_INITIALIZE_MEMORY_POOL( PoolGpuApi,	 rend::PoolRendering_, red::memory::AcquireDefaultAllocator(), poolGpuApi_size );
			{
				RED_INITIALIZE_MEMORY_POOL( PoolTiny,				PoolGpuApi, red::memory::AcquireDefaultAllocator(), poolTiny_size );
				RED_INITIALIZE_MEMORY_POOL( PoolDeviceData,			PoolGpuApi, red::memory::AcquireDefaultAllocator(), poolDeviceData_size );
				RED_INITIALIZE_MEMORY_POOL( PoolRenderStateCache,	PoolGpuApi, red::memory::AcquireDefaultAllocator(), poolRenderStateCache_size );
				RED_INITIALIZE_MEMORY_POOL( PoolCommandLists,		PoolGpuApi, red::memory::AcquireDefaultAllocator(), poolCommandList_size );
				RED_INITIALIZE_MEMORY_POOL( PoolCommandListCB,		PoolGpuApi, red::memory::AcquireDefaultAllocator(), poolCommandListCB_size );
				RED_INITIALIZE_MEMORY_POOL( PoolResRefExtra,		PoolGpuApi, red::memory::AcquireDefaultAllocator(), poolResRefExtra_size );
				RED_INITIALIZE_MEMORY_POOL( PoolQueries,			PoolGpuApi, red::memory::AcquireDefaultAllocator(), poolQueries_size );
				RED_INITIALIZE_MEMORY_POOL( PoolPSOCache,			PoolGpuApi, red::memory::AcquireDefaultAllocator(), poolPSOCache_size );
				RED_INITIALIZE_MEMORY_POOL( PoolDebug,				PoolGpuApi, red::memory::AcquireDefaultAllocator(), 0 );
				RED_INITIALIZE_MEMORY_POOL( PoolD3D12Allocator,		PoolGpuApi, red::memory::AcquireDefaultAllocator(), poolD3D12Allocator_size );
				RED_INITIALIZE_MEMORY_POOL( PoolTextureSizeCache,	PoolGpuApi, red::memory::AcquireDefaultAllocator(), poolTextureSizeCache_size );

				RED_INITIALIZE_MEMORY_POOL( PoolBreadcrumbs,		PoolGpuApi, red::memory::AcquireDefaultAllocator(), poolBreadcrumbs_size );
#ifdef GPUAPI_USE_GPUMIRROR_POOLS
				{
					INIT_FAKE_POOL( PoolBreadcrumbs_Markers, PoolBreadcrumbs, 0 );
					INIT_FAKE_POOL( PoolBreadcrumbs_MarkersUsed, PoolBreadcrumbs_Markers, 0 );
				}
#endif
			}
		}
	}
#elif defined( RED_PLATFORM_ORBIS )

	GpuAllocatorDepotOrbis s_gpuAllocatorDepot;

	void UninitializeMemoryPoolOrbis()
	{
		s_gpuAllocatorDepot.onionAllocator.Uninitialize();
		s_gpuAllocatorDepot.garlicAllocator.Uninitialize();
		s_gpuAllocatorDepot.swapChainAllocator.Uninitialize();
		s_gpuAllocatorDepot.gpuAllocator.Uninitialize();
	}

	void InitializeMemoryPoolOrbis()
	{
		const auto gpuBudget = red::memory::GetPoolBudget<red::memory::PoolGPU>();

		// GPU allocations
		{
			const Uint32 swapChainBudget = ( sce::Gnm::getGpuMode() == sce::Gnm::kGpuModeNeo ) ? 31_MB : 17_MB;

			const red::memory::DynamicTLSFAllocatorParameter onionParam = {
				&red::memory::AcquireSystemAllocator(),
				RED_GIGA_BYTE( 2 ),
				RED_MEGA_BYTE( 8 ),
				RED_MEGA_BYTE( 2 ),
				red::memory::Flags_CPU_Read_Write | red::memory::Flags_GPU_Read_Write | red::memory::Flags_Onion_Bus | red::memory::Flags_Skip_System_OOM
			};
			s_gpuAllocatorDepot.onionAllocator.Initialize( onionParam );

			const red::memory::DynamicTLSFAllocatorParameter garlicParam = {
				&red::memory::AcquireSystemAllocator(),
				RED_GIGA_BYTE( 8 ),
				RED_MEGA_BYTE( 1 ),
				RED_MEGA_BYTE( 2 ),
				red::memory::Flags_CPU_Read_Write | red::memory::Flags_GPU_Read_Write | red::memory::Flags_Garlic_Bus | red::memory::Flags_Skip_System_OOM,
				RED_KILO_BYTE( 64 )
			};
			s_gpuAllocatorDepot.garlicAllocator.Initialize( garlicParam );

			red::memory::GpuAllocatorParameter gpuAllocatorParam;
			gpuAllocatorParam.systemAllocator = &red::memory::AcquireSystemAllocator();
			// Allow the virtual GPU allocator to use less memory if we are in a downgraded mode. Gives more breathing space to non-graphics systems.
			s_gpuAllocatorDepot.gpuAllocator.Initialize( gpuAllocatorParam );

			red::memory::GpuContiguousAllocatorParameter swapChainParam;
			swapChainParam.systemAllocator = &red::memory::AcquireSystemAllocator();
			s_gpuAllocatorDepot.swapChainAllocator.Initialize( swapChainParam );

			auto& allocOnion = s_gpuAllocatorDepot.onionAllocator;
			auto& allocGarlic = s_gpuAllocatorDepot.garlicAllocator;
			auto& allocGpu = s_gpuAllocatorDepot.gpuAllocator;
			auto& allocSwapchain = s_gpuAllocatorDepot.swapChainAllocator;

			RED_INITIALIZE_MEMORY_POOL( PoolGPUOnion, red::memory::PoolGPU, allocOnion,		128_MB );
			{
				RED_INITIALIZE_MEMORY_POOL( PoolGPUCommandBuffer,	PoolGPUOnion, allocOnion,	32_MB );
				RED_INITIALIZE_MEMORY_POOL( PoolGPUQueryResults,	PoolGPUOnion, allocOnion,	0 );
				RED_INITIALIZE_MEMORY_POOL( PoolGPUConstantBuffers,	PoolGPUOnion, allocOnion,	10_MB );
				RED_INITIALIZE_MEMORY_POOL( PoolGPUShaderHeader,	PoolGPUOnion, allocOnion,	512_KB );
				RED_INITIALIZE_MEMORY_POOL( PoolGPUTextureStaging,	PoolGPUOnion, allocOnion,	16_MB );
				RED_INITIALIZE_MEMORY_POOL( PoolGPUBuffer_Mapped,	PoolGPUOnion, allocOnion,	64_MB );
			}
			
			RED_INITIALIZE_MEMORY_POOL( PoolGPUGarlic, red::memory::PoolGPU, allocGarlic, 2940_MB );
			{
				RED_INITIALIZE_MEMORY_POOL( PoolGPUDescriptors,	PoolGPUGarlic, allocGarlic,	16_MB );
				RED_INITIALIZE_MEMORY_POOL( PoolGPUShader,		PoolGPUGarlic, allocGarlic,	10_MB );

				RED_INITIALIZE_MEMORY_POOL( PoolGPURenderData,	PoolGPUGarlic, allocGpu,	2900_MB );
				{
					RED_INITIALIZE_MEMORY_POOL( PoolGPUSwapChain,		PoolGPURenderData, allocSwapchain, swapChainBudget );
					RED_INITIALIZE_MEMORY_POOL( PoolGPURenderTargets,	PoolGPURenderData, allocGpu,	410_MB );
					RED_INITIALIZE_MEMORY_POOL( PoolGPUTextureResources,PoolGPURenderData, allocGpu,	1200_MB );
					RED_INITIALIZE_MEMORY_POOL( PoolGPUBufferResources,	PoolGPURenderData, allocGpu,	1200_MB );
					{
						RED_INITIALIZE_MEMORY_POOL( PoolGPUBuffer_Stream,			PoolGPUBufferResources, allocGpu,	700_MB );
						RED_INITIALIZE_MEMORY_POOL( PoolGPUBuffer_RawAndStructured,	PoolGPUBufferResources, allocGpu,	400_MB );
						RED_INITIALIZE_MEMORY_POOL( PoolGPUBuffer_Indirect,			PoolGPUBufferResources, allocGpu,	16_KB );
						RED_INITIALIZE_MEMORY_POOL( PoolGPUBuffer_Typed,			PoolGPUBufferResources, allocGpu,	1_MB );
					}
				}
			}
		}

		// CPU allocations
		{
			auto& allocDefault = red::memory::AcquireDefaultAllocator();

			RED_INITIALIZE_MEMORY_POOL( PoolGpuApi, rend::PoolRendering_, allocDefault, 20_MB );
			{
				RED_INITIALIZE_MEMORY_POOL( PoolTiny,				PoolGpuApi, allocDefault, 1_KB );
				RED_INITIALIZE_MEMORY_POOL( PoolDeviceData,			PoolGpuApi, allocDefault, 12_MB );
				RED_INITIALIZE_MEMORY_POOL( PoolFetchShader,		PoolGpuApi, allocDefault, 128_KB );
				RED_INITIALIZE_MEMORY_POOL( PoolGNMdescriptors,		PoolGpuApi, allocDefault, 32_KB );
				RED_INITIALIZE_MEMORY_POOL( PoolCommandLists,		PoolGpuApi, allocDefault, 5_MB );
				RED_INITIALIZE_MEMORY_POOL( PoolResRefExtra,		PoolGpuApi, allocDefault, 1_MB );
				RED_INITIALIZE_MEMORY_POOL( PoolQueries,			PoolGpuApi, allocDefault, 0 );
				RED_INITIALIZE_MEMORY_POOL( PoolDebug,				PoolGpuApi, allocDefault, 0 );
			}
		}
	}

#elif defined( RED_PLATFORM_WINPC )

	struct GpuAllocatorDepotPC
	{
		//red::memory::GpuAllocator gpuAllocator;
	};

	GpuAllocatorDepotPC s_gpuAllocatorDepot;

	void UninitializeMemoryPoolPC()
	{
		//s_gpuAllocatorDepot.gpuAllocator.Uninitialize();
	}

	void InitializeMemoryPoolPC()
	{
#ifdef USE_NEW_SPECIALIZED_GPU_POOLS
		RED_INITIALIZE_MEMORY_POOL( PoolTextures, red::memory::PoolGPU, RED_MEGA_BYTE( 300 ) );
		RED_INITIALIZE_MEMORY_POOL( PoolMatLibTextures, red::memory::PoolGPU, RED_MEGA_BYTE( 300 ) );
		RED_INITIALIZE_MEMORY_POOL( PoolMLDataBank, red::memory::PoolGPU, RED_MEGA_BYTE( 20 ) );
		RED_INITIALIZE_MEMORY_POOL( PoolColorTargets, red::memory::PoolGPU, RED_MEGA_BYTE( 220 ) );
		RED_INITIALIZE_MEMORY_POOL( PoolDepthStencilTargets, red::memory::PoolGPU, RED_MEGA_BYTE( 50 ) );
		RED_INITIALIZE_MEMORY_POOL( PoolConstantBuffers, red::memory::PoolGPU, RED_MEGA_BYTE( 15 ) );
		RED_INITIALIZE_MEMORY_POOL( PoolResourceBuffers, red::memory::PoolGPU, RED_MEGA_BYTE( 500 ) );
		RED_INITIALIZE_MEMORY_POOL( PoolShaders, red::memory::PoolGPU, RED_MEGA_BYTE( 20 ) );
		RED_INITIALIZE_MEMORY_POOL( PoolProbes, red::memory::PoolGPU, RED_MEGA_BYTE( 50 ) );
		RED_INITIALIZE_MEMORY_POOL( PoolCommandlists, red::memory::PoolGPU, RED_MEGA_BYTE( 16 ) ); // twice that for editor build!
		RED_INITIALIZE_MEMORY_POOL( PoolQueries, red::memory::PoolGPU, RED_MEGA_BYTE( 1 ) );
		RED_INITIALIZE_MEMORY_POOL( PoolDebugGPU, red::memory::PoolGPU, RED_MEGA_BYTE( 20 ) );
#endif

		RED_INITIALIZE_MEMORY_POOL( PoolGPUTextures, red::memory::PoolGPU, red::memory::AcquireDefaultAllocator(), RED_MEGA_BYTE( 216 ) );
		RED_INITIALIZE_MEMORY_POOL( PoolGPUBuffers, red::memory::PoolGPU, red::memory::AcquireDefaultAllocator(), RED_MEGA_BYTE( 216 ) );

		/*
		red::memory::GpuAllocatorParameter gpuAllocatorParam = 
		{
			&red::memory::AcquireSystemAllocator(),
			RED_GIGA_BYTE( 8 ),		// virtual range
			RED_MEGA_BYTE( 512 ),	// default commited size
			RED_MEGA_BYTE( 16 ),	// chunk size, when commiting
			red::memory::Flags_CPU_Read_Write | red::memory::Flags_GPU_Read_Write
		};

		s_gpuAllocatorDepot.gpuAllocator.Initialize( gpuAllocatorParam );
		*/
		RED_INITIALIZE_MEMORY_POOL( PoolGPUInPlaceRenderData, red::memory::PoolGPU, red::memory::AcquireDefaultAllocator(), RED_MEGA_BYTE( 512 ) );

		const Uint32 poolTiny_size				= RED_KILO_BYTE( 1 );
		const Uint32 poolDeviceData_size		= RED_MEGA_BYTE( 52 );
		const Uint32 poolCommandList_size		= RED_MEGA_BYTE( 5 );
		const Uint32 poolCommandListCB_size		= RED_KILO_BYTE( 2 );
		const Uint32 poolResRefExtra_size		= RED_MEGA_BYTE( 1 );
		const Uint32 poolQueries_size			= RED_MEGA_BYTE( 0 ); // Currently only used for profiler
		const Uint32 poolBreadcrumbs_size		= RED_MEGA_BYTE( 1 );
		const Uint32 poolResidencyMgr_size      = RED_MEGA_BYTE( 5 );
		const Uint32 poolPSOCache_size          = RED_MEGA_BYTE( 5 );
		const Uint32 poolRaytrace_size			= RED_MEGA_BYTE( 5 );
		const Uint32 poolRenderStateCache_size	= RED_MEGA_BYTE( 1 );
		const Uint32 poolVendorAPI_size			= RED_MEGA_BYTE( 1 );
		const Uint32 poolD3D12Allocator_size	= RED_MEGA_BYTE( 2 );
		const Uint32 poolTextureSizeCache_size	= RED_KILO_BYTE( 512 );

		const Uint32 poolGpuApi_size = poolTiny_size +
			poolDeviceData_size +
			poolCommandList_size +
			poolCommandListCB_size +
			poolResRefExtra_size +
			poolQueries_size +
			poolBreadcrumbs_size +
			poolResidencyMgr_size +
			poolPSOCache_size +
			poolRenderStateCache_size +
			poolVendorAPI_size +
			poolD3D12Allocator_size +
			poolTextureSizeCache_size;

		RED_INITIALIZE_MEMORY_POOL( PoolGpuApi, rend::PoolRendering_, red::memory::AcquireDefaultAllocator(), poolGpuApi_size );
		{
			RED_INITIALIZE_MEMORY_POOL( PoolTiny,				PoolGpuApi, red::memory::AcquireDefaultAllocator(), poolTiny_size );
			RED_INITIALIZE_MEMORY_POOL( PoolDeviceData,			PoolGpuApi, red::memory::AcquireDefaultAllocator(), poolDeviceData_size );
			RED_INITIALIZE_MEMORY_POOL( PoolRenderStateCache,	PoolGpuApi, red::memory::AcquireDefaultAllocator(), poolRenderStateCache_size );
			RED_INITIALIZE_MEMORY_POOL( PoolVendorApi,			PoolGpuApi, red::memory::AcquireDefaultAllocator(), poolVendorAPI_size );
			RED_INITIALIZE_MEMORY_POOL( PoolCommandLists,		PoolGpuApi, red::memory::AcquireDefaultAllocator(), poolCommandList_size );
			RED_INITIALIZE_MEMORY_POOL( PoolCommandListCB,		PoolGpuApi, red::memory::AcquireDefaultAllocator(), poolCommandListCB_size );
			RED_INITIALIZE_MEMORY_POOL( PoolResRefExtra,		PoolGpuApi, red::memory::AcquireDefaultAllocator(), poolResRefExtra_size );
			RED_INITIALIZE_MEMORY_POOL( PoolQueries,			PoolGpuApi, red::memory::AcquireDefaultAllocator(), poolQueries_size );
			RED_INITIALIZE_MEMORY_POOL( PoolResidencyMgr,		PoolGpuApi, red::memory::AcquireDefaultAllocator(), poolResidencyMgr_size );
			RED_INITIALIZE_MEMORY_POOL( PoolPSOCache,			PoolGpuApi, red::memory::AcquireDefaultAllocator(), poolPSOCache_size );
			RED_INITIALIZE_MEMORY_POOL( PoolRaytrace,			PoolGpuApi, red::memory::AcquireDefaultAllocator(), poolRaytrace_size );
			RED_INITIALIZE_MEMORY_POOL( PoolD3D12Allocator,		PoolGpuApi, red::memory::AcquireDefaultAllocator(), poolD3D12Allocator_size );
			RED_INITIALIZE_MEMORY_POOL( PoolTextureSizeCache,	PoolGpuApi, red::memory::AcquireDefaultAllocator(), poolTextureSizeCache_size );
			RED_INITIALIZE_MEMORY_POOL( PoolBreadcrumbs,		PoolGpuApi, red::memory::AcquireDefaultAllocator(), poolBreadcrumbs_size );
#ifdef GPUAPI_USE_GPUMIRROR_POOLS
			RED_INITIALIZE_MEMORY_POOL( PoolBreadcrumbs_Markers, PoolBreadcrumbs, s_gpuFakeAllocator, 0 );
			RED_INITIALIZE_MEMORY_POOL( PoolBreadcrumbs_MarkersUsed, PoolBreadcrumbs_Markers, s_gpuFakeAllocator, 0 );

			red::memory::ForceNoDebugAllocator< PoolBreadcrumbs_Markers >();
			red::memory::ForceNoDebugAllocator< PoolBreadcrumbs_MarkersUsed >();
#endif
			RED_INITIALIZE_MEMORY_POOL( PoolDebug, PoolGpuApi, red::memory::AcquireDefaultAllocator(), 0 );
		}
	}

#endif



	void UninitializeMemoryPool()
	{
#ifdef RED_PLATFORM_DURANGO
		UninitializeMemoryPoolDurango();
#elif defined( RED_PLATFORM_ORBIS )
		UninitializeMemoryPoolOrbis();
#elif defined( RED_PLATFORM_WINPC )
		UninitializeMemoryPoolPC();
#endif
	}

	void InitializeMemoryPool()
	{
#ifdef RED_PLATFORM_DURANGO
		InitializeMemoryPoolDurango();
#elif defined( RED_PLATFORM_ORBIS )
		InitializeMemoryPoolOrbis();
#elif defined( RED_PLATFORM_WINPC )
		InitializeMemoryPoolPC();
#endif

		InitializeFakePools();
	}
}
