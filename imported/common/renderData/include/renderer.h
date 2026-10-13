/**
* Copyright (c) 2007 CD Projekt Red. All Rights Reserved.
*/
#pragma once

#include "gpuDataProcessor.h"
#include "visibilityOccluderInstance.h"
#include "../../../common/redContainers/include/map.h"
#include "renderScene.h"
#include "renderingMemory.h"
#include "renderFrameInfo.h"

#ifdef RED_PLATFORM_WINPC
#define RENDER_ENABLE_RESOURCE_EVICT
#endif

class IRenderEngineHandler;
class IRenderCommandHandler;
class IMaterialDataProvider;
class IMaterialParametersProvider;
class IRenderFloatTrackData;

struct DynamicTextureDesc;
struct DynamicVolumeTextureDesc;

enum EViewportWindowMode : Uint8;
using ViewportHandle = red::SharedPtr< IViewport >;

namespace rend
{
	struct SkinProfileData;
	struct HairProfileData;
	struct GradientData;
	struct FoliageProfileData;
	struct TextureRegionSet;
	struct MultilayerSetupData;
	struct AutoFoliageRuntimeMap;
	struct TerrainMaterialData;
}
struct VideoParams;
class IRenderVideo;

namespace rend
{
	class IIndexBufferDataProvider;
	class IDebugDrawer;
	class DebugFilterMask;
	class GameplayEffects;
	struct DebugStats;
}

namespace GpuApi
{
	struct MemoryStats;
}

#ifndef RED_CONFIGURATION_FINAL
namespace GI
{
	class SSectorStats;
}
#endif //  RED_CONFIGURATION_FINAL


struct RENDER_DATA_API GeneralStats
{
	Uint32 m_characterMemory;
	Uint32 m_environmentMemory;

	GeneralStats()
		: m_characterMemory( 0 )
		, m_environmentMemory( 0 )
	{}

	GeneralStats( Uint32 charMem, Uint32 envMem )
		: m_characterMemory( charMem )
		, m_environmentMemory( envMem )
	{}

	void ResetStats()
	{
		m_characterMemory = 0;
		m_environmentMemory = 0;
	}
};

struct SkinningStats : red::NonCopyable
{
    static constexpr Uint32 MAX_CATEGORIES = 16;
    struct Category
    {
        CName name = CName::NONE();
        Uint32 quantity = 0;
    };

	Uint32 m_numRegisteredSkinningBuffers  = 0;
	Uint32 m_numActiveSkinningBuffers = 0;
	Uint32 m_skinningBufferUsage = 0;
	Uint32 m_skinningDataMemorySpace = 0;
    Bool m_categoriesOverflow = false;
    red::StaticArray<Category, MAX_CATEGORIES> m_categories;
	red::ArraySpan<const Uint8> m_memoryMapHistogram;
};

struct PreviousFrameState
{
	enum State : Uint8
	{
		NONE,
		LOW,
		HIGH,
		CRITICAL
	};

	// batch data allocator
	State m_wasBatchDataGeometryOverflow{ NONE };
	State m_wasBatchDataPsoOverflow{ NONE };
	State m_wasBatchDataParamsOverflow{ NONE };
	State m_wasBatchDataProxyOverflow{ NONE };
	State m_wasBatchDataInstanceOverflow{ NONE };

	State m_wasDynamicInstanceBufferOverflow{ NONE };
	State m_wasDynamicSkinnedInstanceBufferOverflow{ NONE };
	State m_wasGITetrahedronBufferOverflow{ NONE };
	State m_wasStaticInstanceBufferOverflow{ NONE };
	State m_wasRenderStageChunkAllocatorOverflow{ NONE };
	State m_wasDecalOverflow{ NONE };
	State m_wasEnvProbeOverflow{ NONE };
	State m_wasLocalShadowsOverflow { NONE };
	State m_wasLocalShadowsQueueOverflow { NONE };
	State m_wasLocalLightContactShadowsOverflow{ NONE };
	State m_wasDebugOverflow{ NONE };
	State m_wasGlyphCacheOverflow{ NONE };
	State m_wasLightsAllocationOverflow{ NONE };
	State m_wasParticleEmitterOverflow{ NONE };
	State m_wasParticleAmountOverflow{ NONE };
	State m_wasVideoUploadOverflow{ NONE };
	State m_wasSingleVideoUploadOverflow{ NONE };
	State m_wasDPLTextureOverflown{ NONE };
	State m_wasFogVolumeOverflown{ NONE };
	State m_wasRaytraceOverflow{ NONE };
	State m_wasMultilayerProxyMaterialOverflow{ NONE };
	State m_wasDynamicCameraOverflow{ NONE };
	State m_wasLightChannelsOverflow{ NONE };
	State m_wasReflectionsUberQualityEnabled{ NONE }; // It's not an issue by itself, but the developer must be aware it's enabled
	Bool m_wasUploadOverflow { false };

	RED_INLINE Bool IsAnyOverflow() const
	{
		return m_wasBatchDataGeometryOverflow ||
			m_wasBatchDataPsoOverflow ||
			m_wasBatchDataParamsOverflow ||
			m_wasBatchDataProxyOverflow ||
			m_wasBatchDataInstanceOverflow ||
			m_wasDynamicInstanceBufferOverflow ||
			m_wasDynamicSkinnedInstanceBufferOverflow ||
			m_wasGITetrahedronBufferOverflow ||
			m_wasStaticInstanceBufferOverflow ||
			m_wasRenderStageChunkAllocatorOverflow ||
			m_wasDecalOverflow ||
			m_wasEnvProbeOverflow ||
			m_wasLocalShadowsOverflow ||
			m_wasLocalShadowsQueueOverflow ||
			m_wasLocalLightContactShadowsOverflow ||
			m_wasDebugOverflow ||
			m_wasGlyphCacheOverflow ||
			m_wasLightsAllocationOverflow ||
			m_wasParticleEmitterOverflow ||
			m_wasParticleAmountOverflow ||
			m_wasVideoUploadOverflow ||
			m_wasSingleVideoUploadOverflow ||
			m_wasDPLTextureOverflown ||
			m_wasFogVolumeOverflown ||
			m_wasRaytraceOverflow ||
			m_wasMultilayerProxyMaterialOverflow ||
			m_wasDynamicCameraOverflow || 
			m_wasLightChannelsOverflow ||
			m_wasReflectionsUberQualityEnabled ||
			m_wasUploadOverflow;
	}

	Bool m_didImmediateCompilePSO{false};			// Did we compile any PSOs on demand during render?
	Bool m_wasMissingPSO{ false };					// Did we fail to get a PSO during render because it wasn't precompiled?
};

#if 0
struct RENDER_DATA_API SrtUserData
{
	enum EColorType
	{
		CT_Off,
		CT_Grass,
		CT_Branch,
		CT_Tree
	};

	static String ToString( EColorType val )
	{
		if ( val == CT_Off )
			return "Off";
		else if ( val == CT_Grass )
			return "Grass";
		else if ( val == CT_Branch )
			return "Branch";
		else if ( val == CT_Tree )
			return "Tree";

		return String::EMPTY();
	}

	EColorType m_billboardColorType;
	red::DynArray< String > m_materials { rend::PoolFoliage() };
	red::DynArray< EColorType > m_materialColorType { rend::PoolFoliage() };

	Bool m_grassRandomColorEnabled;
	Bool m_grassTerrainNormalsEnabled;

	Bool m_interactiveEnabled;
};
#endif

struct SRenderDataSource
{
	const res::ResourcePath		m_resourcePath;
	TRenderPtr<IRenderResource>	m_existing;
};

struct RenderPipelineStats
{
	Uint64 VerticesRead = 0;
	Uint64 PrimitivesRead = 0;
	Uint64 VertexShaderInvocations = 0;
	Uint64 GeometryShaderInvocations = 0;
	Uint64 PrimitivesOutputByGeometryShader = 0;
	Uint64 PrimitivesSentToRasterizer = 0;
	Uint64 PrimitivesRendered = 0;
	Uint64 PixelShaderInvocations = 0;
	Uint64 HullShaderInvocations = 0;
	Uint64 DomainShaderInvocations = 0;
	Uint64 ComputeShaderInvocations = 0;
};

struct IRenderGpuProfiler
{
	RED_USE_MEMORY_POOL( rend::PoolGPUProfiling );

	struct Sample
	{
		CName	m_name;
		Uint64	m_start;
		Uint64	m_end;			// #TODO: Uint32 length?
		Uint64	m_startCpu;
		Uint32	m_lengthCpu;
		Uint32	m_id;
		Uint8	m_thread;
		Uint8	m_padding[7];
		RenderPipelineStats m_pipelineStats;
	};

	RED_STATIC_ASSERT( sizeof( Sample ) == 136 );

	virtual ~IRenderGpuProfiler()
	{
	}

	virtual void SetCollectPipelineStats( Bool enable ) = 0;

	virtual Uint32 AddEntry( CName scopeName ) = 0;
	virtual Uint32 StartEntry( Uint32 scopeId ) = 0;
	virtual void EndEntry( Uint32 entryId ) = 0;

	virtual Float GetLastGpuFrameTime() const = 0;

	virtual Uint32 GetNumFramesReady() const = 0;

	virtual void ProcessQueries() = 0;

	virtual void GetLastFrame( Uint64 &frameStart, Uint64 &frameEnd ) = 0;
	virtual Uint32 CopyLastFrameSamples( Sample *buffer0, Uint32 buffer0Size, Sample *buffer1, Uint32 buffer1Size ) = 0;
	virtual void ConsumeLastFrame() = 0;

	virtual Uint32 GetNumGpuThreads() = 0;
	virtual const AnsiChar* GetGpuThreadName( Uint32 gpuThread ) = 0;
	virtual void GetCpuGpuSync( Uint32 gpuThread, Uint64& syncCpu, Uint64& syncGpu, Uint64& gpuFrequency ) = 0;
};


namespace renderDev
{
	struct SConfig;
	class ICameraSpy;
}

struct SDynamicResolutionSetup
{
	Uint32 heightMin = 0;
	Uint32 heightMax = 0;
	Uint32 targetHeight = 0;
	Uint32 targetFPS = 0;
	Bool enabled = false;
	Bool IsValid() const { return targetHeight > 0 && heightMax > 0 && heightMin > 0 && heightMax >= heightMin && targetFPS > 0 && enabled; }
};

#ifdef USE_PROFILER

struct ResolutionHistoryEntry
{
	Float targetFrameTime = 0.0f;
	Float mainSceneTime = 0.0f;
	Float subsceneTime = 0.0f;
	Float drawBuffersTime = 0.0f;
	Uint16 currentHeight = 0;
	Uint16 minHeight = 0;
	Uint16 maxHeight = 0;
};

#endif // USE_PROFILER

/// Base rendering interface
class RENDER_DATA_API IRender : public IGpuDataProcessor
{
	RED_USE_MEMORY_POOL( rend::PoolRenderingCore );

public:
	IRender();
	virtual ~IRender();

public:
	virtual void Register( red::UniquePtr< IRenderEngineHandler >&& engineHandler );

	// Initialize renderer
	virtual Bool Init();

	// Update internal state
	virtual void Tick( Float timeDelta, Float timeScale, Float lastCpuFrameTime = 0.0f );

	virtual Uint32 GetLastTickCounter() const { return 0; }

	// Get last frame delta time
	virtual Float GetLastFrameTime() const { return 1.0f / 30.0f; }

	// Is renderer ready, so we can move on and tick game?
	virtual Bool PrepareRenderer();
	virtual Bool IsRaytracingAvaible() { return false; };


	// Make the GPU crash :)
	virtual void RequestGpuHang() {}


	//! Flush rendering command queue
	virtual void Flush();

	virtual void Sync( job::Builder& sync );

#ifdef RENDER_ENABLE_RESOURCE_EVICT
	virtual void RequestEvictOnNextSceneRender();
#endif

	virtual void Unsafe_DebugDumpRenderCommandQueue();

	virtual void LoadConfig();

public:
	// Reload all textures ( slow )
	virtual void ReloadTextures();

	// Reload engine shaders ( material graphs )
	virtual void ReloadEngineShaders();

	// Reload simple shaders
	virtual void FlushReloadSimpleShaders();

	// Enqueue simple shaders reload
	virtual void EnqueueReloadSimpleShaders();

	// Recreate platform dependent resources
	virtual void RecreatePlatformResources();

	// Recreate material resources
	virtual void RecalculateTextureStreamingSettings();

	// This does some magic on relinking and setupping basic renderer shaders
	virtual void RelinkSimpleShaders();

public:
	// Initialize the renderer without SwapChain and Viewport for commandlets
	virtual Bool InitWithoutViewport( Uint32 width, Uint32 height, EViewportWindowMode windowMode, Bool vsync = false );
	// Interface
	virtual ViewportHandle CreateViewport( void* TopLevelWindow, void* ParentWindow, const red::String& title, Uint32 width, Uint32 height, EViewportWindowMode windowMode, Bool vsync, Int32 outputMonitorIdx, Int32 hdrMode );

	// Create game viewport, window will be owned by renderer
	virtual ViewportHandle CreateGameViewport( const red::String& title, Uint32 width, Uint32 height, EViewportWindowMode windowMode, Int32 hdrMode, Int32 monitorIdx );

	// Request a resize/recreate of render surfaces
	virtual void RequestResizeRenderSurfaces( Uint32 width, Uint32 height );

public:
	// Create renderer resource for mesh blob
	virtual TRenderPtr<IRenderResource> UploadBlobMesh( const SRenderDataSource& src, const THandle< IRenderResourceBlob  > blob );

	// Create renderer resource for texture blob
	virtual TRenderPtr<IRenderResource> UploadBlobTexture( const SRenderDataSource& src, const THandle< IRenderResourceBlob  > blob, const struct STextureGroupSetup& setup );

	// Makes a copy of the texture render resource, every setting is the same, except dynamicAssignment (set to true, so that we can swap the internals of the texture)
	virtual TRenderPtr< IRenderResource > CloneTexture( const TRenderPtr< IRenderResource >& src );
	
	virtual void UploadDistantLights( const THandle< class CDistantLightsResource >& resource, const TRenderPtr<IRenderScene>& scene );

	struct DistantIrradianceCreateInfo
	{
		TRenderPtr<IRenderResource> resourceA { nullptr };
		TRenderPtr<IRenderResource> resourceN { nullptr };
		TRenderPtr<IRenderResource> resourceH { nullptr };
		Vector4						sectorSpan{-1,-1,1,1};
	};

	virtual void UploadDistantIrradiace( const DistantIrradianceCreateInfo& info, const TRenderPtr<IRenderScene>& scene );

	#if 0
	acseparate
	// Create renderer resource for fur mesh
	virtual IRenderResource* UploadFurMesh( const CFurMeshResource* fur );
	#endif

	// Create render resource for particle emitter
	virtual TRenderPtr<IRenderResource> UploadParticleEmitter( const char* debugName, const THandle< IRenderResourceBlob > blob, const RenderEmitterDrawerData& drawerData, const SDynamicDecalInitInfo& decalInfo );
	virtual void PrecacheParticleEmitter( const TRenderPtr<IRenderResource>& emitter, const TRenderPtr<IRenderResource>& material ) {}

	// Create renderer resource for material
	virtual TRenderPtr<IRenderResource> UploadMaterial( const IMaterialDataProvider* materialDataProvider, const IMaterialParametersProvider* materialParametersProvider, job::Builder* builder = nullptr );
	virtual void UpdateMaterial( const TRenderPtr<IRenderResource>& renderResource, const IMaterialDataProvider* materialDataProvider, const IMaterialParametersProvider* materialParametersProvider );
	virtual TRenderPtr<IRenderResource> UploadMaterialSetup( const red::Map< CName, SMaterialSetupEntry >& materialSetupParams, const red::DynArray< CName >& chunkMaterialNames );
	virtual void						ReadUsedMaterialTechniques( const TRenderPtr< IRenderResource >& renderResource, red::DynArray< Uint32 >& techniques );

	virtual TRenderPtr<IRenderResource> UploadSkinProfile( const rend::SkinProfileData* data );
	virtual void UpdateSkinProfile( const TRenderPtr<IRenderResource>& skinProfile, const rend::SkinProfileData* data );

	virtual TRenderPtr<IRenderResource> UploadHairProfile( const rend::HairProfileData* data );
	virtual void UpdateHairProfile( const TRenderPtr< IRenderResource >& skinProfile, const rend::HairProfileData* data );

	virtual TRenderPtr<IRenderResource> UploadGradient( const rend::GradientData* data );
	virtual void UpdateGradient( const TRenderPtr< IRenderResource >& gradient, const rend::GradientData* data );

	virtual TRenderPtr< IRenderResource > UploadTerrainSetup( const SRenderDataSource& src, const rend::TerrainMaterialData& data );
	virtual void UpdateTerrainSetup( const TRenderPtr< IRenderResource >& skinProfile, const rend::TerrainMaterialData* data );

	virtual TRenderPtr<IRenderResource> UploadFoliageProfile(const rend::FoliageProfileData* data, const Uint64 foliageProfileResourceHash );
	virtual void UpdateFoliageProfile(const TRenderPtr<IRenderResource>& foliageProfile, const rend::FoliageProfileData* data);

	virtual TRenderPtr<IRenderResource> UploadTextureRegionSet( const rend::TextureRegionSet* data );
	virtual void UpdateTextureRegionSet( const TRenderPtr<IRenderResource>& textureRegionSet, const rend::TextureRegionSet* data );

	virtual TRenderPtr<IRenderResource> UploadMultilayerSetup( const SRenderDataSource& src, const rend::MultilayerSetupData& data );
	virtual TRenderPtr<IRenderResource> UploadMultilayerMask( const SRenderDataSource& src, const THandle<IRenderResourceBlob>& blob );

	virtual TRenderPtr<IRenderResource> UploadIndexBuffer( const rend::IIndexBufferDataProvider& dataProvider );
	virtual TRenderPtr<IRenderResource>	UploadIndexBuffer( const red::DynArray<Uint16>& indices, const red::DynArray<Uint32>& offsets );

    virtual TRenderPtr<IRenderResource> UploadClothMesh( const TRenderPtr<IRenderResource>& mesh );

	virtual Uint16		UploadIESData( const Uint8* );

	virtual void						DrawQuadWithMaterial(IRenderResource* dynTexture, IRenderResource* material);

#ifndef NO_RUNTIME_MATERIAL_COMPILATION
	// Force recompilation of material
	virtual void ForceMaterialRecompilation( const TRenderPtr<IRenderResource>& renderResource );

	virtual Bool IsAsyncMaterialCompilationEnabled() const { return false; }
#endif

	// Create renderer mesh for static debug data
	virtual TRenderPtr<IRenderResource> UploadDebugMesh( const red::DynArray< DebugVertex >& vertices, const red::DynArray< Uint32 >& indices );
	virtual TRenderPtr<IRenderResource> UploadDebugMesh( const red::DynArray< DebugVertexUV >& vertices, const red::DynArray< Uint32 >& indices );

	virtual const GpuApi::MemoryStats& GetMemoryStats() const;
	virtual void GetDebugStats( rend::DebugStats& debugStats ) const;
	virtual void GetSkinningStats( SkinningStats& stats ) const;

	virtual void GetPreviousFrameState( PreviousFrameState& outState ) const;

public:

	//! Create debug drawer for debug geometry, top level viewport size should be given
	virtual rend::IDebugDrawer* CreateDebugDrawer( const rend::DebugFilterMask& filters, const Uint32 screenWidth, const Uint32 screenHeight ) { return nullptr; }

	//! Create X2 rendering scene
	virtual TRenderPtr<IRenderScene> CreateScene( IRenderScene::Type type );

	//! Create X@ rendering proxy in scene
	virtual TRenderPtr<IRenderProxy> CreateProxy( const RenderProxyInitInfo& info, job::Builder* builder = nullptr );

	//! Create dynamic decal object
	virtual TRenderPtr<IRenderResource> CreateDynamicDecal( const SDynamicDecalInitInfo& info );

	//! Create skinning buffer
	virtual TRenderPtr<IRenderSkinningData> CreateSkinningBuffer( const CName& category = CName::NONE() );

	//! Create a skinning buffer that should not be used for direct rendering. Writes into a provided buffer.
	virtual TRenderPtr<IRenderSkinningData> CreateNonRenderSkinningBuffer( Uint32 numMatrices ) ;

	virtual TRenderPtr<IRenderFloatTrackData> CreateFloatTrackBuffer( Uint32 numFloats, const red::DynArray< CName >& floatTrackNames ) ;

	virtual TRenderPtr<IRenderResource> CreateDynamicTexture( const DynamicTextureDesc& desc );

	virtual TRenderPtr<IRenderResource> CreateDynamicVolumeTexture( const DynamicVolumeTextureDesc& desc );

	// Fade and blackscreen state info methods
public:
	// Are we currently streaming in any textures?
	virtual Bool IsStreamingTextures() const { return false; }

	// Get number of textures and the pending data size
	virtual void GetPendingStreamingTextures( Uint32& outNumTextures, Uint32& outDataSize ) const { outNumTextures = 0; outDataSize = 0; }

	// Are we currently streaming in any meshes ?
	virtual Bool IsStreamingMeshes() const { return false; }

	// Get number of mesh that are being streamed and the pending data size
	virtual void GetPendingStreamingMeshes( Uint32& outNumMeshes, Uint32& outDataSize ) const { outNumMeshes = 0; outDataSize = 0; }

	// Are we currently prefetching envprobes
	virtual Bool IsDuringEnvProbesPrefetch() const { return false; }

	// Is device lost? If yes, do not perform any attaches and stuff
	virtual Bool IsDeviceLost() { return true; }

	// Is memory defragmentation going on?
	virtual Bool IsTexturesDefragLock() const { return false; }
	virtual Bool IsMeshesDefragLock() const { return false; }

	// Force fake device reset - HACK :( 
	virtual void ForceFakeDeviceReset();
	virtual void ForceFakeDeviceLost();
	virtual void ForceFakeDeviceUnlost();

public:
	// Grab a recent thumbnail screenshot. This is not necessarily the more recent frame, but also does not have to wait
	// for the GPU to finish rendering things. Expects the screenshotParameters to be set up for the current platform's
	// thumbnail size (rend::dim::eSAVE_THUMBNAIL_CURRENT_PLATFORM).
	virtual void GrabLatestGameSaveThumbnail( const SScreenshotParameters& screenshotParameters ) {}

	virtual void TakeScreenshot( const SScreenshotParameters& screenshotParameters, Bool* finished, Bool& status ) {}
	virtual void TakeQuickScreenshot( const SScreenshotParameters& screenshotParameters ) {}
	virtual void TakeQuickScreenshotNow( const SScreenshotParameters& screenshotParameters ) {}

	// Convert frame grab info to images.
	virtual void TakeOneRegularScreenshotNow( const CRenderFrameGrab& frameInfoGrab, const SScreenshotParameters& screenshotParameters, EFramePurpose framePurpose /*= FP_ScreenShot*/ ) const {}

	virtual void SetAsyncCompilationMode( bool /*enable*/ ) {}
	virtual Bool GetAsyncCompilationMode() const { return false; };

	virtual void SetObserverVelocity( Float velocity );
	virtual void GIStreamingRegistration( Uint64 nodeId, Bool unregister = false );

	virtual Float GetGIStrength() const;
	virtual Uint32 GetNumSectorsWaiting() const;
	virtual Float GetAvgWaitingTime() const;
	virtual Float GetGIObserverVelocity() const;

	virtual Float GetLastGPUFrameDuration( Float* outMainScene = nullptr, Float* outSubscene = nullptr, Float* outDrawBuffers = nullptr ) const { return 0.f; }

	virtual Uint32 GetRayTracingInstanceNum() const { return 0; }

#ifndef RED_PLATFORM_CONSOLE
	virtual void DisableCacheUsing();
#endif

public:
	virtual IRenderEngineHandler* GetEngineHandler() const { return nullptr; }

public:	
	virtual Int32 GetMonitorCount() const;
	virtual Uint32 GetSupportedHDRModesMask( Int32 monitorIndex, Bool fullscreen ) const;

	virtual Bool GetHardwareRayTracingSupport() const;
#ifdef RED_PLATFORM_WINPC
	virtual Bool GetHardwareDLSSSupport() const;
#endif
	virtual Float GetMonitorMaxLuminanceNits( Int32 monitorIndex ) const;
	virtual void GetNativeResolution( Uint32& width, Uint32& height ) const;
	virtual Uint32 GetCurrentOutputMonitorConfig() const;
	//! List all currently available screen modes
	virtual void ListAvailableDisplayModes( red::DynArray<String>& outScreenModes, Int32 requestedMonitorIdx ) const;
	virtual void ListAvailableVSyncFPSModes( red::DynArray<String>& outVSyncFPSModes, Int32 requestedMonitorIdx = -1 ) const;
	virtual Bool GetBestMatchingResolution( const Uint32 width, const Uint32 height, Uint32& outWidth, Uint32& outHeight, Uint32 monitorIdx = 0 ) const;
	virtual void GetResolutionScale( float& x, float& y ) const;
	virtual void GetDynamicResolutionSetup( SDynamicResolutionSetup& outSetup ) const;
#ifdef USE_PROFILER
	virtual void GetResolutionHistory( red::DynArray< ResolutionHistoryEntry >& outHistory ) const;
#endif

#if defined( RED_USE_IMGUI ) && defined( RED_PLATFORM_WINPC )
	virtual void GetDLSSDebugSettings( Bool& enabledInNGX, Int32& ngxQuality ) const;
#endif

public:
	virtual IRenderCommandHandler* GetCommands() { return nullptr; }

	virtual vis::OccluderInstancePtr CreateOccluderInstance(const vis::OccluderInstanceInitInfo& initInfo);

	//! Create movie renderer
	virtual TRenderPtr<IRenderVideo> CreateVideo( const VideoParams& videoParams ) const;
	
	virtual void ProcessRenderVideoDestroyQueue() const {}


	RED_INLINE rend::GameplayEffects& GetGameplayEffects() { return *m_gameplayEffects; }

#ifdef USE_PROFILER
	virtual renderDev::SConfig*		GetDevConfig() const { return nullptr; };
	virtual renderDev::ICameraSpy*	GetCameraSpy() const { return nullptr; };
#endif

#ifdef USE_PROFILER
public:
	virtual IRenderGpuProfiler* GetGpuProfiler() const { return nullptr; } 
#endif 

#ifndef RED_CONFIGURATION_FINAL
	// Populate the SSectorStats with runtime information
	virtual void SetGIDebugData(GI::SSectorStats& out, math::Vector3 sector);
#endif //  RED_CONFIGURATION_FINAL

private:
	red::UniquePtr<rend::GameplayEffects> m_gameplayEffects;
};

RENDER_DATA_API void InitializeGpuMemoryPools();
RENDER_DATA_API void UninitializeGpuMemoryPools();

/// Render interface
extern RENDER_DATA_API IRender* GRender;

RENDER_DATA_API void RegisterRender( std::function<IRender*()> creator, 
									 std::function<void(IRender*)> destructor, 
									 std::function<Bool(void)> minimumRequiredGPU,
									 std::function<Uint64(void)> gpuTotalMemory );

RENDER_DATA_API void CreateRender();
RENDER_DATA_API void DeleteRender();
RENDER_DATA_API Bool HasMinimumRequiredGPU();
RENDER_DATA_API Uint64 GetGpuTotalMemory();
