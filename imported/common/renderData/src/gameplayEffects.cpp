/**
* Copyright (c) 2017 CD Projekt Red. All Rights Reserved.
*/
#include "build.h"
#include "gameplayEffects.h"
#include "renderPostFx.h"
#include "renderer.h"
#include "renderCommandHandler.h"
#include "effectContextUpdateData.h"
#include "renderSkinningData.h"



// #TODO: Might be good to batch all updates internally, and then flush them in a single "SetupGameplayEffects" render
// command once per frame.


namespace rend
{
	////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
	// Distant crowd
	void DistantCrowdData::Send( RenderProxyPtr renderingProxy, const red::DynArray<Float>* data )
	{
		if ( renderingProxy.Get( ))
		{
			renderingProxy->SetVisibility(false);

			if (data)
			{
				renderingProxy->SetVisibility(true);
				SDestructionRegionsEffectContextUpdate updateInfo = { *data };
				renderingProxy->UpdateEffectInfo(updateInfo);
			};
		};
	}

	rend::DistantCrowdData& GameplayEffects::GetDistantCrowdData()
	{
		return m_distantCrowdData;
	}

	////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
	// Gameplay effects

	GameplayEffects::GameplayEffects()
		: m_visionModeParams(EPE_CPFocusMode)
	{
	}


	////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
	// Vision Modes


	void GameplayEffects::VisionMode_SetFocusModeEnabled( Bool enabled )
	{
		RED_SCOPE_LOCK( m_mutex );

		if ( m_visionModeParams.cpVisionMode.focusEnabled != enabled )
		{
			m_visionModeParams.cpVisionMode.focusEnabled = enabled;
			GRender->GetCommands()->SetGameplayEffectParameters( m_visionModeParams );
		}
	}

	void GameplayEffects::VisionMode_SetBraindanceEnabled( Bool enabled )
	{
		RED_SCOPE_LOCK( m_mutex );

		if ( m_visionModeParams.cpVisionMode.braindanceEnabled != enabled )
		{
			m_visionModeParams.cpVisionMode.braindanceEnabled = enabled;
			GRender->GetCommands()->SetGameplayEffectParameters( m_visionModeParams );
		}
	}

	void GameplayEffects::VisionMode_SetHighlight( const TRenderPtr<IRenderScene>& scene, const TRenderPtr<IRenderProxy>& proxy, Bool isHighlighted, Uint8 highlightInfo, Float opacityValue, Bool initial )
	{
		GRender->GetCommands()->SetVisionModeHighlight( scene, proxy, isHighlighted, highlightInfo, opacityValue, initial );
	}

	void GameplayEffects::Scanning_SetScanProgress( PostFx_ScanningState state, Float progress )
	{
		RED_SCOPE_LOCK( m_mutex );

		if ( m_visionModeParams.cpVisionMode.scanState != state || !math::AlmostEquals( m_visionModeParams.cpVisionMode.scanProgress, progress ) )
		{
			m_visionModeParams.cpVisionMode.scanState = state;
			m_visionModeParams.cpVisionMode.scanProgress = progress;
			GRender->GetCommands()->SetGameplayEffectParameters( m_visionModeParams );
		}
	}

	void GameplayEffects::Scanning_SetProxyScanningState( const TRenderPtr<IRenderScene>& scene, const TRenderPtr<IRenderProxy>& proxy, PostFx_ScanningState state )
	{
		GRender->GetCommands()->SetVisionModeScanning( scene, proxy, state );
	}

	void GameplayEffects::CyberspacePixelsort_Activate(Bool fullscreen, Bool vfx, Float initialDatamosh, Float targetDatamosh, Float initialIntensity, Float targetIntensity, Float timeBlend/*, Float datamosh*/)
	{	
		RED_SCOPE_LOCK( m_mutex );
		{
			GRender->GetCommands()->ToggleGameplayEffect(EPE_CPCyberspacePixelsort, true);

			PostFXEffectParameters params;
			params.effect = EPE_CPCyberspacePixelsort;
			
			params.cpCyberspace.m_mode = fullscreen ? PostFXEffectParameters::CyberspaceMode::Fullscreen
				: PostFXEffectParameters::CyberspaceMode::StencilCut;
			params.cpCyberspace.m_vfx = vfx ? PostFXEffectParameters::CyberspaceMode::Vfx
				: PostFXEffectParameters::CyberspaceMode::StencilCut;
			params.cpCyberspace.m_initialDatamosh = initialDatamosh;
			params.cpCyberspace.m_targetDatamosh = targetDatamosh;
				
			params.cpCyberspace.m_currentTreshold = initialIntensity;
			params.cpCyberspace.m_targetTreshold = targetIntensity;
			params.cpCyberspace.m_blendTime = Max<Float>(0.0f, timeBlend);

			GRender->GetCommands()->SetGameplayEffectParameters(params);
		}
	}

	void GameplayEffects::CyberspacePixelsort_Deactivate()
	{
		RED_SCOPE_LOCK( m_mutex );
		{
			PostFXEffectParameters params;
			params.effect = EPE_CPCyberspacePixelsort;
			params.cpCyberspace.m_currentTreshold = 0.0f;
			params.cpCyberspace.m_initialDatamosh = 0.0f;
			params.cpCyberspace.m_targetDatamosh = 0.0f;
			GRender->GetCommands()->SetGameplayEffectParameters(params);
			GRender->GetCommands()->ToggleGameplayEffect(EPE_CPCyberspacePixelsort, false);
		}
	}

	void GameplayEffects::EnforceScreenSpaceReflectionsUberQuality( Bool enable )
	{
		RED_SCOPE_LOCK( m_mutex );
		{
			GRender->GetCommands()->EnforceScreenSpaceReflectionsUberQuality( enable );
		}
	}

	////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
	// Sandevistan / Kereznikov


	// #TODO: Handle this better, might be good to not have hard-coded names here. It's coming from the
	// TimeDilation system, maybe it would be better to have it look up appropriate values from some
	// resource or something. Leaving simple like this for now, since the only parameter we have is to
	// switch between the two modes, and it's probably simple and specific enough to not warrant some
	// elaborate generalized thing. And maybe eventually these could be switched to different effects (if
	// they end up needing different treatments instead of just a change in strength), in which case this
	// would be handled in another way anyways.
	static PostFXEffectParameters MakeSandevistanParameters( CName reason )
	{
		PostFXEffectParameters params( EPE_CPSandevistan );
		if ( reason == RED_NAME_CONSTEXPR_NOREG( RED_STRINGIFY( sandevistan ) ) )
		{
			params.cpSandevistan.mode = PostFXEffectParameters::SandevistanMode::Default;
		}
		else if ( reason == RED_NAME_CONSTEXPR_NOREG( RED_STRINGIFY( kereznikov ) ) )
		{
			params.cpSandevistan.mode = PostFXEffectParameters::SandevistanMode::Kereznikov;
		}
		else
		{
			RED_LOG_ERROR("GameplayEffects: Unhandled sandevistan reason '%ls'. Setting default parameters.", reason.AsChar());
			params.cpSandevistan.mode = PostFXEffectParameters::SandevistanMode::Default;
		}
		return params;
	}


	void GameplayEffects::Sandevistan_Activate( CName reason )
	{
		if( reason != RED_NAME_CONSTEXPR_NOREG( RED_STRINGIFY( sandevistan ) ) && reason != RED_NAME_CONSTEXPR_NOREG( RED_STRINGIFY( kereznikov ) ) )		// !! Name is kerenzikov but due to misunderstanding it was used as kereznikov, renames are ongoing and thus it might stop working
		{
			return;
		}
		
		RED_SCOPE_LOCK( m_mutex );

		if ( m_sandevistanActivations.Exist(reason) )
		{
			return;
		}

		// If it's the first activation, enable the effect. Otherwise it's already been enabled, so we can just update
		// parameters.
		if ( m_sandevistanActivations.Empty() )
		{
			GRender->GetCommands()->ToggleGameplayEffect( EPE_CPSandevistan, true );
		}

		m_sandevistanActivations.PushBack( reason );

		PostFXEffectParameters sandevistanParameters = MakeSandevistanParameters( reason );
		GRender->GetCommands()->SetGameplayEffectParameters( sandevistanParameters );
	}

	void GameplayEffects::Sandevistan_Deactivate( CName reason )
	{
		RED_SCOPE_LOCK( m_mutex );

		if ( !m_sandevistanActivations.Remove( reason ).IsSuccessful() )
		{
			return;
		}

		// If that was the last one, turn off the effect. Otherwise set parameters based on the next one in line.
		if ( m_sandevistanActivations.Empty() )
		{
			GRender->GetCommands()->ToggleGameplayEffect( EPE_CPSandevistan, false );
		}
		else
		{
			PostFXEffectParameters sandevistanParameters = MakeSandevistanParameters( m_sandevistanActivations.Back() );
			GRender->GetCommands()->SetGameplayEffectParameters( sandevistanParameters );
		}
	}

	

}
