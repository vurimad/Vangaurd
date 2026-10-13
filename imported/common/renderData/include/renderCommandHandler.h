/**
* Copyright (c) 2007 CD Projekt Red. All Rights Reserved.
*/
#pragma once

#include "visibilityOccluderInstance.h"
#include "renderPublicEnums.h"

struct SVertexShaderCustomSlotParams;
struct SParticleEffectContextUpdate;
struct SFogVolumeEffectContextUpdate;
struct SColorGradeEffectContextUpdate;
struct SMaterialParameterEffectContextUpdate;
struct SLightParameterEffectContextUpdate;
struct SLightFlickerParamsUpdate;
struct SDecalEffectContextUpdate;
struct SDestructionRegionsEffectContextUpdate;
struct SVehicleParamsUpdate;
class IRenderVideo;
class CRenderFrameGrab;
enum EPostFXEffect : Uint32;
struct PostFXEffectParameters;
class CReflectionProbeDataResource;
struct RenderDismCullObjectCB;
struct RenderRainData;
struct SMaterialSetupParameters;
struct SMaterialUpdateData;
struct SExposureScaleParams;
struct STonemappingParams;
struct SChromaticAberrationParams;
struct SVignetteParams;
struct SBloomParams;
struct SFilmGrainParams;
struct SColorGradeV2Params;
struct SEmissiveParams;
struct SWeaponPlaneBlurParams;
struct RendInteriorMapData;
struct SDataMoshParams;

namespace MorphTargets
{
	struct MorphTargetWeight;
}

namespace rend
{
	enum class PostFx_ScanningState : Uint8;
	class DrawBuffer;
	typedef red::FixedSizeFunction<void(const red::String&, bool),72U> OnScreenshotCreatedCallback;
	struct AutoFoliageRuntimeMap;
	struct WindImpulseSphere;
	struct WindImpulseCapsule;
}

namespace job
{
	class Builder;
}

// IRenderCommandHandler
class IRenderCommandHandler
{
public:

	using TScenePtr = TRenderPtr<IRenderScene>;
	using TProxyPtr = TRenderPtr<IRenderProxy>;

	virtual ~IRenderCommandHandler () {}

public:

	#ifndef NO_EDITOR
	struct DebugTexturePreviewDesc
	{
		Rect	rect{ Rect::EMPTY() };
		Int8	mipIdx{-1};
		Uint16	sliceIdx{0};
		Float	minVal{0.0f};
		Float	maxVal{1.0f};
		Uint8	channelMask{1|2|4};
		Uint8	numChannels{3};
		Bool	allowInterpret{true};
		Bool	pointSample{false};
		Float	min{ 0.0f };
		Float	max{ 1.0f };
	};

	virtual void SendDebugTexturePreview( const TScenePtr& scene, const TRenderPtr<IRenderResource>& texture, const DebugTexturePreviewDesc& desc ) = 0;
	virtual void EditorForceAutoHideDistance( const TProxyPtr& proxy, Bool forceNoAuothideDistance, Bool isGlobal ) = 0;
	virtual void UseExtendedDynamicRangeSky( const TScenePtr& scene, bool enable ) = 0;
	#endif

	// Give renderer a chance to release any resources it might be holding onto. Should be called periodically when
	// doing some blocking operation involving loading/unloading a lot of resources, when FrameTick isn't being fired
	// off.
	virtual void RetirePendingResources() = 0;

	// Call once per frame, just before all RenderScene. Allows renderer to do any per-frame processing, without
	// worrying about whether we might have multiple RenderScene calls.
	// #TODO: Might be useful to have a time delta? Or something else?
	virtual void FrameTick( red::ArraySpan< TRenderPtr<IRenderScene> > scenes, Bool flushSkinning = true ) = 0;

	// Render scene, but before doing that - execute all commands queued up so far. This is the right command to issue and complete all renderer work for one frame.
	virtual void RenderScene( const TRenderPtr<IRenderFrame>& frame ) = 0;

	// Queue up Render Scene as a regular command.
	virtual void RenderSubScene( const TRenderPtr<IRenderFrame>& frame ) = 0;

	virtual void RegisterCamera( const TScenePtr& scene, const CRenderCamera& camera, const SCameraRenderViewport &cameraViewport, Uint64 cameraName, Bool pernament = true ) = 0;
	virtual void UnregisterCamera( const TScenePtr& scene, Uint64 cameraName ) = 0;
	virtual void AddCameraDependency( const TScenePtr& scene, Uint64 parentCamera, Uint64 childCamera ) = 0;
	virtual void RemoveCameraDependency( const TScenePtr& scene, Uint64 parentCamera, Uint64 childCamera ) = 0;
	virtual void UpdateCameraData( const TScenePtr& scene, Uint64 cameraName, const class CRenderCameraFrameInfo& cameraInfo ) = 0;
	virtual void MarkCameraCut( const TScenePtr& scene, Uint64 cameraName ) = 0;
	virtual void AddProxyToScene( const TScenePtr& scene, const TProxyPtr& proxy ) = 0;
	virtual void AddProxiesToScene( const TScenePtr& scene, const red::ArraySpan<TRenderPtr<IRenderProxy>>& proxy ) = 0;
	virtual void RemoveProxyFromScene( const TScenePtr& scene, const TProxyPtr& proxy ) = 0;
	virtual void RemoveProxiesFromScene( const TScenePtr& scene, const red::ArraySpan<TRenderPtr<IRenderProxy>>& proxy ) = 0;
	virtual void MoveProxyInSceneVisStructure( const TScenePtr& scene, const TProxyPtr& proxy ) = 0;
	virtual void MoveProxiesInSceneVisStructure( const TScenePtr& scene, const red::ArraySpan< TRenderPtr<IRenderProxy> >& proxies ) = 0;
	virtual void UpdateSecondaryRefPoint( const TProxyPtr& proxy, const Vector3& newSecondaryRefPoint ) = 0;
	virtual void UpdateLocalShadowsInfo( const TScenePtr& scene, const TProxyPtr& proxy, Bool castLocalShadows, Bool castShadowsEvenIfNotVisibile ) = 0;
	virtual void UpdateHitProxyID( const TProxyPtr& proxy, const IRenderSelectable* selectable ) = 0;
	virtual void AppendVertexShaderCustomSlotParams( const TProxyPtr& proxy, const SVertexShaderCustomSlotParams& params ) = 0;
	virtual void SetSelectionFlag( const TScenePtr& scene, const TProxyPtr& proxy, Bool flag, Uint8 selectionInfo ) = 0;
	virtual void UpdateMeshDebugColor( const TProxyPtr& proxy, const Color newDebugColor ) = 0;
	virtual void TakeScreenshot( const class CRenderFrameGrab& frameInfoGrab, const String& filePath, ESaveFormat saveFormat, rend::OnScreenshotCreatedCallback onCreatedCallback, EFramePurpose framePurpose = FP_ScreenShot ) = 0;
	virtual void TakeScreenshot( const class CRenderFrameGrab& frameInfoGrab, IFile* output, ESaveFormat saveFormat, rend::OnScreenshotCreatedCallback onCreatedCallback, EFramePurpose framePurpose = FP_ScreenShot ) = 0;
	virtual void ToggleContinuousScreenshot( Bool isEnabled, ESaveFormat saveFormat ) = 0;
	virtual void ReloadReflectionProbe( const TResAsyncRef< CReflectionProbeDataResource >& path, Uint64 dataHash ) = 0;
	virtual void ScreenFadeOut( const TScenePtr& scene, const Color& color, Float time ) = 0;
	virtual void ScreenFadeIn( const TScenePtr& scene, Float time ) = 0;
	virtual void ChangeSceneForcedLOD( const TScenePtr& scene, Int32 forceLOD ) = 0;
	virtual void SetProxyDissolveVisibility( const TProxyPtr& proxy, const Bool isVisibile, const Uint8 force ) = 0;
	virtual void DisableClusteredProxyInstances( const TProxyPtr& proxy, const red::BitSet64Dynamic& clusteredProxyInstances ) = 0;
	virtual void ForceLODForProxy( const TProxyPtr& proxy, Int8 lod ) = 0;
	virtual void ForceLODForProxies( const red::DynArray< TRenderPtr< IRenderProxy > >& proxies, Int8 lod ) = 0;
	virtual void LODLevelScalesForProxy( const TProxyPtr& proxy, Uint32 scales ) = 0;
	virtual void SuspendRendering() = 0;
	virtual void ResumeRendering() = 0;
	virtual void GamePaused() = 0;
	virtual void GameUnpaused() = 0;
	virtual void HandleResizeEvent( Uint32 width, Uint32 height ) = 0;
	virtual void SetAutoFoliageRuntimeMap( const TRenderPtr<IRenderScene>& scene, red::UniquePtr<rend::AutoFoliageRuntimeMap>&& runtimeMap ) = 0;
	virtual void AddWindImpulseShapes( const TRenderPtr<IRenderScene>& scene, red::DynArray<rend::WindImpulseSphere>&& sphereShapes, red::DynArray<rend::WindImpulseCapsule>&& capsuleShapes ) = 0;
    virtual void SetInteriorMapCellData( const TRenderPtr<IRenderScene>& scene, Uint64 hash, red::UniquePtr<RendInteriorMapData, rend::PoolEnvironment>&& data, Bool force = false ) = 0;
	virtual void SetAutohideDistanceNear( const TProxyPtr& proxy, Float value ) = 0;

	// Stuff for effect system context updates
	virtual void UpdateParticleEffectContext( const TProxyPtr& proxy, const SParticleEffectContextUpdate& update ) = 0;
	virtual void UpdateFogVolumeEffectContext( const TProxyPtr& proxy, const SFogVolumeEffectContextUpdate& update ) = 0;
	virtual void UpdateMaterialParameterEffectContext( const TProxyPtr& proxy, const SMaterialParameterEffectContextUpdate& update ) = 0;
	virtual void UpdateLightParameterEffectContext( const TProxyPtr& proxy, const SLightParameterEffectContextUpdate& update ) = 0;
	virtual void UpdateColorGradeEffectContext( const SColorGradeEffectContextUpdate& update ) = 0;
	virtual void UpdateMotionBlurScaleEffectContext( Float motionBlurScale ) = 0;
	virtual void UpdateExposureScaleEffectContext( const SExposureScaleParams& exposureScaleParams ) = 0;
	virtual void UpdateTonemappingEffectContext( const STonemappingParams& tonemappingParams ) = 0;
	virtual void SaveTonemappingEffectContext() = 0;
	virtual void RestoreTonemappingEffectContext() = 0;

	virtual void UpdateChromaticAberrationEffectContext(const SChromaticAberrationParams& chromaticAberrationParams) = 0;
	virtual void UpdateBloomnEffectContext(const SBloomParams& chromaticAberrationParams) = 0;
	virtual void UpdateVignetteEffectContext(const SVignetteParams& vignetteParams) = 0;
	virtual void UpdateColorGradeV2EffectContext(const SColorGradeV2Params& colorGradeV2Params) = 0;
	virtual void UpdateEmissiveEffectContext(const SEmissiveParams& emissiveParams) = 0;
	virtual void UpdateFilmGrainEffectContext(const SFilmGrainParams& filmGrain) = 0;
	virtual void UpdateWeaponPlaneBlurEffectContext(const SWeaponPlaneBlurParams& weaponPlaneBlurParams) = 0;
	virtual void UpdateFOVEffectContext( const TScenePtr& scene, const Float fov ) = 0;
	virtual void UpdateDataMoshEffectContext(const SDataMoshParams& dataMoshParams ) = 0;

	virtual void UpdateDecalEffectContext( const TProxyPtr& proxy, const SDecalEffectContextUpdate& update ) = 0;
	virtual void UpdateVehicleParams( const TProxyPtr& proxy, const SVehicleParamsUpdate& update ) = 0;
    virtual void UpdateClothMeshData(const TProxyPtr& proxy, Uint8 chunk, red::DynArray<Vector3>&& positions, Box bounds) = 0;
	virtual void UpdateDestructionRegionsEffectContext(const TProxyPtr& proxy, const SDestructionRegionsEffectContextUpdate& update ) = 0;
	virtual void Add_SetDismembermentData( const TProxyPtr& proxy, const RenderDismCullObjectCB* data ) = 0;
	virtual void SetRainData( const TProxyPtr& proxy, const RenderRainData& rainData) = 0;
	virtual void SuspendParticles( const TScenePtr& scene ) = 0;
	virtual void ResumeParticles( const TScenePtr& scene ) = 0;

	// Override some parameters in the materials used by a render proxy. Does not stack, overrides will be applied to
	// the proxy's original materials. Overrides are applied to all materials in the proxy that can use them (where the
	// material has a matching parameter definition);
	// #NOTE: Currently only mesh proxies are supported. Will assert if you give it another type.
	virtual void SetMaterialParameterOverrides( const TProxyPtr& meshProxy, SMaterialSetupParameters&& params ) = 0;

	// update material data
	virtual void UpdateMaterialData( const TRenderPtr< IRenderResource >& material, SMaterialUpdateData&& updateData ) = 0;

	virtual void ApplyMorphTargetWeights( const TProxyPtr& morphTargetProxy, const red::DynArray< MorphTargets::MorphTargetWeight >& weights ) = 0;

	virtual void UnloadMorphData( const TProxyPtr& morphTargetProxy ) = 0;

	virtual void UpdateLightFlickerParams( const TProxyPtr& proxy, const SLightFlickerParamsUpdate& update ) = 0;

	virtual void ResizeDynamicTexture( const TRenderPtr<IRenderResource>& dynamicTexture, Uint32 newWidth, Uint32 newHeight, Bool mipChain ) = 0;

	virtual void SubmitDrawBuffer( const red::SharedPtr<rend::DrawBuffer>& drawBuffer ) = 0;

	// Reflection probes
	virtual void ForceRelitGlobalProbe( const TScenePtr& scene ) = 0;
	virtual void ForceRelitLocalProbes( const TScenePtr& scene , const Vector3& position, Float radius ) = 0;
	virtual void NotifyPlayerInsideVehicle( const TScenePtr& scene, const Bool isPlayerDrivingVehicle ) = 0;

	// Gameplay Effects
	virtual void DisableAllGameplayEffects() = 0;
	virtual void ToggleGameplayEffect( EPostFXEffect effect, Bool enabled ) = 0;
	virtual void SetGameplayEffectParameters( const PostFXEffectParameters& params ) = 0;
	virtual void SetVisionModeHighlight( const TScenePtr& scene, const TProxyPtr& proxy, Bool isHighlighted, Uint8 highlightInfo, Float opacityValue, Bool initial ) = 0;
	virtual void SetVisionModeScanning( const TScenePtr& scene, const TProxyPtr& proxy, rend::PostFx_ScanningState state ) = 0;
	virtual void EnforceScreenSpaceReflectionsUberQuality( Bool enable ) = 0;

	// Stuff for visibility system.
	virtual void AddOccluderToScene( const TScenePtr& scene, const vis::OccluderInstancePtr& occluder, const Matrix& localToWorld ) = 0;
	virtual void AddOccludersToScene( const TScenePtr& scene, red::DynArray<vis::OccluderAddInfo>&& occluders ) = 0;
	virtual void RemoveOccluderFromScene( const vis::OccluderInstancePtr& occluder ) = 0;
	virtual void RemoveOccludersFromScene( red::DynArray<vis::OccluderInstancePtr>&& occluders ) = 0;
	virtual void UpdateOccluder( const vis::OccluderInstancePtr& occluder, const Matrix& localToWorld ) = 0;

	// Draw a video in a given canvas. If canvasName is empty/none/etc, video plays fullscreen. If it is set to some
	// name, it will draw on any bink-enabled material which references it.
	virtual void SetVideoToRender( CName canvasName, const TRenderPtr<IRenderVideo>& video ) = 0;

	virtual void FlushTextureGrabs() = 0;

	virtual void FlushPreviousFrameCommandsProcessing() = 0;

	virtual void SetMeshChunkMask( const TProxyPtr& proxy, Uint64 mask ) = 0;
	virtual void SetIndexBufferOverride( const TProxyPtr& proxy, const TRenderPtr<IRenderResource>& ibOverride ) = 0;

	virtual void SetMeshMaterialOverlay( const TProxyPtr& proxy, const TRenderPtr< IRenderResource >& material, const TRenderPtr< IRenderResource >& materialParam, bool overrideMaterial ) = 0;
	
	virtual void ResetSceneCustomData( const TScenePtr& scene ) = 0;
};
