/**
* Copyright (c) 2017 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"
#include "renderingMemory.h"
#if !defined( VANGUARD_ASSET_HOST )
#include "renderProxy.h"
#include "renderSkinningData.h"
#include "renderScene.h"
#endif
#include "../../redMemory/src/frameAllocatorWithFallback.h"

namespace rend
{
	const Uint32 c_renderBlobAllocSizeCutOff = RED_KILO_BYTE( 64 );

	RenderBlobAllocator::RenderBlobAllocator()
	{}

	RenderBlobAllocator::~RenderBlobAllocator()
	{}

	void RenderBlobAllocator::Initialize( red::memory::DefaultAllocator* defaultAllocator, red::memory::SystemAllocator* systemAllocator )
	{
		RED_FATAL_ASSERT( defaultAllocator );
		RED_FATAL_ASSERT( systemAllocator );

		m_defaultAllocator = defaultAllocator;

		red::memory::BigSizeAllocatorParameter param =
		{
			systemAllocator,
			RED_GIGA_BYTE( 4 ),
			red::memory::Flags_CPU_Read_Write
		};

		m_bigSizeAllocator.Initialize( param );
	}

	void RenderBlobAllocator::Uninitialize()
	{
		m_bigSizeAllocator.Uninitialize();
	}

	red::memory::Block RenderBlobAllocator::Allocate( Uint32 size )
	{
		if( size < RED_KILO_BYTE( 64 ) )
		{
			return m_defaultAllocator->Allocate( size );
		}
		else
		{
			return m_bigSizeAllocator.Allocate( size );
		}
	}

	red::memory::Block RenderBlobAllocator::AllocateAligned( Uint32 size, Uint32 alignment )
	{
		if( size < c_renderBlobAllocSizeCutOff )
		{
			return m_defaultAllocator->AllocateAligned( size, alignment );
		}
		else
		{
			return m_bigSizeAllocator.AllocateAligned( size, alignment );
		}
	}

	red::memory::Block RenderBlobAllocator::Reallocate( red::memory::Block& block, Uint32 size )
	{
		if( size == 0 )
		{
			Free( block );
			return red::memory::NullBlock();
		}

		if( block.address == 0 )
		{
			return Allocate( size );
		}

		red::memory::Block newBlock = red::memory::NullBlock();

		if( m_defaultAllocator->OwnBlock( block.address ) )
		{
			if( size < c_renderBlobAllocSizeCutOff )
			{
				newBlock = m_defaultAllocator->Reallocate( block, size );
			}
			else
			{
				newBlock = m_bigSizeAllocator.Allocate( size );
				if( newBlock.address )
				{
					red::memory::MemcpyBlock( newBlock.address, block.address, GetBlockSize( block.address ) );
					m_defaultAllocator->Free( block );
				}
			}
		}
		else
		{
			if( size < c_renderBlobAllocSizeCutOff )
			{
				newBlock = m_defaultAllocator->Allocate( size );
				if( newBlock.address )
				{
					red::memory::MemcpyBlock( newBlock.address, block.address, GetBlockSize( block.address ) );
					m_bigSizeAllocator.Free( block );
				}
			}
			else
			{
				newBlock = m_bigSizeAllocator.Reallocate( block, size );
			}
		}

		return newBlock;
	}

	red::memory::Block RenderBlobAllocator::ReallocateAligned( red::memory::Block& block, Uint32 size, Uint32 alignment )
	{
		if( size == 0 )
		{
			Free( block );
			return red::memory::NullBlock();
		}

		if( block.address == 0 )
		{
			return AllocateAligned( size, alignment );
		}

		red::memory::Block newBlock = red::memory::NullBlock();

		if( m_defaultAllocator->OwnBlock( block.address ) )
		{
			if( size < c_renderBlobAllocSizeCutOff  )
			{
				newBlock = m_defaultAllocator->ReallocateAligned( block, size, alignment );
			}
			else
			{
				newBlock = m_bigSizeAllocator.AllocateAligned( size, alignment );
				if( newBlock.address )
				{
					red::memory::MemcpyBlock( newBlock.address, block.address, GetBlockSize( block.address ) );
					m_defaultAllocator->Free( block );
				}
			}
		}
		else
		{
			if( size < c_renderBlobAllocSizeCutOff )
			{
				newBlock = m_defaultAllocator->AllocateAligned( size, alignment );
				if( newBlock.address )
				{
					red::memory::MemcpyBlock( newBlock.address, block.address, GetBlockSize( block.address ) );
					m_bigSizeAllocator.Free( block );
				}
			}
			else
			{
				newBlock = m_bigSizeAllocator.ReallocateAligned( block, size, alignment );
			}
		}

		return newBlock;
	}

	void RenderBlobAllocator::Free( red::memory::Block& block )
	{
		if( m_defaultAllocator->OwnBlock( block.address ) )
		{
			m_defaultAllocator->Free( block );
		}
		else
		{
			m_bigSizeAllocator.Free( block );
		}
	}

	bool RenderBlobAllocator::OwnBlock( Uint64 block ) const
	{
		return m_defaultAllocator->OwnBlock( block ) || m_bigSizeAllocator.OwnBlock( block );
	}

	Uint64 RenderBlobAllocator::GetBlockSize( Uint64 address ) const
	{
		if( m_defaultAllocator->OwnBlock( address ) )
		{
			return m_defaultAllocator->GetBlockSize( address );
		}
		else
		{
			return m_bigSizeAllocator.GetBlockSize( address );
		}
	}
}

namespace
{
	const Uint32 s_frameAllocatorBudget = RED_MEGA_BYTE( 4 );
	const Uint32 s_doubleFrameAllocatorBudget = RED_KILO_BYTE( 256 );

	static red::memory::FrameAllocatorWithFallback s_frameAllocator;
	static red::memory::FrameAllocatorWithFallback s_doubleBufferedFrameAllocator;
#ifdef RED_PLATFORM_CONSOLE
	static rend::RenderBlobAllocator s_renderBlobAllocator; // ctremblay: Only for console. On Editor & cmdlet, we keep all buffer alive forever.
#endif
}


void rend::InitializeMemoryPools()
{
#define INIT_POOL RED_INITIALIZE_MEMORY_POOL
#define FIXED_SIZE(proxy) proxy.AcquireAllocator(), proxy.c_maxSize

	auto& systemAllocator = red::memory::AcquireSystemAllocator();
	auto& defaultAllocator = red::memory::AcquireDefaultAllocator();
	auto& nullAllocator = red::memory::AcquireNullAllocator();
	auto& bigSizeAllocator = red::memory::AcquireBigSizeAllocator();
	{
		red::memory::FrameAllocatorParameter param =
		{
			&systemAllocator,
			s_frameAllocatorBudget,
			1,
			red::memory::Flags_CPU_Read_Write
		};

		s_frameAllocator.Initialize( param );

		red::memory::FrameAllocatorParameter doubleBufferedFrameAllocatorParam =
		{
			&systemAllocator,
			s_doubleFrameAllocatorBudget,
			2,
			red::memory::Flags_CPU_Read_Write
		};

		s_doubleBufferedFrameAllocator.Initialize( doubleBufferedFrameAllocatorParam );
	
#ifdef RED_PLATFORM_CONSOLE
		s_renderBlobAllocator.Initialize( &defaultAllocator, &systemAllocator );
#endif
	}

	//////////////////////////////////////////////////////////////////////////
	// ENGINE SIDE POOLS
	//////////////////////////////////////////////////////////////////////////

	INIT_POOL( PoolTexture,			red::PoolResource, defaultAllocator,	512_MB );
	INIT_POOL( PoolMesh,			red::PoolResource, defaultAllocator,	128_MB );
	INIT_POOL( PoolBink,			red::PoolResource, defaultAllocator,	64_MB );
	INIT_POOL( PoolFont,			red::memory::PoolUI, defaultAllocator,	15_MB );
	{
		INIT_POOL( PoolFreeType,		PoolFont, defaultAllocator,				5_MB );
		INIT_POOL( PoolFontCache,		PoolFont, defaultAllocator,				256_KB );
		INIT_POOL( PoolGlyphCache,		PoolFont, defaultAllocator,				256_KB );
	}

#ifdef RED_USE_IMGUI
	INIT_POOL( PoolImGui,			red::PoolDebug, defaultAllocator,			16_MB );
#endif
	INIT_POOL( PoolRenderDebug,		red::PoolDebug, defaultAllocator,		16_MB );
	{
		INIT_POOL( PoolGIDebug,			PoolRenderDebug, defaultAllocator,		0 );
		INIT_POOL( PoolGPUProfiling,	PoolRenderDebug, defaultAllocator,		0 );
	}

	//////////////////////////////////////////////////////////////////////////
	// RENDERING POOLS
	//////////////////////////////////////////////////////////////////////////
	// Main pool, parent to all rendering pools.
	INIT_POOL( PoolRendering_, red::memory::PoolCPU, nullAllocator, 285_MB );
	{
		INIT_POOL( PoolFrameRendering,					PoolRendering_, s_frameAllocator,			s_frameAllocatorBudget );
		INIT_POOL( PoolDoubleBufferedFrameRendering,	PoolRendering_, s_doubleBufferedFrameAllocator,	s_doubleFrameAllocatorBudget * 2 );
		INIT_POOL( PoolRenderingCore,					PoolRendering_, defaultAllocator,			8_KB			);
		INIT_POOL( PoolSurfaceCache,					PoolRendering_, defaultAllocator,			1_MB			);
		INIT_POOL( PoolShaderCache,						PoolRendering_, defaultAllocator,			30_MB			);
		{
			INIT_POOL( PoolShaderCacheShaders,				PoolShaderCache, defaultAllocator,			128_KB		);
			INIT_POOL( PoolShaderCacheData,					PoolShaderCache, defaultAllocator,			3_MB		);
			INIT_POOL( PoolShaderCompilation,				PoolShaderCache, defaultAllocator,			0			);
			INIT_POOL( PoolPSO,								PoolShaderCache, defaultAllocator,			8_KB		);
			INIT_POOL( PoolShaderCacheData_ShaderBinaries,	PoolShaderCache, defaultAllocator,			26_MB		);
		}
		INIT_POOL( PoolRenderProxy,						PoolRendering_, defaultAllocator,			30_MB			);
		{
			INIT_POOL( PoolRenderProxyInternals_Mesh,		PoolRenderProxy, defaultAllocator,			5_MB		);
			INIT_POOL( PoolRenderProxy_CustomData,			PoolRenderProxy, defaultAllocator,			1_MB		);
			INIT_POOL( PoolRenderProxy_InstanceData,		PoolRenderProxy, defaultAllocator,			2_MB		);
			INIT_POOL( PoolRenderProxy_Handles,				PoolRenderProxy, defaultAllocator,			2_MB		);
		}
		INIT_POOL( PoolRenderResources,					PoolRendering_, defaultAllocator,			15_MB			);
		{
			INIT_POOL( PoolRenderMesh,						PoolRenderResources, defaultAllocator,		8_MB		);
			INIT_POOL( PoolRenderDynamicTexture,			PoolRenderResources, defaultAllocator,		8_KB		);
		}
		INIT_POOL( PoolMaterials,						PoolRendering_, defaultAllocator,			10_MB			);
		INIT_POOL( PoolRenderMaterial,					PoolRendering_, defaultAllocator,			25_MB			);
		{
			INIT_POOL( PoolMaterialTechniques,				PoolRenderMaterial, defaultAllocator,		512_KB		);
			INIT_POOL( PoolMaterialStagePassData,			PoolRenderMaterial, defaultAllocator,		1_MB		);
			INIT_POOL( PoolMaterialStagePassCache,			PoolRenderMaterial, defaultAllocator,		10_MB		);
			INIT_POOL( PoolRenderMaterialParams,			PoolRenderMaterial, defaultAllocator,		6_MB		);
			INIT_POOL( PoolMaterialExtractedData,			PoolRenderMaterial, defaultAllocator,		1_MB		);
			INIT_POOL( PoolMaterialModifier,				PoolRenderMaterial, defaultAllocator,		64_KB		);
		}
		INIT_POOL( PoolDecals,							PoolRendering_, defaultAllocator,			2_MB			);
		INIT_POOL( PoolParticleData,					PoolRendering_, defaultAllocator,			15_MB			);
		INIT_POOL( PoolRenderGraph,						PoolRendering_, defaultAllocator,			512_KB			);
		INIT_POOL( PoolFlowAllocator,					PoolRendering_, defaultAllocator,			4_MB			);
		INIT_POOL( PoolVideoPlayer,						PoolRendering_, defaultAllocator,			32_KB			);
		INIT_POOL( PoolRenderData,						PoolRendering_, defaultAllocator,			5_MB			);
		INIT_POOL( PoolGI,								PoolRendering_, defaultAllocator,			64_KB			);
		INIT_POOL( PoolRenderBlobs,						PoolRendering_, defaultAllocator,			10_MB			);
#ifdef RED_PLATFORM_CONSOLE
			INIT_POOL( PoolRenderBlobsBuffers, PoolRenderBlobs, s_renderBlobAllocator, 1_MB );
#else 
			INIT_POOL( PoolRenderBlobsBuffers, PoolRenderBlobs, defaultAllocator, 1_MB );
#endif

		INIT_POOL( PoolRenderScene,						PoolRendering_, defaultAllocator,			15_MB			);
		{
			INIT_POOL( PoolRenderScene_Proxies,				PoolRenderScene, defaultAllocator,			3_MB		);
			INIT_POOL( PoolRenderScene_ProxyData,			PoolRenderScene, defaultAllocator,			3_MB		);
			INIT_POOL( PoolRenderScene_PendingOps,			PoolRenderScene, defaultAllocator,			3_MB		);
			INIT_POOL( PoolRenderScene_RuntimeSystem,		PoolRenderScene, defaultAllocator,			3_MB		);
		}
		INIT_POOL( PoolVisBVH,							PoolRendering_, defaultAllocator,			5_MB			);
		INIT_POOL( PoolSkinningData,					PoolRendering_, defaultAllocator,			2_MB			);
		INIT_POOL( PoolFrameInfo,						PoolRendering_, defaultAllocator,			0				);
		INIT_POOL( PoolFoliage,							PoolRendering_, defaultAllocator,			3_MB			);
		INIT_POOL( PoolBatchers,						PoolRendering_, defaultAllocator,			10_MB			);
		INIT_POOL( PoolRenderCommandHandler,			PoolRendering_, defaultAllocator,			4_MB			);
		INIT_POOL( PoolPostProcessAndEffects,			PoolRendering_, defaultAllocator,			128_KB			);
		INIT_POOL( PoolEnvironment,						PoolRendering_, defaultAllocator,			3_MB/2			); // 1.5MB
		INIT_POOL( PoolImageProcessing,					PoolRendering_, defaultAllocator,			0				);
		INIT_POOL( PoolRenderingUI,						PoolRendering_, defaultAllocator,			2_MB			);
		INIT_POOL( PoolReflectionProbes,				PoolRendering_, defaultAllocator,			32_KB			);
		INIT_POOL( PoolLighting,						PoolRendering_, defaultAllocator,			1_MB			);
		INIT_POOL( PoolDataRegistry,					PoolRendering_, defaultAllocator,			8_KB			);
		INIT_POOL( PoolCameras,							PoolRendering_, defaultAllocator,			1_MB			);

#ifdef RED_PLATFORM_WINPC
		INIT_POOL( PoolRaytrace,						PoolRendering_, defaultAllocator,			8_MB			);
#else
		INIT_POOL( PoolRaytrace,						PoolRendering_, defaultAllocator,			0				);
#endif

		INIT_POOL( PoolVisibilitySystem,				PoolRendering_, defaultAllocator,			22_MB			);
		{
			INIT_POOL( PoolVisibilityScene,					PoolVisibilitySystem, defaultAllocator,		20_MB		);
			INIT_POOL( PoolOccluderShapes,					PoolVisibilitySystem, defaultAllocator,		2_MB		);
		}
	}

#undef INIT_POOL
#undef FIXED_SIZE
}
