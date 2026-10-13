/**
* Copyright (c) 2017 CD Projekt Red. All Rights Reserved.
*/

#pragma once

#include "../../redMemory/include/defaultAllocator.h"
#include "../../redMemory/include/linearAllocator.h"
#include "../../redMemory/src/dynamicFixedSizeAllocator.h"
#include "renderDataApi.h"

namespace rend
{
	struct RenderBlobAllocatorMetrics
	{
	};

	class RenderBlobAllocator
	{
	public:
		RED_MEMORY_DECLARE_ALLOCATOR( RenderBlobAllocator, RenderBlobAllocatorMetrics, 8 );

		RenderBlobAllocator();
		~RenderBlobAllocator();

		void Initialize( red::memory::DefaultAllocator* defaultAllocator, red::memory::SystemAllocator* systemAllocator );
		void Uninitialize();

		red::memory::Block Allocate( Uint32 size );
		red::memory::Block AllocateAligned( Uint32 size, Uint32 alignment );
		red::memory::Block Reallocate( red::memory::Block& block, Uint32 size );
		red::memory::Block ReallocateAligned( red::memory::Block& block, Uint32 size, Uint32 alignment );
		void Free( red::memory::Block& block );

		bool OwnBlock( Uint64 block ) const;
		Uint64 GetBlockSize( Uint64 address ) const;

	private:

		red::memory::DefaultAllocator* m_defaultAllocator;
		red::memory::BigSizeAllocator m_bigSizeAllocator;
	};


	// Engine side pools
	RED_MEMORY_POOL( PoolTexture, red::memory::DefaultAllocator, RENDER_DATA_API );
	RED_MEMORY_POOL( PoolMesh, red::memory::DefaultAllocator, RENDER_DATA_API ); // Engine/renderData stuff. Should renderData stuff get moved to PoolRenderMesh?
	RED_MEMORY_POOL( PoolBink, red::memory::DefaultAllocator, RENDER_DATA_API );
	RED_MEMORY_POOL( PoolFont, red::memory::DefaultAllocator, RENDER_DATA_API );
	RED_MEMORY_POOL( 	PoolFreeType, red::memory::DefaultAllocator, RENDER_DATA_API );
	RED_MEMORY_POOL( 	PoolFontCache, red::memory::DefaultAllocator, RENDER_DATA_API );
	RED_MEMORY_POOL( 	PoolGlyphCache, red::memory::DefaultAllocator, RENDER_DATA_API );

#ifdef RED_USE_IMGUI
	RED_MEMORY_POOL( PoolImGui, red::memory::DefaultAllocator, RENDER_DATA_API );
#endif

	RED_MEMORY_POOL( PoolRenderDebug, red::memory::DefaultAllocator, RENDER_DATA_API );

	// These are only CPU memory pools. For GPU memory pools. look up gpuApiMemory.h
	//////////////////////////////////////////////////////////////////////////

	RED_MEMORY_POOL( PoolRendering_, red::memory::NullAllocator, RENDER_DATA_API );
	RED_MEMORY_POOL(	PoolFrameRendering, red::memory::FrameAllocatorWithFallback, RENDER_DATA_API ); // PoolFrame, but usable in renderer
	RED_MEMORY_POOL(	PoolDoubleBufferedFrameRendering, red::memory::FrameAllocatorWithFallback, RENDER_DATA_API ); // PoolDoubleBufferedFrame, but usable in renderer
	RED_MEMORY_POOL(	PoolRenderingCore, red::memory::DefaultAllocator, RENDER_DATA_API ); // Basic system/manager type things that generally have single allocation and don't fit elsewhere
	RED_MEMORY_POOL(	PoolSurfaceCache, red::memory::DefaultAllocator, RENDER_DATA_API );
	RED_MEMORY_POOL(	PoolShaderCache, red::memory::DefaultAllocator, RENDER_DATA_API );
	RED_MEMORY_POOL(		PoolShaderCacheShaders, red::memory::DefaultAllocator, RENDER_DATA_API );
	RED_MEMORY_POOL(		PoolShaderCacheData, red::memory::DefaultAllocator, RENDER_DATA_API );
	RED_MEMORY_POOL(		PoolShaderCompilation, red::memory::DefaultAllocator, RENDER_DATA_API );
	RED_MEMORY_POOL(		PoolPSO, red::memory::DefaultAllocator, RENDER_DATA_API );
	RED_MEMORY_POOL(		PoolShaderCacheData_ShaderBinaries, red::memory::DefaultAllocator, RENDER_DATA_API );
	RED_MEMORY_POOL(	PoolRenderProxy, red::memory::DefaultAllocator, RENDER_DATA_API );
	RED_MEMORY_POOL(		PoolRenderProxyInternals_Mesh, red::memory::DefaultAllocator, RENDER_DATA_API );
	RED_MEMORY_POOL(		PoolRenderProxy_CustomData, red::memory::DefaultAllocator, RENDER_DATA_API );
	RED_MEMORY_POOL(		PoolRenderProxy_InstanceData, red::memory::DefaultAllocator, RENDER_DATA_API );
	RED_MEMORY_POOL(		PoolRenderProxy_Handles, red::memory::DefaultAllocator, RENDER_DATA_API );
	RED_MEMORY_POOL(	PoolRenderResources, red::memory::DefaultAllocator, RENDER_DATA_API );
	RED_MEMORY_POOL(		PoolRenderMesh, red::memory::DefaultAllocator, RENDER_DATA_API );
	RED_MEMORY_POOL(		PoolRenderDynamicTexture, red::memory::DefaultAllocator, RENDER_DATA_API );
	RED_MEMORY_POOL(	PoolMaterials, red::memory::DefaultAllocator, RENDER_DATA_API );
	RED_MEMORY_POOL(	PoolRenderMaterial, red::memory::DefaultAllocator, RENDER_DATA_API );
	RED_MEMORY_POOL(		PoolMaterialTechniques, red::memory::DefaultAllocator, RENDER_DATA_API );
	RED_MEMORY_POOL(		PoolMaterialStagePassData, red::memory::DefaultAllocator, RENDER_DATA_API );
	RED_MEMORY_POOL(		PoolMaterialStagePassCache, red::memory::DefaultAllocator, RENDER_DATA_API );
	RED_MEMORY_POOL(		PoolRenderMaterialParams, red::memory::DefaultAllocator, RENDER_DATA_API );
	RED_MEMORY_POOL(		PoolMaterialExtractedData, red::memory::DefaultAllocator, RENDER_DATA_API );
	RED_MEMORY_POOL(		PoolMaterialModifier, red::memory::DefaultAllocator, RENDER_DATA_API );
	RED_MEMORY_POOL(	PoolDecals, red::memory::DefaultAllocator, RENDER_DATA_API );
	RED_MEMORY_POOL(	PoolParticleData, red::memory::DefaultAllocator, RENDER_DATA_API );
	RED_MEMORY_POOL(	PoolRenderGraph, red::memory::DefaultAllocator, RENDER_DATA_API );
	RED_MEMORY_POOL(	PoolFlowAllocator, red::memory::DefaultAllocator, RENDER_DATA_API );
	RED_MEMORY_POOL(	PoolVideoPlayer, red::memory::DefaultAllocator, RENDER_DATA_API );
	RED_MEMORY_POOL(	PoolRenderData, red::memory::DefaultAllocator, RENDER_DATA_API ); // #TODO: Probably get rid of this, it doesn't really say much
	RED_MEMORY_POOL(	PoolGI, red::memory::DefaultAllocator, RENDER_DATA_API );
	RED_MEMORY_POOL(	PoolGIDebug, red::memory::DefaultAllocator, RENDER_DATA_API );
	RED_MEMORY_POOL(	PoolRenderBlobs, red::memory::DefaultAllocator, RENDER_DATA_API );
#ifdef RED_PLATFORM_CONSOLE
	RED_MEMORY_POOL(		PoolRenderBlobsBuffers, RenderBlobAllocator, RENDER_DATA_API );
#else
	RED_MEMORY_POOL( PoolRenderBlobsBuffers, red::memory::DefaultAllocator, RENDER_DATA_API );
#endif
	RED_MEMORY_POOL(	PoolRenderScene, red::memory::DefaultAllocator, RENDER_DATA_API );
	RED_MEMORY_POOL(		PoolRenderScene_Proxies, red::memory::DefaultAllocator, RENDER_DATA_API );
	RED_MEMORY_POOL(		PoolRenderScene_ProxyData, red::memory::DefaultAllocator, RENDER_DATA_API );
	RED_MEMORY_POOL(		PoolRenderScene_PendingOps, red::memory::DefaultAllocator, RENDER_DATA_API );
	RED_MEMORY_POOL(		PoolRenderScene_RuntimeSystem, red::memory::DefaultAllocator, RENDER_DATA_API );
	RED_MEMORY_POOL(	PoolVisBVH, red::memory::DefaultAllocator, RENDER_DATA_API );
	RED_MEMORY_POOL(	PoolSkinningData, red::memory::DefaultAllocator, RENDER_DATA_API );
	RED_MEMORY_POOL(	PoolFrameInfo, red::memory::DefaultAllocator, RENDER_DATA_API );
	RED_MEMORY_POOL(	PoolFoliage, red::memory::DefaultAllocator, RENDER_DATA_API ); // trees, wind, autospawn
	RED_MEMORY_POOL(	PoolGPUProfiling, red::memory::DefaultAllocator, RENDER_DATA_API );
	RED_MEMORY_POOL(	PoolBatchers, red::memory::DefaultAllocator, RENDER_DATA_API );
	RED_MEMORY_POOL(	PoolRenderCommandHandler, red::memory::DefaultAllocator, RENDER_DATA_API );
	RED_MEMORY_POOL(	PoolPostProcessAndEffects, red::memory::DefaultAllocator, RENDER_DATA_API ); // postfx, gameplay effects, misc geometry stuff
	RED_MEMORY_POOL(	PoolEnvironment, red::memory::DefaultAllocator, RENDER_DATA_API ); // ? environment definition, area params, interior volumes, fog...
	RED_MEMORY_POOL(	PoolImageProcessing, red::memory::DefaultAllocator, RENDER_DATA_API ); // compress/decompress, frame grabs, selection, etc
	RED_MEMORY_POOL(	PoolRenderingUI, red::memory::DefaultAllocator, RENDER_DATA_API ); // In-game UI, draw buffers, etc
	RED_MEMORY_POOL(	PoolReflectionProbes, red::memory::DefaultAllocator, RENDER_DATA_API );
	RED_MEMORY_POOL(	PoolLighting, red::memory::DefaultAllocator, RENDER_DATA_API ); // Lights, shadows
	RED_MEMORY_POOL(	PoolDataRegistry, red::memory::DefaultAllocator, RENDER_DATA_API );
	RED_MEMORY_POOL(	PoolCameras, red::memory::DefaultAllocator, RENDER_DATA_API );
	RED_MEMORY_POOL(	PoolRaytrace, red::memory::DefaultAllocator, RENDER_DATA_API );
	RED_MEMORY_POOL(	PoolVisibilitySystem, red::memory::DefaultAllocator, RENDER_DATA_API );
	RED_MEMORY_POOL(		PoolVisibilityScene, red::memory::DefaultAllocator, RENDER_DATA_API );
	RED_MEMORY_POOL(		PoolOccluderShapes, red::memory::DefaultAllocator, RENDER_DATA_API );

	void InitializeMemoryPools();
}
