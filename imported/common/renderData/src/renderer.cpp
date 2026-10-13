/**
* Copyright (c) 2007 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"
#include "renderer.h"
#include "gameplayEffects.h"
#include "renderProxy.h"
#include "renderSkinningData.h"
#include "gpuApiMemory.h"
#include "renderVideo.h"
#include "viewport.h"
#include "../../gpuApi/include/gpuApiStats.h"

static std::function<IRender*()> SRenderCreator = []()->IRender* { return RED_NEW(IRender)(); };
static std::function<void(IRender*)> SRenderDestructor = [](IRender* render)->void { RED_DELETE(render); };
static std::function<Bool(void)> SMinimumRequiredGPU = [](void)->Bool { return true; };
static std::function<Uint64(void)> SGetGpuTotalMemory = [](void)->Uint64 { return 0; };

IRender* GRender = nullptr;

// s.ortiz shitty temp hack
#if defined( RED_PLATFORM_LINUX )
void GpuApi::InitializeMemoryPool() { }
void GpuApi::UninitializeMemoryPool() { }
#endif

void InitializeGpuMemoryPools()
{
	GpuApi::InitializeMemoryPool();
}

void UninitializeGpuMemoryPools()
{
	GpuApi::UninitializeMemoryPool();
}

// Render

IRender::IRender()
	: m_gameplayEffects( red::CreateUniquePtr<rend::GameplayEffects>() )
{
}

IRender::~IRender()
{
}

void IRender::Register( red::UniquePtr< IRenderEngineHandler >&& engineHandler )
{
}

void IRender::LoadConfig( )
{
}

Bool IRender::Init()
{
	return true;
}

void IRender::Tick(Float timeDelta, Float timeScale, Float lastCpuFrameTime)
{
}

Bool IRender::PrepareRenderer()
{
	return true;
}

void IRender::Flush()
{
}

void IRender::Sync( job::Builder& sync )
{
}

#ifdef RENDER_ENABLE_RESOURCE_EVICT
void IRender::RequestEvictOnNextSceneRender()
{
}
#endif

void IRender::Unsafe_DebugDumpRenderCommandQueue()
{
}

void IRender::ReloadTextures()
{
}

void IRender::ReloadEngineShaders()
{
}

void IRender::FlushReloadSimpleShaders()
{
}

void IRender::EnqueueReloadSimpleShaders()
{
}

void IRender::RecreatePlatformResources()
{
}

void IRender::RecalculateTextureStreamingSettings()
{
}

void IRender::RelinkSimpleShaders()
{
}

Bool IRender::InitWithoutViewport( Uint32 width, Uint32 height, EViewportWindowMode windowMode, Bool vsync )
{
	return false;
}

ViewportHandle IRender::CreateViewport(void* TopLevelWindow, void* ParentWindow, const red::String& title, Uint32 width, Uint32 height, EViewportWindowMode windowMode, Bool vsync, Int32 outputMonitorIdx, Int32 hdrMode )
{
	return ViewportHandle();
}

ViewportHandle IRender::CreateGameViewport(const red::String& title, Uint32 width, Uint32 height, EViewportWindowMode windowMode, Int32 hdrMode, Int32 monitorIdx )
{
	return ViewportHandle();
}

void IRender::RequestResizeRenderSurfaces(Uint32 width, Uint32 height)
{
}

void IRender::UploadDistantLights( const THandle< class CDistantLightsResource >& resource, const TRenderPtr<IRenderScene>& scene )
{
}

void IRender::UploadDistantIrradiace( const DistantIrradianceCreateInfo& info, const TRenderPtr<IRenderScene>& scene )
{
}

TRenderPtr<IRenderResource> IRender::UploadBlobMesh( const SRenderDataSource& src, const THandle< IRenderResourceBlob  > blob )
{
	return {};
}

// Create renderer resource for texture blob
TRenderPtr<IRenderResource> IRender::UploadBlobTexture( const SRenderDataSource& src, const THandle< IRenderResourceBlob  > blob, const struct STextureGroupSetup& setup )
{
	return {};
}

TRenderPtr< IRenderResource > IRender::CloneTexture( const TRenderPtr< IRenderResource >& src )
{
	return {};
}

TRenderPtr<IRenderResource> IRender::UploadParticleEmitter(const char* debugName, const THandle< IRenderResourceBlob > blob, const RenderEmitterDrawerData& drawerData, const SDynamicDecalInitInfo& decalInfo)
{
	return {};
}

TRenderPtr<IRenderResource> IRender::UploadMaterial(const IMaterialDataProvider* materialDataProvider, const IMaterialParametersProvider* materialParametersProvider, job::Builder* builder)
{
	return {};
}

void IRender::UpdateMaterial( const TRenderPtr<IRenderResource>& renderResource, const IMaterialDataProvider* materialDataProvider, const IMaterialParametersProvider* materialParametersProvider)
{
}

TRenderPtr<IRenderResource> IRender::UploadMaterialSetup(const red::Map< CName, SMaterialSetupEntry >& materialSetupParams, const red::DynArray< CName >& chunkMaterialNames )
{
	return {};
}

void IRender::ReadUsedMaterialTechniques( const TRenderPtr< IRenderResource >& renderResource, red::DynArray< Uint32 >& techniques )
{
}

TRenderPtr<IRenderResource> IRender::UploadSkinProfile( const rend::SkinProfileData* data )
{
	return {};
}

void IRender::UpdateSkinProfile( const TRenderPtr<IRenderResource>& skinProfile, const rend::SkinProfileData* data )
{
}

TRenderPtr<IRenderResource> IRender::UploadHairProfile( const rend::HairProfileData* data )
{
	return {};
}

void IRender::UpdateHairProfile(const TRenderPtr<IRenderResource>& hairProfile, const rend::HairProfileData* data)
{
}

TRenderPtr<IRenderResource> IRender::UploadGradient( const rend::GradientData* data )
{
	return {};
}

void IRender::UpdateGradient( const TRenderPtr<IRenderResource>& gradient, const rend::GradientData* data )
{
}

TRenderPtr< IRenderResource > IRender::UploadTerrainSetup( const SRenderDataSource& src, const rend::TerrainMaterialData& data )
{
	return {};
}

void IRender::UpdateTerrainSetup( const TRenderPtr< IRenderResource >& terrainSetup, const rend::TerrainMaterialData* data )
{
}

TRenderPtr<IRenderResource> IRender::UploadFoliageProfile(const rend::FoliageProfileData* data, const Uint64 foliageProfileResourceHash )
{
	return {};
}

void IRender::UpdateFoliageProfile( const TRenderPtr<IRenderResource>& foliageProfile, const rend::FoliageProfileData* data )
{
}

TRenderPtr<IRenderResource> IRender::UploadTextureRegionSet( const rend::TextureRegionSet* data )
{
	return {};
}

void IRender::UpdateTextureRegionSet( const TRenderPtr<IRenderResource>& textureRegionSet, const rend::TextureRegionSet* data )
{
}

TRenderPtr<IRenderResource> IRender::UploadMultilayerSetup( const SRenderDataSource& src, const rend::MultilayerSetupData& data )
{
	return {};
}

TRenderPtr<IRenderResource> IRender::UploadMultilayerMask( const SRenderDataSource& src, const THandle<IRenderResourceBlob>& blob )
{
	return {};
}

TRenderPtr<IRenderResource> IRender::UploadIndexBuffer( const rend::IIndexBufferDataProvider& dataProvider )
{
	return{};
}

TRenderPtr<IRenderResource>	IRender::UploadIndexBuffer( const red::DynArray<Uint16>& indices, const red::DynArray<Uint32>& offsets )
{
	return{};
}


TRenderPtr<IRenderResource> IRender::UploadClothMesh( const TRenderPtr<IRenderResource>& mesh )
{
    return {};
}

Uint16 IRender::UploadIESData( const Uint8* )
{
	return 0; //  UINT16_MAX;
}

void IRender::DrawQuadWithMaterial(IRenderResource* dynTexture, IRenderResource* material)
{

}

#ifndef NO_RUNTIME_MATERIAL_COMPILATION
void IRender::ForceMaterialRecompilation( const TRenderPtr<IRenderResource>& renderResource)
{
}
#endif

TRenderPtr<IRenderResource> IRender::UploadDebugMesh(const red::DynArray< DebugVertex >& vertices, const red::DynArray< Uint32 >& indices)
{
	return {};
}

TRenderPtr<IRenderResource> IRender::UploadDebugMesh( const red::DynArray< DebugVertexUV >& vertices, const red::DynArray< Uint32 >& indices )
{
	return {};
}

const GpuApi::MemoryStats& IRender::GetMemoryStats() const
{
	static GpuApi::MemoryStats dummyStats;
	return dummyStats;
}

void IRender::GetDebugStats( rend::DebugStats& ) const
{
}

void IRender::GetSkinningStats( SkinningStats& ) const
{
}

void IRender::GetPreviousFrameState( PreviousFrameState& ) const
{
}

TRenderPtr<IRenderScene> IRender::CreateScene( IRenderScene::Type type )
{
	return {};
}

TRenderPtr<IRenderProxy> IRender::CreateProxy( const RenderProxyInitInfo& info, job::Builder* builder )
{
	return {};
}

TRenderPtr<IRenderResource> IRender::CreateDynamicDecal( const SDynamicDecalInitInfo& info )
{
	return {};
}

TRenderPtr<IRenderSkinningData> IRender::CreateSkinningBuffer( const CName& category )
{
	return {};
}

TRenderPtr<IRenderSkinningData> IRender::CreateNonRenderSkinningBuffer( Uint32 numMatrices )
{
	return {};
}

TRenderPtr<IRenderFloatTrackData> IRender::CreateFloatTrackBuffer( Uint32 numFloats, const red::DynArray< CName >& floatTrackNames )
{
	return {};
}

TRenderPtr<IRenderResource> IRender::CreateDynamicTexture( const DynamicTextureDesc& desc )
{
	RED_TOUCH( desc );
	return {};
}

TRenderPtr<IRenderResource> IRender::CreateDynamicVolumeTexture( const DynamicVolumeTextureDesc& desc )
{
	RED_TOUCH( desc );
	return {};
}

Int32 IRender::GetMonitorCount() const
{
	RED_FATAL("Shouldn't be called");
	return 1;
}

Uint32 IRender::GetSupportedHDRModesMask( Int32 monitorIndex, Bool fullscreen ) const
{
	RED_FATAL( "Shouldn't be called" );
	return 1;
}

Bool IRender::GetHardwareRayTracingSupport() const
{
	RED_FATAL( "Shouldn't be called" );
	return 1;
}
#ifdef RED_PLATFORM_WINPC
Bool IRender::GetHardwareDLSSSupport() const
{
	RED_FATAL( "Shouldn't be called" );
	return 1;
}
#endif
Float IRender::GetMonitorMaxLuminanceNits( Int32 monitorIndex ) const
{
	RED_FATAL( "Shouldn't be called" );
	return 1;
}

void IRender::GetNativeResolution( Uint32& width, Uint32& height ) const
{
	RED_FATAL("Shouldn't be called");
	width = 0;
	height = 0;
}

Uint32 IRender::GetCurrentOutputMonitorConfig() const
{
	RED_FATAL("Shouldn't be called");
	return 0;
}

void IRender::ListAvailableDisplayModes(red::DynArray<String>& outScreenModes, Int32 requestedMonitorIdx ) const
{
	RED_FATAL("Shouldn't be called");
	outScreenModes.Clear();
}

void IRender::ListAvailableVSyncFPSModes( red::DynArray<String>& outVSyncFPSModes, Int32 requestedMonitorIdx /*= -1 */ ) const
{
	RED_FATAL( "Shouldn't be called" );
	outVSyncFPSModes.Clear();
}

Bool IRender::GetBestMatchingResolution(const Uint32 width, const Uint32 height, Uint32& outWidth, Uint32& outHeight, Uint32 monitorIdx /* = 0 */) const
{
	RED_FATAL("Shouldn't be called");
	return false;
}

void IRender::GetResolutionScale(float& x, float& y) const
{
	RED_FATAL("Shouldn't be called");
}

void IRender::GetDynamicResolutionSetup( SDynamicResolutionSetup& outSetup ) const
{
}

#ifdef USE_PROFILER
void IRender::GetResolutionHistory( red::DynArray< ResolutionHistoryEntry >& outHistory ) const
{
}
#endif

#if defined( RED_USE_IMGUI ) && defined( RED_PLATFORM_WINPC )
void IRender::GetDLSSDebugSettings( Bool& enabledInNGX, Int32& ngxQuality ) const
{
}
#endif

void IRender::ForceFakeDeviceReset()
{
}

void IRender::ForceFakeDeviceLost()
{
}

void IRender::ForceFakeDeviceUnlost()
{
}

void IRender::SetObserverVelocity( Float velocity )
{
}

void IRender::GIStreamingRegistration( Uint64 nodeId, Bool unregister /*= false */ )
{
}

Float IRender::GetGIStrength() const
{
	return 0.0f;
}

Uint32 IRender::GetNumSectorsWaiting() const
{
	return 0;
}

Float IRender::GetAvgWaitingTime() const
{
	return 0.0f;
}

Float IRender::GetGIObserverVelocity() const
{
	return 0.0f;
}

#ifndef RED_PLATFORM_CONSOLE
void IRender::DisableCacheUsing()
{
}
#endif

vis::OccluderInstancePtr IRender::CreateOccluderInstance(const vis::OccluderInstanceInitInfo& /*initInfo*/)
{
	return vis::OccluderInstancePtr();
}

TRenderPtr<IRenderVideo> IRender::CreateVideo( const VideoParams& videoParams ) const
{
	return nullptr;
}

#ifndef RED_CONFIGURATION_FINAL
void IRender::SetGIDebugData(GI::SSectorStats& /*out*/, math::Vector3 sector /*sectorCenter*/)
{
	RED_FATAL("Shouldn't be called");
}
#endif //  RED_CONFIGURATION_FINAL

/**
Register functions which create/delete instance of render implementation.
This function has to be called during static initalization of render library.
ex. common\renderer\src\renderInterface.cpp

\param std::function<IRender*()> creator - lambda function which create instance of render implementation
\param std::function<void(IRender*)> destructor - lambda function which delete instance of render implementation
\param std::function<Bool(void)> minimumRequiredGPU - lambda function which check minimum requirements for GPU

*/
void RegisterRender( std::function<IRender*()> creator, 
					 std::function<void(IRender*)> destructor, 
					 std::function<Bool(void)> minimumRequiredGPU,
					 std::function<Uint64(void)> gpuTotalMemory )
{
	RED_FATAL_ASSERT(!GRender);

	SRenderCreator		= creator;
	SRenderDestructor	= destructor;
	SMinimumRequiredGPU = minimumRequiredGPU;
	SGetGpuTotalMemory  = gpuTotalMemory;
}

/**
Create render implementation. It using lambda function set in RegisterRender
*/

void CreateRender()
{
	GRender = SRenderCreator();
	RED_FATAL_ASSERT(GRender);
}

/**
Delete render implementation. It using lambda function set in RegisterRender
*/
void DeleteRender()
{
	if( GRender != nullptr )
	{
		SRenderDestructor(GRender);
		GRender = nullptr;
	}
}

/**
Check minimum requirements for GPU. It using lambda function set in RegisterRender
*/
Bool HasMinimumRequiredGPU()
{
	return SMinimumRequiredGPU();
}

Uint64 GetGpuTotalMemory()
{
	return SGetGpuTotalMemory();
}