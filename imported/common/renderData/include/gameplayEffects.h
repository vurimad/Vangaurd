/**
* Copyright (c) 2017 CD Projekt Red. All Rights Reserved.
*/
#pragma once

#include "renderPostFx.h"
#include "renderProxy.h"

namespace rend
{
	struct RENDER_DATA_API DistantCrowdMember
	{
		Vector3 position;
		Vector3 forward;
		Float info;
	};

	// simple interface for accessing crowd data produced by gameplay code 
	// idea is that gameplay just calls this DistantCrowdData::Set() method whenever it has any data to update
	// @Techart guys: feel free to change it to whatever you like, or what best suits your needs 
	// You can send a render command directly in Set(), if you like, or just do whatever.
	class RENDER_DATA_API DistantCrowdData
	{
		RED_USE_MEMORY_POOL( rend::PoolPostProcessAndEffects );

	public:
		constexpr static Uint32 c_maxNumMembers = 5400;

		// this function is meant to be used by gameplay code to fill the data buffer
		void Send( RenderProxyPtr renderingProxy, const red::DynArray<Float>* data );
	};


	// #TODO: When there's some meaningful stuff in here, it could be exposed to scripts to allow people to control the
	// effects better.
	class RENDER_DATA_API GameplayEffects
	{
		RED_USE_MEMORY_POOL( rend::PoolPostProcessAndEffects );

	public:
		RED_INLINE static Uint8 VisionMode_PackHighlightInfo( const Bool seeThroughWalls, const Bool isPrimaryPattern, const Uint8 fillIndex, const Uint8 outlineIndex )
		{
			return PostFXEffectParameters::CPVisionMode::PackHighlightInfo( seeThroughWalls, isPrimaryPattern, fillIndex, outlineIndex );
		}

	public:
		GameplayEffects();


		////////////////////////////////////////////////////////////////////////////////////////////////////////////////
		// Vision Modes

		// Set whether Focus Mode is enabled. This gives a fullscreen treatment, with highlights around designated
		// objects. When disabled, objects can still be highlighted, but there is no fullscreen treatment.
		void VisionMode_SetFocusModeEnabled( Bool enabled );

		// Set whether Braindance is currently active, which has an effect on the highlights, to keep them visible.
		// Default is false.
		void VisionMode_SetBraindanceEnabled( Bool enabled );

		// Set highlight state for a given render proxy. A value for highlightInfo can be generated with
		// VisionMode_PackHighlightInfo().
		void VisionMode_SetHighlight( const TRenderPtr<IRenderScene>& scene, const TRenderPtr<IRenderProxy>& proxy, Bool isHighlighted, Uint8 highlightInfo, Float opacityValue, Bool initial );

		// If transitioning to Complete, any objects that are currently scanning will be put in "scanned" state until
		// set otherwise.
		// #TODO: If we need to support multiple ongoing scans, could take in a scan ID or something.
		void Scanning_SetScanProgress( PostFx_ScanningState state, Float progress );

		// Set the scanning state of a specific object.
		//    Off:        Object will be highlighted according to normal highlight rules.
		//    Scanning:   Object highlight will be synchronized with the current scan progress.
		//    Cancelling: Object was being scanned but scan was interrupted. 
		//    Complete:   Scan was complete on object. Will not be synchronized with any current scan.
		// #TODO: If we need to support multiple ongoing scans, could take in a scan ID or something, to say which scan
		// to synchronize with.
		void Scanning_SetProxyScanningState( const TRenderPtr<IRenderScene>& scene, const TRenderPtr<IRenderProxy>& proxy, PostFx_ScanningState state );

		////////////////////////////////////////////////////////////////////////////////////////////////////////////////
		// Sandevistan / Kereznikov

		void Sandevistan_Activate( CName reason );
		void Sandevistan_Deactivate( CName reason );


		////////////////////////////////////////////////////////////////////////////////////////////////////////////////
		// Cyberspace
		// In order to store CyberspacePixelsort effect in game save use IRenderGameplayEffectsManagerSystem wrapper instead of direct call this methods

		void CyberspacePixelsort_Activate(Bool fullscreen = true, Bool vfx = false, Float initialDatamosh = 0.0f, Float targetDatamosh = 0.0f, Float initialIntensity = 0.0f, Float targetIntensity = 0.3, Float timeBlend = 5.0f );
		void CyberspacePixelsort_Deactivate();

		////////////////////////////////////////////////////////////////////////////////////////////////////////////////
		// Uber-quality screen-space reflections for Q202
		
		void EnforceScreenSpaceReflectionsUberQuality( Bool enable );

		////////////////////////////////////////////////////////////////////////////////////////////////////////////////
		// Distant crowd

		DistantCrowdData& GetDistantCrowdData();

		////////////////////////////////////////////////////////////////////////////////////////////////////////////////

		// No copy, no move.
		GameplayEffects(const GameplayEffects&) = delete;
		GameplayEffects(GameplayEffects&&) = delete;
		GameplayEffects& operator =(const GameplayEffects&) = delete;
		GameplayEffects& operator =(GameplayEffects&&) = delete;

	private:

		PostFXEffectParameters m_visionModeParams;

		red::DynArray<CName> m_sandevistanActivations{ PoolPostProcessAndEffects() };

		red::LightMutex m_mutex;

		DistantCrowdData m_distantCrowdData;
	};

}
