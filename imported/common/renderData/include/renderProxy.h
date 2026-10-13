/**
* Copyright (c) 2007 CD Projekt Red. All Rights Reserved.
*/
#pragma once

#include "renderProxySystemInterface.h"
#include "renderProxyInitData.h"
#include "renderProxyIsolation.h"
#include "morphTargetCommon.h"


struct SMaterialSetupParameters;
struct LocalShadowsTrianglesCount;
struct SParticleEmitterSimulationData;
struct SParticlePhysicsSimulationDataDoubleBuffered;
struct SDecalEffectContextUpdate;
struct SDestructionRegionsEffectContextUpdate;
struct SVehicleParamsUpdate;
struct SLightFlickerParamsUpdate;

namespace rend
{
	enum class PostFx_ScanningState : Uint8;
	struct VisionModeHighlightData;
}

enum ERenderProxyType : Uint8;

enum ESelectionMode : Uint8
{
	SM_None,
	SM_Fill,
	SM_Outline,
	SM_FillAndOutline
};

enum ESelectionColor : Uint8
{
	SC_Regular,
	SC_Special
};

enum EFadeType : Int32
{
	FT_None,				//!< No fade
	FT_FadeIn,				//!< Fade in
	FT_FadeInStart,			//!< Fade in, meant to be used on proxies during initialization.
	FT_FadeOut,				//!< Fade out
};

enum class FadeInForcingMode : Uint8
{
	None = 0, 
	ForceFade, 
	ForceInstant,
};

namespace 
{
	Uint8 PackSelectionInfo( const Bool hasFill, const Bool hasOutline, const Bool seeThroughWalls, const Uint8 colorIndex )
	{
		RED_FATAL_ASSERT( colorIndex < 16, "Color index out of bounds" );

		Uint8 typeMask = 0;
		if ( hasFill )			typeMask |= 1;
		if ( hasOutline )		typeMask |= 2;
		if ( seeThroughWalls )	typeMask |= 4;
		return ( typeMask << 4 ) | colorIndex;
	}

	void UnpackSelectionInfo( const Uint8 packed, Bool &hasFill, Bool &hasOutline, bool &seeThroughWalls, Uint8 &colorIndex )
	{
		hasFill = ( ( packed >> 4 ) & 1 ) != 0;
		hasOutline = ( ( packed >> 4 ) & 2 ) != 0;
		seeThroughWalls = ( ( packed >> 4 ) & 4 ) != 0;
		colorIndex = packed & 15u;
	}

	Bool DecodeSelInfo_IsSelected( Uint8 packedSelInfo )
	{
		return ( ( packedSelInfo >> 4 ) & 3 ) != 0;
	}

}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

/// Proxy to rendering scene object
class RENDER_DATA_API IRenderProxy : public IRenderObject
{
	RED_USE_MEMORY_POOL( rend::PoolRenderProxy );

public:
	IRenderProxy();
	virtual ~IRenderProxy();
	virtual ERenderProxyType GetType() const = 0;
	virtual void QueryVisible( Bool & /*visible*/ ) {}
	virtual void SetVisible( Bool /*visible*/ ) {}
	virtual void SetDissolveVisible( Bool /*visible*/, Uint8 /*force*/ ) {}
	virtual Bool CanOperateOnLOD() const { return false; }
	virtual Bool ShouldDetachWhenInvisible() const { return true; }

#ifndef NO_DEBUG_PAGES
	virtual void GetDescription( red::DynArray< red::String >& /*descriptionLines*/ ) const {}
	virtual Bool ValidateOptimization( red::DynArray< red::String >* /*commentLines*/ ) const { return true; }
#endif

	// Hack, used to communicate between physics and renderer
	virtual void AssignParticleSpawnDataArray( const red::SharedPtr<SParticleEmitterSimulationData>& data, Uint8 emitterIndex ) {}
	virtual void SetParticleCollisionContext( Uint8 emitterIndex, const red::SharedPtr<SParticlePhysicsSimulationDataDoubleBuffered>& context ) {}
	virtual red::SharedPtr<SParticlePhysicsSimulationDataDoubleBuffered>& GetParticleCollisionContextUpdate( Uint8 emitterIndex );
	virtual void InitAsync( const RenderProxyInitInfo& initInfo, job::Builder& builder ) {};
};

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

class IRenderSkinningData;
class IRenderFloatTrackData;
class CRenderIndexBuffer;
struct SParticleEffectContextUpdate;
struct SParticlePhysicsSimulationData;
struct SFogVolumeEffectContextUpdate;
struct SMaterialParameterEffectContextUpdate;
struct SLightParameterEffectContextUpdate;
struct SDecalParameterEffectContextUpdate;
struct SVertexShaderCustomSlotParams;
struct RenderDismCullObjectCB;
struct RenderRainData;

/// render proxy wrapper for client, abstracts the render commands done on proxy
class RENDER_DATA_API RenderProxyHandle
{
	RED_USE_MEMORY_POOL( rend::PoolRenderProxy_Handles );

public:
	RenderProxyHandle( const TRenderPtr<IRenderProxy>& proxy ); // creates the handle and attaches to scene
	virtual ~RenderProxyHandle(); // detaches from scene automatically

	// #NOTE: For internal use by RuntimeSystemRendering.
	void Internal_Unregister();

	void Initialize( const TRenderPtr<IRenderScene>& scene, Bool isInitiallyVisible, IRenderProxySystemInterface* systemInterface = nullptr );

	void Attach();

	/// are we still attach to the rendering scene
	RED_FORCE_INLINE const Bool IsAttached() const { return m_scene != nullptr; }

	/// check if the proxy is visible
	/// Note that the proxy can be invisible due to isolation mode
	const Bool IsVisible() const { return m_isVisibleExternal & m_isVisibleInFiltering; }

	/// move
	void Relink( const rend::RenderProxyTransform& localToWorld, const Box& worldBounds, Bool isTeleport = false );

	/// move and skin (tempshit)
	void RelinkAndSkin( const rend::RenderProxyTransform& localToWorld, const Box& worldBounds, const TRenderPtr<IRenderSkinningData>& skinningData, const TRenderPtr<IRenderFloatTrackData>& floatTrackData, Bool isTeleport = false );

	/// 
	bool RelinkInstances( red::ArraySpan<const rend::RenderProxyTransform> instances, const Box& worldBounds, Bool isTeleport = false );
	bool RelinkInstances( red::ArraySpan<const Matrix> instances, const Box& worldBounds, Bool isTeleport = false );
	void DisableClusteredProxyInstances( const red::BitSet64Dynamic&& clusteredProxyInstancesIndices );

	/// set new debug color for mesh render proxy
	void UpdateMeshDebugColor( const Color newDebugColor );

	/// move secondary reference point
	void UpdateSecondaryRefPoint( const Vector3& newSecondaryRefPoint );
	void SetAutohideDistanceNear( Float value );
	
	/// toggle casting local shadows along with option to keep proxy invisible in main sc
	void UpdateLocalShadowsInfo( Bool castLocalShadows, Bool castShadowsEvenIfNotVisibile );

	/// add parameters for vertex shader custom slot
	void AppendVertexShaderCustomSlotParams( const SVertexShaderCustomSlotParams& params );

	/// toggle selection effect
	void SetSelection( Bool select );

	/// toggle selection effect with more look control
	void SetSelection( ESelectionMode mode, Bool seeThroughWalls, ESelectionColor selectionColor = SC_Regular );

	/// get current selection effect
	void GetSelection( Bool& hasFill, Bool& outHasOutline, Bool& outSeeThroughWalls, Uint8& outColorIndex ) const;

	void SetVisionModeHighlight( const rend::VisionModeHighlightData& highlightData );
	void SetVisionModeScanState( rend::PostFx_ScanningState state );

	/// toggle proxy FULL visibility
	void SetVisibility(const Bool isVisible);

	/// toggle proxy visibility on dissolve effect
	void SetDissolveVisibility(const Bool isVisible, const FadeInForcingMode force = FadeInForcingMode::None);

	void SetVisibilityAfterFiltering( const Bool isVisible );

	/// toggle proxy lod forcing (use -1 to turn off proxy LOD forcing)
	void SetForcedLOD( Int8 lod );

	/// set distance scales for individual lod levels
	void SetLODLevelScales( Uint32 scales );

	/// bind to a selectable object
	void BindSelectable( const IRenderSelectable* selectable );

	/// Update isolation mode depth
	void SetIsolationModeDepth( const Int8 newDepth );

	/// Update isolation state based on the new isolation group selected
	/// The isolated proxy can be either faded out (checkerboard effect) or invisible
	void ApplyIsolation( const Int8 isolationDepth, RenderProxyIsolationMode mode );

	/// update effect simulation context for corresponding render proxy
	void UpdateEffectInfo( const SParticleEffectContextUpdate& updateInfo );
	void UpdateEffectInfo( const SFogVolumeEffectContextUpdate& updateInfo );
	void UpdateEffectInfo( const SMaterialParameterEffectContextUpdate& updateInfo );
	void UpdateEffectInfo( const SLightParameterEffectContextUpdate& updateInfo );
	void UpdateEffectInfo( const SDecalEffectContextUpdate& updateInfo );
	void UpdateEffectInfo( const SDestructionRegionsEffectContextUpdate& updateInfo );
	void UpdateEffectInfo( const SVehicleParamsUpdate& updateInfo );
	void SetDismembermentData( const RenderDismCullObjectCB* data );
	void SetRainData( RenderRainData& rainData);
    void UpdateClothMeshData(Uint8 chunk, red::DynArray<Vector3>&& positions, Box bounds) const;

	void ApplyMorphTargetWeights( const red::DynArray< MorphTargets::MorphTargetWeight >& weights );
	void UnloadMorphData();

	// Override some parameters in the materials used by a render proxy. Does not stack, overrides will be applied to
	// the proxy's original materials. Overrides are applied to all materials in the proxy that can use them (where the
	// material has a matching parameter definition);
	// #NOTE: Currently only mesh proxies are supported. Will assert if you give it another type.
	void SetMaterialParameterOverrides( SMaterialSetupParameters&& params );

	void UpdateFlickerParams(const SLightFlickerParamsUpdate& updateInfo);

	void Editor_ForceAutoHideDistance( Bool forceNoAutohideDistance, Bool isGlobal = false );

	// Hack, used to communicate between physics and renderer
	void AssignParticleSpawnDataArray( const red::SharedPtr<SParticleEmitterSimulationData>&, Uint8 emitterIndex );
	void SetParticleCollisionContext( Uint8 emitterIndex, const red::SharedPtr<SParticlePhysicsSimulationDataDoubleBuffered>& context );

	void SetMeshChunkMask(Uint64 meshChunkMask);
	void SetIndexBufferOverride( const TRenderPtr<IRenderResource>& ibOverride );

	void SetMeshMaterialOverlay( const TRenderPtr<IRenderResource>& material, const TRenderPtr<IRenderResource>& materialParam = nullptr, bool overrideMaterial = false );

	ERenderProxyType GetProxyType() const { return m_proxy->GetType(); }

	// NOTE: Internally its doing a copy of RenderingStats. Assumed to be used only in editor when the info is requested.
	//		 If more stats would be needed we could go with general proxy stats structure that does not need to copy all stats,
	//       only reads what it needs to.
	Bool GetNumberOfTrianglesRenderedToShadowMaps( LocalShadowsTrianglesCount& trianglesCount );

	Uint64 GetRenderProxyAddressForDebug() const { return reinterpret_cast<Uint64>(m_proxy.Get()); }

private:
	TRenderPtr<IRenderScene> m_scene;
	TRenderPtr<IRenderProxy> m_proxy;

	IRenderProxySystemInterface* m_systemInterface;

	// Encoded selection params: 1-unused | 3-selType | 4-selColor. Will end up directly in a stencil buffer.
	Uint8				m_selectionInfo;
	Uint8				m_visionModeHighlight;
	rend::PostFx_ScanningState	m_visionModeScanState;

	Bool				m_isAttached : 1;
	Bool				m_isVisibleExternal : 1; // set by SetVisibility
	Bool				m_isVisibleInFiltering : 1;
	Bool				m_isInvisibleButStillAttached : 1;

	// When scene is unavailable, defer local shadows update until the component is attached
	Bool				m_deferredLocalShadowsUpdate : 1;
	// Store necessary state for deferred local shadow update
	Bool				m_castLocalShadows : 1;
	Bool				m_castShadowsEvenIfNotVisibile : 1;

	void UpdateVisibility();
	void Detach();
};

typedef red::SharedPtr< RenderProxyHandle > RenderProxyPtr;
