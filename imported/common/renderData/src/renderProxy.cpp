/**
* Copyright (c) 2007 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"
#include "renderProxy.h"
#include "renderer.h"
#include "renderSkinningData.h"
#include "gameplayEffects.h"
#include "renderCommandHandler.h"
#include "renderScene.h"
#include "renderProxyInitData.h"
#include "renderStats.h"
#include "effectContextUpdateData.h"

RenderProxyInitInfo::RenderProxyInitInfo()
	: m_componentData( nullptr )
#ifndef RED_CONFIGURATION_FINAL
	, m_initInfoStateCheck( true )
#endif
{
}

Box RenderProxyInitInfo::ExtractBoundingBox() const
{
	// From component
	if ( m_componentData )
	{
		return m_componentData->m_boundingBox;
	}

	// None 
	return Box();
}

rend::RenderProxyTransform RenderProxyInitInfo::ExtractLocalToWorld() const
{
	// From component
	if ( m_componentData )
	{
		return m_componentData->m_localToWorld;
	}

	// None
	return rend::RenderProxyTransform::IDENTITY();
}

IRenderProxy::IRenderProxy()
{
}

IRenderProxy::~IRenderProxy() = default;

red::SharedPtr<SParticlePhysicsSimulationDataDoubleBuffered>& IRenderProxy::GetParticleCollisionContextUpdate( Uint8 emitterIndex )
{
	static red::SharedPtr<SParticlePhysicsSimulationDataDoubleBuffered> hack;
	return hack;
}

//---

RenderProxyHandle::RenderProxyHandle( const TRenderPtr<IRenderProxy>& proxy )
	: m_proxy( proxy )
	, m_selectionInfo( 0 )
	, m_visionModeHighlight( 0 )
	, m_visionModeScanState( rend::PostFx_ScanningState::Off )
	, m_isVisibleExternal( true )
	, m_systemInterface( nullptr )
	, m_isVisibleInFiltering( true )
	, m_isAttached( false )
	, m_isInvisibleButStillAttached(false)
	, m_deferredLocalShadowsUpdate(false)
{
	RED_FATAL_ASSERT( m_proxy );
}

RenderProxyHandle::~RenderProxyHandle()
{
	Internal_Unregister();
}


void RenderProxyHandle::Internal_Unregister()
{
	// Unregister first. If runtime system has us in the pending attaches, we need to make sure that gets cleared up
	// (either by actually scheduling the attach, or by removing us from the list) before we do anything else.
	if ( m_systemInterface )
	{
		m_systemInterface->OnRenderProxyHandleDestroyed( this );
		m_systemInterface = nullptr;
	}

	Detach();

	m_scene = nullptr;
	m_proxy = nullptr;
}


void RenderProxyHandle::Initialize( const TRenderPtr<IRenderScene>& scene, Bool isInitiallyVisible, IRenderProxySystemInterface* systemInterface )
{
	RED_FATAL_ASSERT( scene );

	m_scene = scene;
	m_isVisibleExternal= isInitiallyVisible;
	m_systemInterface = systemInterface;
}

void RenderProxyHandle::Attach()
{
	RED_FATAL_ASSERT( !m_isAttached, "Render proxy is already attached" );

	if ( IsVisible() )
	{
		GRender->GetCommands()->AddProxyToScene( m_scene, m_proxy );

		if ( m_selectionInfo != 0 )
			GRender->GetCommands()->SetSelectionFlag( m_scene, m_proxy, true, m_selectionInfo );

		if ( m_visionModeHighlight != 0 )
			GRender->GetGameplayEffects().VisionMode_SetHighlight( m_scene, m_proxy, true, m_visionModeHighlight, 1.0f, false );

		if ( m_visionModeScanState != rend::PostFx_ScanningState::Off )
			GRender->GetGameplayEffects().Scanning_SetProxyScanningState( m_scene, m_proxy, m_visionModeScanState );
	}

	m_isAttached = true;

	if ( m_deferredLocalShadowsUpdate )
	{
		m_deferredLocalShadowsUpdate = false;
		UpdateLocalShadowsInfo( m_castLocalShadows, m_castShadowsEvenIfNotVisibile );
	}
}

void RenderProxyHandle::Detach()
{
	if ( m_isAttached )
	{
		if ( m_selectionInfo != 0 )
			GRender->GetCommands()->SetSelectionFlag( m_scene, m_proxy, false, m_selectionInfo );

		if ( m_visionModeHighlight != 0 )
			GRender->GetGameplayEffects().VisionMode_SetHighlight( m_scene, m_proxy, false, m_visionModeHighlight, 1.0f, false );

		if ( m_visionModeScanState != rend::PostFx_ScanningState::Off )
			GRender->GetGameplayEffects().Scanning_SetProxyScanningState( m_scene, m_proxy, rend::PostFx_ScanningState::Off );

		if ( IsVisible() || m_isInvisibleButStillAttached )
			GRender->GetCommands()->RemoveProxyFromScene( m_scene, m_proxy );

		m_isInvisibleButStillAttached = false;
		m_isAttached = false;
	}
}

void RenderProxyHandle::RelinkAndSkin( const rend::RenderProxyTransform& localToWorld, const Box& worldBounds, const TRenderPtr<IRenderSkinningData>& skinningData, const TRenderPtr<IRenderFloatTrackData>& floatTrackData, Bool isTeleport )
{
	RED_FATAL_ASSERT( worldBounds.IsOk() );
	if( m_scene )
	{
		const IRenderScene::ScheduleRelinkParameter relinkParams( m_proxy.Get(), localToWorld, worldBounds, skinningData.Get(), floatTrackData.Get(), isTeleport );
		m_scene->ScheduleRelink( relinkParams );
	}
}

void RenderProxyHandle::Relink( const rend::RenderProxyTransform& localToWorld, const Box& worldBounds, Bool isTeleport )
{
	RED_FATAL_ASSERT( worldBounds.IsOk() );
	if( m_scene )
	{
		const IRenderScene::ScheduleRelinkParameter relinkParams( m_proxy.Get(), localToWorld, worldBounds, isTeleport );
		m_scene->ScheduleRelink( relinkParams );
	}
	
}

bool RenderProxyHandle::RelinkInstances( red::ArraySpan<const rend::RenderProxyTransform> instances, const Box& worldBounds, Bool isTeleport )
{
	RED_FATAL_ASSERT( worldBounds.IsOk() );
	if( m_scene )
	{
		const IRenderScene::ScheduleRelinkParameter relinkParams( m_proxy.Get(), worldBounds, instances, isTeleport );
		return m_scene->ScheduleRelink( relinkParams );
	}

	return false;
}

bool RenderProxyHandle::RelinkInstances( red::ArraySpan<const Matrix> instances, const Box& worldBounds, Bool isTeleport )
{
	RED_FATAL_ASSERT( worldBounds.IsOk() );
	if ( m_scene )
	{
		const IRenderScene::ScheduleRelinkParameter relinkParams( m_proxy.Get(), worldBounds, instances, isTeleport );
		return m_scene->ScheduleRelink( relinkParams );
	}

	return false;
}

void RenderProxyHandle::DisableClusteredProxyInstances( const red::BitSet64Dynamic&& clusteredProxyInstancesIndices )
{
	GRender->GetCommands()->DisableClusteredProxyInstances( m_proxy, clusteredProxyInstancesIndices );
}

void RenderProxyHandle::UpdateMeshDebugColor( const Color newDebugColor )
{
	GRender->GetCommands()->UpdateMeshDebugColor( m_proxy, newDebugColor );
}

void RenderProxyHandle::UpdateSecondaryRefPoint( const Vector3& newSecondaryRefPoint )
{
	GRender->GetCommands()->UpdateSecondaryRefPoint( m_proxy, newSecondaryRefPoint );
}

void RenderProxyHandle::SetAutohideDistanceNear( Float value )
{
	GRender->GetCommands()->SetAutohideDistanceNear( m_proxy, value );
}

void RenderProxyHandle::UpdateLocalShadowsInfo( Bool castLocalShadows, Bool castShadowsEvenIfNotVisibile )
{
	if ( m_isAttached )
	{
		GRender->GetCommands()->UpdateLocalShadowsInfo( m_scene, m_proxy, castLocalShadows, castShadowsEvenIfNotVisibile );
	}
	else // Deferred local shadow update on OnAttach
	{
		m_deferredLocalShadowsUpdate = true;
		m_castLocalShadows = castLocalShadows;
		m_castShadowsEvenIfNotVisibile = castShadowsEvenIfNotVisibile;
	}
}

void RenderProxyHandle::AppendVertexShaderCustomSlotParams( const SVertexShaderCustomSlotParams& params )
{
	GRender->GetCommands()->AppendVertexShaderCustomSlotParams( m_proxy, params );
}

void RenderProxyHandle::SetVisibility( const Bool isVisible )
{
	const Bool wasVisible = IsVisible();
	m_isVisibleExternal = isVisible;

	if ( IsVisible() != wasVisible )
		UpdateVisibility();
}

void RenderProxyHandle::SetDissolveVisibility( const Bool isVisible, const FadeInForcingMode force )
{
	GRender->GetCommands()->SetProxyDissolveVisibility( m_proxy, isVisible, static_cast< Uint8 > ( force ) );
}

void RenderProxyHandle::SetVisibilityAfterFiltering( const Bool isVisible )
{
	const Bool wasVisible = IsVisible();
	m_isVisibleInFiltering = isVisible;

	if ( IsVisible() != wasVisible )
		UpdateVisibility();
}

void RenderProxyHandle::SetForcedLOD( Int8 lod )
{
	if ( m_proxy->CanOperateOnLOD() )
	{
		GRender->GetCommands()->ForceLODForProxy( m_proxy, lod );
	}
}

void RenderProxyHandle::SetLODLevelScales( Uint32 scales )
{
	if( m_proxy->CanOperateOnLOD() )
	{
		GRender->GetCommands()->LODLevelScalesForProxy( m_proxy, scales );
	}
}

void RenderProxyHandle::UpdateVisibility()
{
	if ( !m_isAttached )
		return;

	Bool isVisible = IsVisible();
	m_proxy->SetVisible( isVisible );

	if ( isVisible )
	{
		if (!m_isInvisibleButStillAttached)
		{
			GRender->GetCommands()->AddProxyToScene(m_scene, m_proxy);
		}
		// the proxy is visible either way this is not true anymore
		m_isInvisibleButStillAttached = false;

		if ( m_selectionInfo != 0 )
			GRender->GetCommands()->SetSelectionFlag( m_scene, m_proxy, true, m_selectionInfo );
		if ( m_visionModeHighlight != 0 )
			GRender->GetGameplayEffects().VisionMode_SetHighlight( m_scene, m_proxy, true, m_visionModeHighlight, 1.0f, false );
		if ( m_visionModeScanState != rend::PostFx_ScanningState::Off )
			GRender->GetGameplayEffects().Scanning_SetProxyScanningState( m_scene, m_proxy, m_visionModeScanState );
	}
	else // should be hidden
	{
		if ( m_selectionInfo != 0 )
			GRender->GetCommands()->SetSelectionFlag( m_scene, m_proxy, false, m_selectionInfo );
		if ( m_visionModeHighlight != 0 )
			GRender->GetGameplayEffects().VisionMode_SetHighlight( m_scene, m_proxy, false, m_visionModeHighlight, 1.0f, false );
		if ( m_visionModeScanState != rend::PostFx_ScanningState::Off )
			GRender->GetGameplayEffects().Scanning_SetProxyScanningState( m_scene, m_proxy, rend::PostFx_ScanningState::Off );

		// there are mesh proxies that render shadow even if they are not visible
		if (m_proxy->ShouldDetachWhenInvisible())
		{
			GRender->GetCommands()->RemoveProxyFromScene(m_scene, m_proxy);
		}
		else
		{
			m_isInvisibleButStillAttached = true;
		}
	}
}

void RenderProxyHandle::UpdateEffectInfo( const SParticleEffectContextUpdate& updateInfo )
{
	GRender->GetCommands()->UpdateParticleEffectContext( m_proxy, updateInfo );
}

void RenderProxyHandle::UpdateEffectInfo( const SFogVolumeEffectContextUpdate& updateInfo )
{
	GRender->GetCommands()->UpdateFogVolumeEffectContext( m_proxy, updateInfo );
}

void RenderProxyHandle::UpdateEffectInfo( const SMaterialParameterEffectContextUpdate& updateInfo )
{
	GRender->GetCommands()->UpdateMaterialParameterEffectContext( m_proxy, updateInfo );
}

void RenderProxyHandle::UpdateEffectInfo(const SLightParameterEffectContextUpdate& updateInfo)
{
	GRender->GetCommands()->UpdateLightParameterEffectContext( m_proxy, updateInfo );
}

void RenderProxyHandle::UpdateEffectInfo( const SDecalEffectContextUpdate& updateInfo )
{
	GRender->GetCommands()->UpdateDecalEffectContext(m_proxy, updateInfo);
}

void RenderProxyHandle::UpdateEffectInfo( const SDestructionRegionsEffectContextUpdate& updateInfo )
{
	GRender->GetCommands()->UpdateDestructionRegionsEffectContext(m_proxy, updateInfo);
}

void RenderProxyHandle::UpdateEffectInfo( const SVehicleParamsUpdate& updateInfo )
{
	GRender->GetCommands()->UpdateVehicleParams( m_proxy, updateInfo );
}

void RenderProxyHandle::UpdateFlickerParams( const SLightFlickerParamsUpdate& updateInfo )
{
	GRender->GetCommands()->UpdateLightFlickerParams( m_proxy, updateInfo );
}

void RenderProxyHandle::SetDismembermentData( const RenderDismCullObjectCB* data )
{
	GRender->GetCommands()->Add_SetDismembermentData( m_proxy, data );
}

void RenderProxyHandle::SetRainData( RenderRainData& rainData )
{
	GRender->GetCommands()->SetRainData( m_proxy, rainData );
}

void RenderProxyHandle::UpdateClothMeshData(Uint8 chunk, red::DynArray<Vector3>&& positions, Box bounds) const
{
    GRender->GetCommands()->UpdateClothMeshData(m_proxy, chunk, std::move(positions), bounds);
}

void RenderProxyHandle::ApplyMorphTargetWeights( const red::DynArray< MorphTargets::MorphTargetWeight >& weights )
{
	GRender->GetCommands()->ApplyMorphTargetWeights( m_proxy, weights );
}

void RenderProxyHandle::UnloadMorphData()
{
	GRender->GetCommands()->UnloadMorphData( m_proxy );
}

void RenderProxyHandle::SetMaterialParameterOverrides( SMaterialSetupParameters&& params )
{
	GRender->GetCommands()->SetMaterialParameterOverrides( m_proxy, std::move(params) );
}

void RenderProxyHandle::SetSelection( Bool select )
{
	// Default selection for editor
	SetSelection( select ? SM_FillAndOutline : SM_None, true );
}

void RenderProxyHandle::SetSelection( ESelectionMode mode, Bool seeThroughWalls, ESelectionColor selectionColor )
{
	if ( m_selectionInfo )
	{
		// Just disable previous selection
		if ( m_isAttached && IsVisible() )
		{
			GRender->GetCommands()->SetSelectionFlag( m_scene, m_proxy, false, m_selectionInfo );
		}
		m_selectionInfo = 0;
	}

	if ( mode != SM_None )
	{
		m_selectionInfo = PackSelectionInfo( mode == SM_Fill || mode == SM_FillAndOutline, mode == SM_Outline || mode == SM_FillAndOutline,
			seeThroughWalls, (Uint8)selectionColor );
		if ( m_isAttached && IsVisible() )
		{
			GRender->GetCommands()->SetSelectionFlag( m_scene, m_proxy, true, m_selectionInfo );
		}
	}
}

void RenderProxyHandle::GetSelection( Bool& outHasFill, Bool& outHasOutline, Bool& outSeeThroughWalls, Uint8& outColorIndex ) const
{
	UnpackSelectionInfo( m_selectionInfo, outHasFill, outHasOutline, outSeeThroughWalls, outColorIndex );
}

void RenderProxyHandle::SetVisionModeHighlight( const rend::VisionModeHighlightData& highlightData )
{
	if ( highlightData.m_fillIndex != 0 || highlightData.m_outlineIndex != 0 )
	{
		m_visionModeHighlight = rend::GameplayEffects::VisionMode_PackHighlightInfo( highlightData.m_seeThroughWalls, highlightData.m_isPrimaryPattern, highlightData.m_fillIndex, highlightData.m_outlineIndex );
		if ( m_isAttached && IsVisible() )
		{
			GRender->GetGameplayEffects().VisionMode_SetHighlight( m_scene, m_proxy, true, m_visionModeHighlight, highlightData.m_opacity, highlightData.m_initial );
		}
	}
	else if ( m_visionModeHighlight )
	{
		// Just disable previous selection
		if ( m_isAttached && IsVisible() )
		{
			GRender->GetGameplayEffects().VisionMode_SetHighlight( m_scene, m_proxy, false, m_visionModeHighlight, highlightData.m_opacity, highlightData.m_initial );
		}
		m_visionModeHighlight = 0;
	}
}

void RenderProxyHandle::SetVisionModeScanState( rend::PostFx_ScanningState state )
{
	if ( m_visionModeScanState != state )
	{
		m_visionModeScanState = state;
		if ( m_isAttached && IsVisible() )
		{
			GRender->GetGameplayEffects().Scanning_SetProxyScanningState( m_scene, m_proxy, state );
		}
	}
}

void RenderProxyHandle::BindSelectable( const IRenderSelectable* selectable )
{
	GRender->GetCommands()->UpdateHitProxyID( m_proxy, selectable );
}

void RenderProxyHandle::Editor_ForceAutoHideDistance( Bool forceNoAutohideDistance, Bool isGlobal /*= false*/ )
{
#ifndef NO_EDITOR
	if ( !IsGameMode() )
	{
		GRender->GetCommands()->EditorForceAutoHideDistance( m_proxy, forceNoAutohideDistance, isGlobal );
	}
#endif
}

void RenderProxyHandle::AssignParticleSpawnDataArray( const red::SharedPtr<SParticleEmitterSimulationData>& data, Uint8 emitterIndex )
{
	m_proxy->AssignParticleSpawnDataArray( data, emitterIndex );
}

void RenderProxyHandle::SetParticleCollisionContext( Uint8 emitterIndex, const red::SharedPtr<SParticlePhysicsSimulationDataDoubleBuffered>& context )
{
	m_proxy->SetParticleCollisionContext( emitterIndex, context );
}

void RenderProxyHandle::SetMeshChunkMask(Uint64 meshChunkMask)
{
	RED_FATAL_ASSERT(m_proxy->GetType() == RPT_Mesh, "mesh chunk masks are only applicable to meshes");
	GRender->GetCommands()->SetMeshChunkMask(m_proxy, meshChunkMask);
}

void RenderProxyHandle::SetIndexBufferOverride( const TRenderPtr<IRenderResource>& ibOverride )
{
	RED_FATAL_ASSERT( m_proxy->GetType() == RPT_Mesh, "index buffer override is only applicable to meshes" );
	GRender->GetCommands()->SetIndexBufferOverride( m_proxy, ibOverride );
}

void RenderProxyHandle::SetMeshMaterialOverlay( const TRenderPtr<IRenderResource>& material, const TRenderPtr<IRenderResource>& materialParam, bool overrideMaterial )
{
	RED_FATAL_ASSERT( m_proxy->GetType() == RPT_Mesh || m_proxy->GetType() == RPT_MorphTargetMesh, "Material overlay can be only applied to meshes" );
	GRender->GetCommands()->SetMeshMaterialOverlay( m_proxy, material, materialParam, overrideMaterial );
}

Bool RenderProxyHandle::GetNumberOfTrianglesRenderedToShadowMaps( LocalShadowsTrianglesCount& trianglesCount )
{
	RenderingStats rs;
	m_scene->GetRenderingStats( rs );
	
	return rs.m_shadowCastingLightsStats.GetNumOfShadowTrianglesRendered( m_proxy.Get(), trianglesCount );
}
