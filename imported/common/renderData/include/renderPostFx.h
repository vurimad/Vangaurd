/**
* Copyright (C) 2016 CD Projekt Red. All Rights Reserved.
*/
#pragma once


namespace rend
{
	enum class PostFx_ScanningState : Uint8
	{
		Off,
		QuickScan,
		Scanning,
		Cancelled,
		Complete
	};
}

RTTI_DECLARE_ENUM_IN_NAMESPACE( PostFx_ScanningState, rend );


// Types of gameplay post FX's
// Order of these enums is used to process and render effects
enum EPostFXEffect : Uint32
{
	EPE_CPFocusMode,
	EPE_CPSandevistan,
	EPE_CPCyberspacePixelsort,
	
	EPE_MAX
};

namespace rend
{
	struct RENDER_DATA_API VisionModeHighlightData
	{
		VisionModeHighlightData( Bool seeThroughWalls, Bool isPrimaryPattern, Uint8 fillIndex, Uint8 outlineIndex, Float opacity, Bool initial );

	public:
		const Bool m_seeThroughWalls;
		const Bool m_isPrimaryPattern;
		const Uint8 m_fillIndex;
		const Uint8 m_outlineIndex;
		const Float m_opacity;
		const Bool m_initial;
	};
}


// #TODO: Might be useful to have a bitfield in here, so that a person could update only a subset of the parameters for
// an effect. Bitfield would have effect-specific interpretations, to indicate which parameters have valid data.
struct PostFXEffectParameters
{
	enum class SandevistanMode : Uint32
	{
		// #NOTE: These must match the #define's in postfx_CPSandevistan.fx
		Default = 0,
		Kereznikov = 1
	};

	struct CPSandevistan
	{
		SandevistanMode mode;
	};

	struct CPVisionMode
	{
		Bool focusEnabled : 1;
		Bool braindanceEnabled : 1;

		Float resolveTime;			// Time in seconds to resolve a normal highlight

		Float scanProgress;
		rend::PostFx_ScanningState scanState;

		RED_INLINE Bool operator ==(const CPVisionMode& other) const
		{
			return (focusEnabled == other.focusEnabled)
				&& (braindanceEnabled == other.braindanceEnabled)
				&& (resolveTime == other.resolveTime)
				&& (scanState == other.scanState)
				&& (scanProgress == other.scanProgress);
		}
		RED_INLINE Bool operator !=(const CPVisionMode& other ) const { return !operator ==(other); }


		static Uint8 PackHighlightInfo( const Bool seeThroughWalls, const Bool isPrimaryPattern, const Uint8 fillIndex, const Uint8 outlineIndex );
	};

	enum class CyberspaceMode : Uint32
	{
		Fullscreen = 0,
		StencilCut = 1,
		Vfx = 2
	};

	struct CPCyberspace
	{
		CyberspaceMode m_mode = CyberspaceMode::Fullscreen;
		CyberspaceMode m_vfx = CyberspaceMode::StencilCut;
		Float m_initialDatamosh;
		Float m_targetDatamosh;
		Float m_targetTreshold;
		Float m_blendTime;
		Float m_currentTreshold;
	};

	EPostFXEffect effect;

	union
	{
		Uint8 raw[16];
		CPSandevistan cpSandevistan;
		CPVisionMode cpVisionMode;
		CPCyberspace cpCyberspace;
	};


	RED_INLINE PostFXEffectParameters()
		: effect(EPE_MAX)
	{}

	RED_INLINE PostFXEffectParameters( EPostFXEffect effect )
		: effect( effect )
	{
		red::Memzero(raw, sizeof(raw));

		// Set up additional defaults depending on effect type.
		switch ( effect )
		{
		case EPE_CPFocusMode:
			{
				cpVisionMode.focusEnabled = false;
				cpVisionMode.braindanceEnabled = false;
				cpVisionMode.resolveTime = 0.25f;
			} break;

		case EPE_CPCyberspacePixelsort:
		{
			cpCyberspace.m_mode = CyberspaceMode::Fullscreen;
			cpCyberspace.m_vfx = CyberspaceMode::StencilCut;
			cpCyberspace.m_initialDatamosh = 0.0f;
			cpCyberspace.m_targetDatamosh = 0.0f;
			cpCyberspace.m_blendTime = 5.0f;
			cpCyberspace.m_targetTreshold = 0.3f;
			cpCyberspace.m_currentTreshold = 0.0f;
		} break;
		}
	}
};
static_assert(sizeof(PostFXEffectParameters::CPSandevistan) <= sizeof(std::declval<PostFXEffectParameters>().raw), "PostFXEffectParameters::raw isn't big enough");
static_assert(sizeof(PostFXEffectParameters::CPVisionMode) <= sizeof(std::declval<PostFXEffectParameters>().raw), "PostFXEffectParameters::raw isn't big enough");
