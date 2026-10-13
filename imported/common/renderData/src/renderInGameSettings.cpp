/**
* Copyright (c) 2020 CD Projekt Red. All Rights Reserved.
*/
#include "build.h"

#include "renderInGameSettings.h"
#include "../../redCore/include/names.h"
#include "../../redConfig/include/inGameConfigSystem.h"
#include "renderer.h"
#include "renderSettingsVars.h"
#include "viewport.h"
#include "../../redConfig/include/inGameConfigUtils.h"

#if defined(RED_PLATFORM_DURANGO)
	#define CONSOLE_TYPE_XBOX_SERIES_S        ((CONSOLE_TYPE)5)
	#define CONSOLE_TYPE_XBOX_SERIES_X        ((CONSOLE_TYPE)6)
	#define CONSOLE_TYPE_XBOX_SERIES_X_DEVKIT ((CONSOLE_TYPE)7)
#endif

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Groups
static CName s_inGameConfigGroup( RED_NAME_CONSTEXPR( "/graphics" ) );
static CName s_inGameConfigGroup_Basic( RED_NAME_CONSTEXPR( "/graphics/basic" ) );
static CName s_inGameConfigGroup_Display( RED_NAME_CONSTEXPR( "/video/display" ) );

#ifdef RED_PLATFORM_WINPC
static CName s_inGameConfigGroup_Advanced( RED_NAME_CONSTEXPR( "/graphics/advanced" ) );
static CName s_inGameConfigGroup_RayTracing( RED_NAME_CONSTEXPR( "/graphics/raytracing" ) );
static CName s_inGameConfigGroup_DynamicResolution( RED_NAME_CONSTEXPR( "/graphics/dynamicresolution" ) );
#endif

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Video tab
#ifdef RED_PLATFORM_WINPC
static CName s_inGameConfigVar_OutputMonitor( RED_NAME_CONSTEXPR( "OutputMonitor" ) );
static CName s_inGameConfigVar_VSync( RED_NAME_CONSTEXPR( "VSync" ) );
static CName s_inGameConfigVar_WindowMode( RED_NAME_CONSTEXPR( "WindowMode" ) );
static CName s_inGameConfigVar_Resolution( RED_NAME_CONSTEXPR( "Resolution" ) );
#endif

static CName s_inGameConfigVar_TonemappingMidpoint( RED_NAME_CONSTEXPR( "TonemappingMidpoint" ) );
static CName s_inGameConfigVar_MaxMonitorBrightness( RED_NAME_CONSTEXPR( "MaxMonitorBrightness" ) );
static CName s_inGameConfigVar_PaperWhiteLevel( RED_NAME_CONSTEXPR( "PaperWhiteLevel" ) );
static CName s_inGameConfigVar_Gamma( RED_NAME_CONSTEXPR( "Gamma" ) );
static CName s_inGameConfigVar_HDRModes( RED_NAME_CONSTEXPR( "HDRModes" ) );
static CName s_inGameConfigVar_FrameRateModes( RED_NAME_CONSTEXPR( "FrameRateModes" ) );

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Graphics tab

static CName s_inGameConfigVar_FilmGrain( RED_NAME_CONSTEXPR( "FilmGrain" ) );
static CName s_inGameConfigVar_ChromaticAberation( RED_NAME_CONSTEXPR( "ChromaticAberration" ) );
static CName s_inGameConfigVar_DepthOfField( RED_NAME_CONSTEXPR( "DepthOfField" ) );
static CName s_inGameConfigVar_LensFlares( RED_NAME_CONSTEXPR( "LensFlares" ) );
static CName s_inGameConfigVar_MotionBlur( RED_NAME_CONSTEXPR( "MotionBlur" ) );

#ifdef RED_PLATFORM_WINPC
static CName s_inGameConfigVar_QuickPresets( RED_NAME_CONSTEXPR( "QuickPresets" ) );
static CName s_inGameConfigVar_RayTracing( RED_NAME_CONSTEXPR( "RayTracing" ) );
static CName s_inGameConfigVar_RayTracedReflections( RED_NAME_CONSTEXPR( "RayTracedReflections" ) );
static CName s_inGameConfigVar_RayTracedSunShadows( RED_NAME_CONSTEXPR( "RayTracedSunShadows" ) );
static CName s_inGameConfigVar_RayTracedLighting( RED_NAME_CONSTEXPR( "RayTracedLighting" ) );
static CName s_inGameConfigVar_DynamicResolutionScaling( RED_NAME_CONSTEXPR( "DynamicResolutionScaling" ) );
static CName s_inGameConfigVar_DLSS( RED_NAME_CONSTEXPR( "DLSS" ) );
static CName s_inGameConfigVar_StaticResolutionScaling( RED_NAME_CONSTEXPR( "StaticResolutionScaling" ) );
static CName s_inGameConfigVar_DRS_TargetFPS( RED_NAME_CONSTEXPR( "DRS_TargetFPS" ) );
static CName s_inGameConfigVar_DRS_MaximalResolution( RED_NAME_CONSTEXPR( "DRS_MaximalResolution" ) );
static CName s_inGameConfigVar_DRS_MinimalResolution( RED_NAME_CONSTEXPR( "DRS_MinimalResolution" ) );
static CName s_inGameConfigVar_SRS_Resolution( RED_NAME_CONSTEXPR( "SRS_Resolution" ) );
static CName s_inGameConfigVar_MaximumFPSOnOff( RED_NAME_CONSTEXPR( "MaximumFPS_OnOff" ) );
static CName s_inGameConfigVar_MaximumFPSValue( RED_NAME_CONSTEXPR( "MaximumFPS_Value" ) );
#endif

Bool IsWindows7();

namespace rend
{
	namespace settings
	{
		constexpr Uint32 c_LocalShadowsResolution_OFF = 1;
		constexpr Uint8  c_LocalShadowsProcessedPerFrame_OFF = 0;

		constexpr Uint32 c_LocalShadowsResolution_LOW = 512;
		constexpr Uint8  c_LocalShadowsProcessedPerFrame_LOW = 2;

		constexpr Uint32 c_LocalShadowsResolution_MEDIUM = 512;
		constexpr Uint8  c_LocalShadowsProcessedPerFrame_MEDIUM = 3;

		constexpr Uint32 c_LocalShadowsResolution_HIGH = 1024;
		constexpr Uint8  c_LocalShadowsProcessedPerFrame_HIGH = 5;

		template<>
		InGameOption Get< InGameEntry::LocalShadowsQuality >( const Config::GlobalRenderingSettings& qualityLevel )
		{
			if ( !qualityLevel.allowLocalShadows && qualityLevel.LocalShadowsResolution == c_LocalShadowsResolution_OFF && qualityLevel.LocalShadowsProcessedPerFrame == c_LocalShadowsProcessedPerFrame_OFF )
			{
				return InGameOption::Off;
			}

			if ( qualityLevel.allowLocalShadows && qualityLevel.LocalShadowsResolution == c_LocalShadowsResolution_LOW && qualityLevel.LocalShadowsProcessedPerFrame == c_LocalShadowsProcessedPerFrame_LOW )
			{
				return InGameOption::Low;
			}

			if ( qualityLevel.allowLocalShadows && qualityLevel.LocalShadowsResolution == c_LocalShadowsResolution_MEDIUM && qualityLevel.LocalShadowsProcessedPerFrame == c_LocalShadowsProcessedPerFrame_MEDIUM )
			{
				return InGameOption::Medium;
			}

			if ( qualityLevel.allowLocalShadows && qualityLevel.LocalShadowsResolution == c_LocalShadowsResolution_HIGH && qualityLevel.LocalShadowsProcessedPerFrame == c_LocalShadowsProcessedPerFrame_HIGH )
			{
				return InGameOption::High;
			}

			return InGameOption::Invalid;
		}

		template<> void Set< InGameEntry::LocalShadowsQuality, InGameOption::Off >( Config::GlobalRenderingSettings& out )
		{
			out.allowLocalShadows = false;
			out.LocalShadowsResolution = c_LocalShadowsResolution_OFF;
			out.LocalShadowsProcessedPerFrame = c_LocalShadowsProcessedPerFrame_OFF;
		}

		template<> void Set< InGameEntry::LocalShadowsQuality, InGameOption::Low >( Config::GlobalRenderingSettings& out )
		{
			out.allowLocalShadows = true;
			out.LocalShadowsResolution = c_LocalShadowsResolution_LOW;
			out.LocalShadowsProcessedPerFrame = c_LocalShadowsProcessedPerFrame_LOW;
		}

		template<> void Set< InGameEntry::LocalShadowsQuality, InGameOption::Medium >( Config::GlobalRenderingSettings& out )
		{
			out.allowLocalShadows = true;
			out.LocalShadowsResolution = c_LocalShadowsResolution_MEDIUM;
			out.LocalShadowsProcessedPerFrame = c_LocalShadowsProcessedPerFrame_MEDIUM;
		}

		template<> void Set< InGameEntry::LocalShadowsQuality, InGameOption::High >( Config::GlobalRenderingSettings& out )
		{
			out.allowLocalShadows = true;
			out.LocalShadowsResolution = c_LocalShadowsResolution_HIGH;
			out.LocalShadowsProcessedPerFrame = c_LocalShadowsProcessedPerFrame_HIGH;
		}

		constexpr Uint32	c_volumetricCloudsDownsample_MEDIUM = 1;
		constexpr Uint32	c_volumetricCloudsNumShadowSteps_MEDIUM = 4;
		constexpr Uint32	c_volumetricCloudsNumSteps_MEDIUM = 48;
		constexpr Float		c_volumetricCloudsStepScale_MEDIUM = 1.33;
		constexpr Float		c_volumetricCloudsShadowStepScale_MEDIUM = 2;
		constexpr Uint32	c_volumetricCloudsShadowResolution_MEDIUM = 512u;

		constexpr Uint32	c_volumetricCloudsDownsample_HIGH = 1;
		constexpr Uint32	c_volumetricCloudsNumShadowSteps_HIGH = 6;
		constexpr Uint32	c_volumetricCloudsNumSteps_HIGH = 64;
		constexpr Float		c_volumetricCloudsStepScale_HIGH = 1.0f;
		constexpr Float		c_volumetricCloudsShadowStepScale_HIGH = 2;
		constexpr Uint32	c_volumetricCloudsShadowResolution_HIGH = 512u;

		constexpr Uint32	c_volumetricCloudsDownsample_ULTRA = 1;
		constexpr Uint32	c_volumetricCloudsNumShadowSteps_ULTRA = 6;
		constexpr Uint32	c_volumetricCloudsNumSteps_ULTRA = 128;
		constexpr Float		c_volumetricCloudsStepScale_ULTRA = 0.5f;
		constexpr Float		c_volumetricCloudsShadowStepScale_ULTRA = 2;
		constexpr Uint32	c_volumetricCloudsShadowResolution_ULTRA = 512u;

		template<>
		InGameOption Get< InGameEntry::VolumetricCloudsQuality >( const Config::GlobalRenderingSettings& qualityLevel )
		{
			if ( !qualityLevel.allowVolumetricClouds )
			{
				return InGameOption::Off;
			}
			else
			{
				if ( qualityLevel.volumetricCloudsDownsample == c_volumetricCloudsDownsample_MEDIUM && qualityLevel.volumetricCloudsNumShadowSteps == c_volumetricCloudsNumShadowSteps_MEDIUM
					&& qualityLevel.volumetricCloudsNumSteps == c_volumetricCloudsNumSteps_MEDIUM && qualityLevel.volumetricCloudsStepScale == c_volumetricCloudsStepScale_MEDIUM
					&& qualityLevel.volumetricCloudsShadowStepScale == c_volumetricCloudsShadowStepScale_MEDIUM && qualityLevel.volumetricCloudsShadowResolution == c_volumetricCloudsShadowResolution_MEDIUM )
				{
					return InGameOption::Medium;
				}

				if ( qualityLevel.volumetricCloudsDownsample == c_volumetricCloudsDownsample_HIGH && qualityLevel.volumetricCloudsNumShadowSteps == c_volumetricCloudsNumShadowSteps_HIGH
					&& qualityLevel.volumetricCloudsNumSteps == c_volumetricCloudsNumSteps_HIGH && qualityLevel.volumetricCloudsStepScale == c_volumetricCloudsStepScale_HIGH
					&& qualityLevel.volumetricCloudsShadowStepScale == c_volumetricCloudsShadowStepScale_HIGH && qualityLevel.volumetricCloudsShadowResolution == c_volumetricCloudsShadowResolution_HIGH )
				{
					return InGameOption::High;
				}

				if ( qualityLevel.volumetricCloudsDownsample == c_volumetricCloudsDownsample_ULTRA && qualityLevel.volumetricCloudsNumShadowSteps == c_volumetricCloudsNumShadowSteps_ULTRA
					&& qualityLevel.volumetricCloudsNumSteps == c_volumetricCloudsNumSteps_ULTRA && qualityLevel.volumetricCloudsStepScale == c_volumetricCloudsStepScale_ULTRA
					&& qualityLevel.volumetricCloudsShadowStepScale == c_volumetricCloudsShadowStepScale_ULTRA && qualityLevel.volumetricCloudsShadowResolution == c_volumetricCloudsShadowResolution_ULTRA )
				{
					return InGameOption::Ultra;
				}
			}

			return InGameOption::Invalid;
		}

		template<> void Set< InGameEntry::VolumetricCloudsQuality, InGameOption::Off >( Config::GlobalRenderingSettings& out )
		{
			out.allowVolumetricClouds = false;
		}

		template<> void Set< InGameEntry::VolumetricCloudsQuality, InGameOption::Medium >( Config::GlobalRenderingSettings& out )
		{
			out.allowVolumetricClouds = true;
			out.volumetricCloudsDownsample = c_volumetricCloudsDownsample_MEDIUM;
			out.volumetricCloudsNumShadowSteps = c_volumetricCloudsNumShadowSteps_MEDIUM;
			out.volumetricCloudsNumSteps = c_volumetricCloudsNumSteps_MEDIUM;
			out.volumetricCloudsStepScale = c_volumetricCloudsStepScale_MEDIUM;
			out.volumetricCloudsShadowStepScale = c_volumetricCloudsShadowStepScale_MEDIUM;
			out.volumetricCloudsShadowResolution = c_volumetricCloudsShadowResolution_MEDIUM;
		}

		template<> void Set< InGameEntry::VolumetricCloudsQuality, InGameOption::High >( Config::GlobalRenderingSettings& out )
		{
			out.allowVolumetricClouds = true;
			out.volumetricCloudsDownsample = c_volumetricCloudsDownsample_HIGH;
			out.volumetricCloudsNumShadowSteps = c_volumetricCloudsNumShadowSteps_HIGH;
			out.volumetricCloudsNumSteps = c_volumetricCloudsNumSteps_HIGH;
			out.volumetricCloudsStepScale = c_volumetricCloudsStepScale_HIGH;
			out.volumetricCloudsShadowStepScale = c_volumetricCloudsShadowStepScale_HIGH;
			out.volumetricCloudsShadowResolution = c_volumetricCloudsShadowResolution_HIGH;
		}

		template<> void Set< InGameEntry::VolumetricCloudsQuality, InGameOption::Ultra >( Config::GlobalRenderingSettings& out )
		{
			out.allowVolumetricClouds = true;
			out.volumetricCloudsDownsample = c_volumetricCloudsDownsample_ULTRA;
			out.volumetricCloudsNumShadowSteps = c_volumetricCloudsNumShadowSteps_ULTRA;
			out.volumetricCloudsNumSteps = c_volumetricCloudsNumSteps_ULTRA;
			out.volumetricCloudsStepScale = c_volumetricCloudsStepScale_ULTRA;
			out.volumetricCloudsShadowStepScale = c_volumetricCloudsShadowStepScale_ULTRA;
			out.volumetricCloudsShadowResolution = c_volumetricCloudsShadowResolution_ULTRA;
		}

		constexpr Uint32 c_DynamicDecalsLimit_LOW = 60;
		constexpr Uint32 c_DynamicDecalsChunkLimit_LOW = 30;
		constexpr Uint32 c_DynamicDecalsSpawnerLimit_LOW = 60;
		constexpr Uint32 c_DynamicDecalsChunkPerTargetLimit_LOW = 5;

		constexpr Uint32 c_DynamicDecalsLimit_MEDIUM = 60;
		constexpr Uint32 c_DynamicDecalsChunkLimit_MEDIUM = 50;
		constexpr Uint32 c_DynamicDecalsSpawnerLimit_MEDIUM = 60;
		constexpr Uint32 c_DynamicDecalsChunkPerTargetLimit_MEDIUM = 15;

		constexpr Uint32 c_DynamicDecalsLimit_HIGH = 90;
		constexpr Uint32 c_DynamicDecalsChunkLimit_HIGH = 100;
		constexpr Uint32 c_DynamicDecalsSpawnerLimit_HIGH = 90;
		constexpr Uint32 c_DynamicDecalsChunkPerTargetLimit_HIGH = 20;

		constexpr Uint32 c_DynamicDecalsLimit_ULTRA = 120;
		constexpr Uint32 c_DynamicDecalsChunkLimit_ULTRA = 150;
		constexpr Uint32 c_DynamicDecalsSpawnerLimit_ULTRA = 120;
		constexpr Uint32 c_DynamicDecalsChunkPerTargetLimit_ULTRA = 25;


		template<>
		InGameOption Get< InGameEntry::MaxDynamicDecals >( const Config::GlobalRenderingSettings& qualityLevel )
		{
			if ( qualityLevel.DynamicDecalsLimit == c_DynamicDecalsLimit_LOW &&
				qualityLevel.DynamicDecalsChunkLimit == c_DynamicDecalsChunkLimit_LOW &&
				qualityLevel.DynamicDecalsSpawnerLimit == c_DynamicDecalsSpawnerLimit_LOW &&
				qualityLevel.DynamicDecalsChunkPerTargetLimit == c_DynamicDecalsChunkPerTargetLimit_LOW )
			{
				return InGameOption::Low;
			}

			if ( qualityLevel.DynamicDecalsLimit == c_DynamicDecalsLimit_MEDIUM &&
				qualityLevel.DynamicDecalsChunkLimit == c_DynamicDecalsChunkLimit_MEDIUM &&
				qualityLevel.DynamicDecalsSpawnerLimit == c_DynamicDecalsSpawnerLimit_MEDIUM &&
				qualityLevel.DynamicDecalsChunkPerTargetLimit == c_DynamicDecalsChunkPerTargetLimit_MEDIUM )
			{
				return InGameOption::Medium;
			}

			if ( qualityLevel.DynamicDecalsLimit == c_DynamicDecalsLimit_HIGH &&
				qualityLevel.DynamicDecalsChunkLimit == c_DynamicDecalsChunkLimit_HIGH &&
				qualityLevel.DynamicDecalsSpawnerLimit == c_DynamicDecalsSpawnerLimit_HIGH &&
				qualityLevel.DynamicDecalsChunkPerTargetLimit == c_DynamicDecalsChunkPerTargetLimit_HIGH )
			{
				return InGameOption::High;
			}

			if ( qualityLevel.DynamicDecalsLimit == c_DynamicDecalsLimit_ULTRA &&
				qualityLevel.DynamicDecalsChunkLimit == c_DynamicDecalsChunkLimit_ULTRA &&
				qualityLevel.DynamicDecalsSpawnerLimit == c_DynamicDecalsSpawnerLimit_ULTRA &&
				qualityLevel.DynamicDecalsChunkPerTargetLimit == c_DynamicDecalsChunkPerTargetLimit_ULTRA )
			{
				return InGameOption::Ultra;
			}

			return InGameOption::Invalid;
		}

		template<> void Set< InGameEntry::MaxDynamicDecals, InGameOption::Low >( Config::GlobalRenderingSettings& out )
		{
			out.DynamicDecalsLimit = c_DynamicDecalsLimit_LOW;
			out.DynamicDecalsChunkLimit = c_DynamicDecalsChunkLimit_LOW;
			out.DynamicDecalsSpawnerLimit = c_DynamicDecalsSpawnerLimit_LOW;
			out.DynamicDecalsChunkPerTargetLimit = c_DynamicDecalsChunkPerTargetLimit_LOW;
		}

		template<> void Set< InGameEntry::MaxDynamicDecals, InGameOption::Medium >( Config::GlobalRenderingSettings& out )
		{
			out.DynamicDecalsLimit = c_DynamicDecalsLimit_MEDIUM;
			out.DynamicDecalsChunkLimit = c_DynamicDecalsChunkLimit_MEDIUM;
			out.DynamicDecalsSpawnerLimit = c_DynamicDecalsSpawnerLimit_MEDIUM;
			out.DynamicDecalsChunkPerTargetLimit = c_DynamicDecalsChunkPerTargetLimit_MEDIUM;
		}

		template<> void Set< InGameEntry::MaxDynamicDecals, InGameOption::High >( Config::GlobalRenderingSettings& out )
		{
			out.DynamicDecalsLimit = c_DynamicDecalsLimit_HIGH;
			out.DynamicDecalsChunkLimit = c_DynamicDecalsChunkLimit_HIGH;
			out.DynamicDecalsSpawnerLimit = c_DynamicDecalsSpawnerLimit_HIGH;
			out.DynamicDecalsChunkPerTargetLimit = c_DynamicDecalsChunkPerTargetLimit_HIGH;
		}

		template<> void Set< InGameEntry::MaxDynamicDecals, InGameOption::Ultra >( Config::GlobalRenderingSettings& out )
		{
			out.DynamicDecalsLimit = c_DynamicDecalsLimit_ULTRA;
			out.DynamicDecalsChunkLimit = c_DynamicDecalsChunkLimit_ULTRA;
			out.DynamicDecalsSpawnerLimit = c_DynamicDecalsSpawnerLimit_ULTRA;
			out.DynamicDecalsChunkPerTargetLimit = c_DynamicDecalsChunkPerTargetLimit_ULTRA;
		}

	}
}

namespace utils
{
	//
	// Fill in available values and display lockeys for given monitor and window state.
	// Also matches requestedMode with the above and returns its index on outHdrModes/outDisplayedName lists.
	//
	Int32 GetHDRModesDynamicData( GpuWrapApi::eHDRMode requestedMode, Int32 monitorIdx, Bool fullscreen, red::DynArray< String >& outHdrModes, red::DynArray< CName > & outDisplayedName )
	{
		Int32 matchingHDRModeIndex = -1;
		const Uint32 availableHDRModes = GRender->GetSupportedHDRModesMask( monitorIdx, fullscreen );
		{
			if ( availableHDRModes & RED_FLAG( GpuWrapApi::eHDRMode::HDRMODE_None ) )
			{
				outHdrModes.PushBack( "None" );
				outDisplayedName.PushBack( RED_NAME_CONSTEXPR( "UI-Settings-HDRModes-None" ) );
				if ( GpuWrapApi::eHDRMode::HDRMODE_None == requestedMode )
					matchingHDRModeIndex = outHdrModes.Size() - 1;
			}

			if ( availableHDRModes & RED_FLAG( GpuWrapApi::eHDRMode::HDRMODE_HDR10_PQ ) )
			{
				outHdrModes.PushBack( "HDR10_PQ" );
				outDisplayedName.PushBack( RED_NAME_CONSTEXPR( "UI-Settings-HDRModes-HDR10_PQ" ) );
				if ( GpuWrapApi::eHDRMode::HDRMODE_HDR10_PQ == requestedMode )
					matchingHDRModeIndex = outHdrModes.Size() - 1;
			}

			if ( availableHDRModes & RED_FLAG( GpuWrapApi::eHDRMode::HDRMODE_HDR10_scRGB ) )
			{
				outHdrModes.PushBack( "HDR10_scRGB" );
				outDisplayedName.PushBack( RED_NAME_CONSTEXPR( "UI-Settings-HDRModes-HDR10_scRGB" ) );
				if ( GpuWrapApi::eHDRMode::HDRMODE_HDR10_scRGB == requestedMode )
					matchingHDRModeIndex = outHdrModes.Size() - 1;
			}

			if ( availableHDRModes & RED_FLAG( GpuWrapApi::eHDRMode::HDRMODE_FreeSync2_Gamma22 ) )
			{
				outHdrModes.PushBack( "FreeSync2_Gamma22" );
				outDisplayedName.PushBack( RED_NAME_CONSTEXPR( "UI-Settings-HDRModes-FreeSync2_Gamma22" ) );
				if ( GpuWrapApi::eHDRMode::HDRMODE_FreeSync2_Gamma22 == requestedMode )
					matchingHDRModeIndex = outHdrModes.Size() - 1;
			}

			if ( availableHDRModes & RED_FLAG( GpuWrapApi::eHDRMode::HDRMODE_FreeSync2_scRGB ) )
			{
				outHdrModes.PushBack( "FreeSync2_scRGB" );
				outDisplayedName.PushBack( RED_NAME_CONSTEXPR( "UI-Settings-HDRModes-FreeSync2_scRGB" ) );
				if ( GpuWrapApi::eHDRMode::HDRMODE_FreeSync2_scRGB == requestedMode )
					matchingHDRModeIndex = outHdrModes.Size() - 1;
			}

			RED_ASSERT( !outHdrModes.Empty(), "At least 'None' option should be available" );
			return matchingHDRModeIndex;
		}
	}

#ifdef RED_PLATFORM_WINPC
	void GetAvailableQuickPresets( red::DynArray< String >& outQuickPresets, red::DynArray< CName >& outQuickPresetsLocKeys )
	{
		//
		// These are always available
		//
		outQuickPresets.PushBack( "Custom" );
		outQuickPresets.PushBack( "Low" );
		outQuickPresets.PushBack( "Medium" );
		outQuickPresets.PushBack( "High" );
		outQuickPresets.PushBack( "Ultra" );

		outQuickPresetsLocKeys.PushBack( RED_NAME_CONSTEXPR( "UI-Settings-Video-QualitySetting-Custom" ) );
		outQuickPresetsLocKeys.PushBack( RED_NAME_CONSTEXPR( "UI-Settings-Video-QualitySetting-Low" ) );
		outQuickPresetsLocKeys.PushBack( RED_NAME_CONSTEXPR( "UI-Settings-Video-QualitySetting-Medium" ) );
		outQuickPresetsLocKeys.PushBack( RED_NAME_CONSTEXPR( "UI-Settings-Video-QualitySetting-High" ) );
		outQuickPresetsLocKeys.PushBack( RED_NAME_CONSTEXPR( "UI-Settings-Video-QualitySetting-Ultra" ) );

		//
		// These are only available on ray tracing supporting hardware.
		//
		const Bool isRayTracingSupported = GRender->GetHardwareRayTracingSupport();
		if ( isRayTracingSupported )
		{
			outQuickPresets.PushBack( "Ray Tracing Medium" );
			outQuickPresets.PushBack( "Ray Tracing Ultra" );

			outQuickPresetsLocKeys.PushBack( RED_NAME_CONSTEXPR( "UI-Settings-Video-QualitySetting-RayTracingMedium" ) );
			outQuickPresetsLocKeys.PushBack( RED_NAME_CONSTEXPR( "UI-Settings-Video-QualitySetting-RayTracingUltra" ) );
		}
	}

	void GetAvailableWindowMode( red::DynArray< String >& outQuickPresets, red::DynArray< CName >& outQuickPresetsLocKeys )
	{
		//
		// These are always available
		//
		outQuickPresets.PushBack( "Windowed" );
		outQuickPresets.PushBack( "BorderlessWindowed" );

		outQuickPresetsLocKeys.PushBack( RED_NAME_CONSTEXPR( "UI-Settings-Video-WindowModeSetting-Windowed" ) );
		outQuickPresetsLocKeys.PushBack( RED_NAME_CONSTEXPR( "UI-Settings-Video-WindowModeSetting-BorderlessWindowed" ) );

		//
		// Fullscreen is disabled only on Win7
		//
		const Bool isWin7 = IsWindows7();
		if( !isWin7 )
		{
			outQuickPresets.PushBack( "Fullscreen" );
			outQuickPresetsLocKeys.PushBack( RED_NAME_CONSTEXPR( "UI-Settings-Video-WindowModeSetting-Fullscreen" ) );
		}
	}

	Config::GraphicsQualityLevel MapIdxToGraphicsQualityLevel( Int32 idx )
	{
		RED_ASSERT( idx >= 0 && idx <= 6, "Invalid index tries to map to GraphicsQualityLevel." );

		static const Config::GraphicsQualityLevel mapping[ 7 ]
		{
			Config::GraphicsQualityLevel::Custom,
			Config::GraphicsQualityLevel::Low,
			Config::GraphicsQualityLevel::Medium,
			Config::GraphicsQualityLevel::High,
			Config::GraphicsQualityLevel::Ultra,
			Config::GraphicsQualityLevel::RTXMedium,
			Config::GraphicsQualityLevel::RTXUltra
		};

		return mapping[ idx ];
	}

	//
	// In order to omit tracing potential bugs cause by Config::GraphicsQualityLevel enum
	// changing internally (reordering etc.) I will do this in retarded way.
	//
	Int32 MapGraphicsQualityLevelToIdx( Config::GraphicsQualityLevel gql )
	{
		const Bool rtSupported = GRender->GetHardwareRayTracingSupport();
		if ( gql == Config::GraphicsQualityLevel::Custom )
			return 0;
		if ( gql == Config::GraphicsQualityLevel::Low )
			return 1;
		if ( gql == Config::GraphicsQualityLevel::Medium )
			return 2;
		if ( gql == Config::GraphicsQualityLevel::High )
			return 3;
		if ( gql == Config::GraphicsQualityLevel::Ultra )
			return 4;
		if ( gql == Config::GraphicsQualityLevel::RTXMedium )
			return rtSupported ? 5 : 0;	// If ray tracing is not supported default to Custom.
		if ( gql == Config::GraphicsQualityLevel::RTXUltra )
			return rtSupported ? 6 : 0;	// If ray tracing is not supported default to Custom.

		RED_FATAL_ASSERT( "GraphicsQualityLevel - wrong mapping to index." );
		return -1;
	}

	//
	// NOTE: This function is done in an explicit way in order to enable quicker debugging of the differences.
	//
	Bool MatchesPreset( const Config::GlobalRenderingSettings& base, const Config::GlobalRenderingSettings& preset )
	{
		Bool result = true;

		//
		// Basic tab.
		//
		const Bool filmGrainMatches = ( base.AllowFilmGrain == preset.AllowFilmGrain ); result &= filmGrainMatches;
		const Bool chromaticAbberationMatches = ( base.allowChromaticAberration == preset.allowChromaticAberration ); result &= chromaticAbberationMatches;
		const Bool depthOfFieldMatches = ( base.allowDOF == preset.allowDOF ); result &= depthOfFieldMatches;
		const Bool imageBasedFlaresMatches = ( base.allowImageBasedFlares == preset.allowImageBasedFlares ); result &= imageBasedFlaresMatches;
		const Bool motionBlurMatches = ( base.allowMotionBlur == preset.allowMotionBlur ) && ( base.MotionBlurQuality == preset.MotionBlurQuality ); result &= motionBlurMatches;

		//
		// Advanced tab.
		//
		const Bool contactShadowsMatches = ( base.allowContactShadows_Local == preset.allowContactShadows_Local ) && ( base.allowContactShadows_Global == preset.allowContactShadows_Global ); result &= contactShadowsMatches;
		const Bool facialTangentUpdatesMatches = ( base.allowRuntimeTangentUpdate == preset.allowRuntimeTangentUpdate ); result &= facialTangentUpdatesMatches;
		const Bool anisotropyLevelMatches = ( base.AnisotropyLevel == preset.AnisotropyLevel ); result &= anisotropyLevelMatches;
		const Bool shadowMeshQualityMatches = ( base.ShadowLODBias == preset.ShadowLODBias ); result &= shadowMeshQualityMatches;
		const Bool numShadowCascadesMatches = ( base.NumShadowCascades == preset.NumShadowCascades ); result &= numShadowCascadesMatches;
		const Bool shadowCascadeResolutionMatches = ( base.ShadowCascadeResolution == preset.ShadowCascadeResolution ); result &= shadowCascadeResolutionMatches;
		const Bool distantShadowmapResolutionMatches = ( base.DistantShadowmapResolution == preset.DistantShadowmapResolution ); result &= distantShadowmapResolutionMatches;
		const Bool volFogResolutionMatches = ( base.VolFogTileSize == preset.VolFogTileSize ) && ( base.VolFogNumSlices == preset.VolFogNumSlices ); result &= volFogResolutionMatches;
		const Bool ssrMatches = ( base.allowSSR == preset.allowSSR ) && ( base.ScreenSpaceReflectionLevel == preset.ScreenSpaceReflectionLevel ); result &= ssrMatches;
		const Bool subsurfaceQualityProfileMatches = ( base.SubsurfaceQualityProfileIndex == preset.SubsurfaceQualityProfileIndex ); result &= subsurfaceQualityProfileMatches;
		const Bool ssaoMatches = ( base.allowSSAO == preset.allowSSAO ) && ( base.ConeAOQuality == preset.ConeAOQuality ); result &= ssaoMatches;
		const Bool highPrecisionRenderTargetsMatches = ( base.UseHighPrecisionRenderTargets == preset.UseHighPrecisionRenderTargets ); result &= highPrecisionRenderTargetsMatches;
		const Bool mirrorMatches = ( base.MirrorFinalResolutionScale == preset.MirrorFinalResolutionScale ) && ( base.MirrorInternalResolutionScale == preset.MirrorInternalResolutionScale ); result &= mirrorMatches;
		
		using namespace rend::settings;
		const Bool localShadowsQualityMatches = Get< InGameEntry::LocalShadowsQuality >( base ) == Get< InGameEntry::LocalShadowsQuality >( preset ); result &= localShadowsQualityMatches;
		const Bool volumetricCloudsQualityMatches = Get< InGameEntry::VolumetricCloudsQuality >( base ) == Get< InGameEntry::VolumetricCloudsQuality >( preset ); result &= volumetricCloudsQualityMatches;
		const Bool maxDynamicDecalsMatches = Get< InGameEntry::MaxDynamicDecals >( base ) == Get< InGameEntry::MaxDynamicDecals >( preset ); result &= maxDynamicDecalsMatches;

		//
		// RTX stuff.
		//
		const Bool rtxEnabledMatches = ( base.allowRaytracing == preset.allowRaytracing ); result &= rtxEnabledMatches;
		const Bool rtxLightingMatches = ( base.RTX_EnableDiffuseIllumination == preset.RTX_EnableDiffuseIllumination ) && ( base.RTX_EnableAmbientOcclusion == preset.RTX_EnableAmbientOcclusion ) && ( base.RTX_EnableGlobalIllumination == preset.RTX_EnableGlobalIllumination ) &&
			( base.RTX_AmbientOcclusionRayNumber == preset.RTX_AmbientOcclusionRayNumber ) && ( base.RTX_ImportanceSamplingTransitionMin == preset.RTX_ImportanceSamplingTransitionMin ) &&
			( base.RTX_ImportanceSamplingTransitionMax == preset.RTX_ImportanceSamplingTransitionMax ); result &= rtxLightingMatches;
		const Bool rtxReflectionsMatches = ( base.RTX_EnableReflections == preset.RTX_EnableReflections ) && ( base.RTX_EnableTransparentReflections == preset.RTX_EnableTransparentReflections ); result &= rtxReflectionsMatches;
		const Bool rtxSunShadows = ( base.RTX_EnableDirectionalShadow == preset.RTX_EnableDirectionalShadow ); result &= rtxSunShadows;

		const Bool txaaMatches = ( base.allowTXAA == preset.allowTXAA ); result &= txaaMatches;

		return result;
	}

	//
	// Use MatchForPreset method to check whether supplied GlobalRenderingSettings is 1:1 with 
	// one of the in-game QuickPresets (Low, Medium, High, Ultra and RTXMedium/Ultra if available).
	//
	Config::GraphicsQualityLevel FindMatchingQuickPreset( const Config::GlobalRenderingSettings& renderingSettings )
	{
		// Should be probably ifdefed or something. 
		// DLSS variable is not reflected in GlobalRenderingSettings so it needs to be queried and checked separately.
		const Int32 dlssQualityIdx = rend::settings::graphics_tab::GetDLSSQuality();

 		if ( MatchesPreset( renderingSettings, Config::GetRenderSettings_LOW() ) && dlssQualityIdx == 0 )
		{
			return Config::GraphicsQualityLevel::Low;
		}
		else if ( MatchesPreset( renderingSettings, Config::GetRenderSettings_MEDIUM() ) && dlssQualityIdx == 0 )
		{
			return Config::GraphicsQualityLevel::Medium;
		}
		else if ( MatchesPreset( renderingSettings, Config::GetRenderSettings_HIGH() ) && dlssQualityIdx == 0 )
		{
			return Config::GraphicsQualityLevel::High;
		}
		else if ( MatchesPreset( renderingSettings, Config::GetRenderSettings_ULTRA() ) && dlssQualityIdx == 0 )
		{
			return Config::GraphicsQualityLevel::Ultra;
		}

		//
		// In case RTX is not supported - default to Custom.
		//
		const Bool isRayTracingSupported = GRender->GetHardwareRayTracingSupport();
		if ( isRayTracingSupported && MatchesPreset( renderingSettings, Config::GetRenderSettings_RTXMEDIUM() ) && dlssQualityIdx == 1 )
		{
			return Config::GraphicsQualityLevel::RTXMedium;
		}
		else if ( isRayTracingSupported && MatchesPreset( renderingSettings, Config::GetRenderSettings_RTXULTRA() ) && dlssQualityIdx == 1 )
		{
			return Config::GraphicsQualityLevel::RTXUltra;
		}

		return Config::GraphicsQualityLevel::Custom;
	}

	Config::GraphicsQualityLevel MapUnsupportedQualityLevelsToCustom( Config::GraphicsQualityLevel qualityLevel )
	{
		switch ( qualityLevel )
		{
		case Config::GraphicsQualityLevel::Cinematic:
		case Config::GraphicsQualityLevel::Cinematic_RTX:
		case Config::GraphicsQualityLevel::CinematicEXR:
		case Config::GraphicsQualityLevel::CinematicEXR_RTX:
		case Config::GraphicsQualityLevel::Console:
		case Config::GraphicsQualityLevel::ConsolePro:
		case Config::GraphicsQualityLevel::ConsoleEarlyNextGen:
		case Config::GraphicsQualityLevel::ConsoleEarlyNextGenQuality:
		case Config::GraphicsQualityLevel::GeForceNow:
		case Config::GraphicsQualityLevel::IconsGeneration:
		case Config::GraphicsQualityLevel::SafeMode:
		case Config::GraphicsQualityLevel::Auto:
		case Config::GraphicsQualityLevel::FromUserSettings:
		case Config::GraphicsQualityLevel::Count:
			return Config::GraphicsQualityLevel::Custom;
		}

		return qualityLevel;
	}

	void SetDLSSBasedOnPreset( Config::GraphicsQualityLevel qualityLevel )
	{
		switch ( qualityLevel )
		{
		case Config::GraphicsQualityLevel::RTXMedium:
		case Config::GraphicsQualityLevel::RTXUltra:
			rend::settings::graphics_tab::SetDLSS( true, 1, InGameConfig::Source::PresetCode );
			break;
		default:
			rend::settings::graphics_tab::SetDLSS( false, 0, InGameConfig::Source::PresetCode );
			break;
		}
	}

#endif
}


namespace rend
{
	namespace settings
	{
		Bool Valid()
		{
			auto& sys = InGameConfig::System::GetInstance();
			const auto mode = sys.GetMode();
			return mode != InGameConfig::Mode::Headless && IsGameMode();
		}

		namespace callbacks
		{
#ifdef RED_PLATFORM_WINPC
			void OnSave( IViewport* viewport )
			{
				//
				// Flush renderer so it does not read from GGlobalRederingSettings/GGlobalTextureSettings
				//
				RED_ASSERT( GRender, "Renderer not initialized" );
				GRender->Flush();

				GRender->RequestEvictOnNextSceneRender();

				//
				// Pull in game settings and push to GGlobalRederingSettings
				//
				Config::GGlobalRenderingSettings.UpdateWithInGameVideoSettings();


				//
				// Pull in game settings and push to GGlobalTextureSettings
				//
				Config::GGlobalTextureSettings.UpdateWithInGameVideoSettings();

				//
				// 
				//
				if ( viewport )
				{
					const Bool dlssEnabled = graphics_tab::GetDLSSEnabled();
					const Int32 dlssQuality = graphics_tab::GetDLSSQuality();
					const Bool drsEnabled = graphics_tab::GetDRSEnabled();
					const Uint32 drsMaximalResolutionPercentage = graphics_tab::GetDRSMaximalResolutionPercentage();
					const Uint32 drsMinimalResolutionPercentage = graphics_tab::GetDRSMinimalResolutionPercentage();
					const Uint32 drsTargetFPS = graphics_tab::GetDRSTargetFPS();
					const Bool srsEnabled = graphics_tab::GetSRSEnabled();
					const Uint32 srsResolutionPercentage = graphics_tab::GetSRSResolutionPercentage();
					const Int32 fpsClamp = video_tab::GetFPSClamp();

					RED_ASSERT( ( dlssEnabled ? 1 : 0 ) + ( drsEnabled ? 1 : 0 ) + ( srsEnabled ? 1 : 0 ) <= 1,
						"DRS, SRS, and DLSS cannot be active at the same time." );

					viewport->RequestDLSS( dlssEnabled );
					viewport->RequestDLSSQualityPreset( dlssQuality );
					viewport->RequestDRS( drsEnabled );
					viewport->RequestDRSMaximalResolution( drsMaximalResolutionPercentage );
					viewport->RequestDRSMinimalResolution( drsMinimalResolutionPercentage );
					viewport->RequestDRSTargetFPS( drsTargetFPS );
					viewport->RequestSRS( srsEnabled );
					viewport->RequestSRSResolution( srsResolutionPercentage );
					viewport->RequestFPSClamp( fpsClamp );
				}
			}

			void OnApply( IViewport* viewport )
			{
				if ( viewport && IsGameMode() )
				{
					//
					// Pull relevant variables from InGameConfig system.
					//
					const Int32 monitorIdx = rend::settings::video_tab::GetMonitorIdx();
					Uint32 width, height;
					rend::settings::video_tab::GetResolution( width, height, monitorIdx );
					const auto windowMode = rend::settings::video_tab::GetWindowMode();
					const auto hdrMode = rend::settings::video_tab::GetHDRMode( monitorIdx, windowMode == VWM_Fullscreen );
					const Uint32 vsync = rend::settings::video_tab::GetVSyncInterval( monitorIdx );

					//
					// Request changes from the viewport.
					//
					viewport->RequestOutputMonitor( ( Uint32 )monitorIdx );
					viewport->RequestWindowSize( ( Uint32 )width, ( Uint32 )height );
					viewport->RequestWindowMode( windowMode );
					viewport->RequestHDRMode( hdrMode );
					viewport->RequestVSyncInterval( vsync );
				}
			}

			void OnDisplayCalibrationValuesChanged( IViewport* viewport )
			{
				if ( viewport && IsGameMode() )
				{
					const Float tonemappingMidpoint = video_tab::GetTonemappingMidpoint();
					const Float maxMonitorBrightness = video_tab::GetMaxMonitorBrightness();
					const Float paperWhiteLevel = video_tab::GetPaperWhiteLevel();
					const Float gamma = video_tab::GetGamma();

					viewport->RequestHDRTonemappingMidpoint( tonemappingMidpoint );
					viewport->RequestHDRMaxBrightness( maxMonitorBrightness );
					viewport->RequestHDRPaperWhite( paperWhiteLevel );
					viewport->RequestGamma( gamma );
				}
			}

			//
			// [PC]
			//
			void OnPresetChanged()
			{
				//
				// Either exit on 'Custom' preset or map to Config::GlobalRenderingSettings.
				//
				const Config::GraphicsQualityLevel selectedPreset = graphics_tab::GetQuickPreset();
				Config::GlobalRenderingSettings renderingSettingsToMap = {};
				switch ( selectedPreset )
				{
				case Config::GraphicsQualityLevel::Custom:
				{
					//
					// Custom means that someone clicked something that did not match any of presets or
					// manually selected custom. Both options do not require visual update of variables.
					//
					return;
				}
				case Config::GraphicsQualityLevel::Low:
				{					
					renderingSettingsToMap = Config::GetRenderSettings_LOW();
					break;
				}
				case Config::GraphicsQualityLevel::Medium:
				{
					renderingSettingsToMap = Config::GetRenderSettings_MEDIUM();
					break;
				}
				case Config::GraphicsQualityLevel::High:
				{
					renderingSettingsToMap = Config::GetRenderSettings_HIGH();
					break;
				}
				case Config::GraphicsQualityLevel::Ultra:
				{
					renderingSettingsToMap = Config::GetRenderSettings_ULTRA();
					break;
				}
				case Config::GraphicsQualityLevel::RTXMedium:
				{
					renderingSettingsToMap = Config::GetRenderSettings_RTXMEDIUM();
					break;
				}
				case Config::GraphicsQualityLevel::RTXUltra:
				{
					renderingSettingsToMap = Config::GetRenderSettings_RTXULTRA();
					break;
				}

				default:
					RED_HALT("Should not come here.");
				}

				//
				// Update InGameConfig variables with provided rendering settings. No per-setting callback will be triggered.
				//
				Config::MapRenderingSettingsToInGameSettings( renderingSettingsToMap, InGameConfig::Source::Code );

				//
				// DLSS is black sheep of Graphics Tab to be honest. Kept in viewport, shown in graphics tab and part of presets...
				//
				utils::SetDLSSBasedOnPreset( selectedPreset );

				//
				// Sanity check for GLobalRenderingSettings. No call to check DLSS.
				//
				{
					Config::GlobalRenderingSettings sanityCheck = {};
					sanityCheck.UpdateWithInGameVideoSettings();
					RED_ASSERT( utils::MatchesPreset( renderingSettingsToMap, renderingSettingsToMap ), "Mismatch between what is in InGameConfig and GGlobalRenderingSettings." );
				}
			}

			//
			// [PC]
			//
			void OnLoadQuickPresetFromUserSettings( const Config::MemoryInfo& mem )
			{
				//
				// Pull var from InGameConfigSystem
				//
				auto& sys = InGameConfig::System::GetInstance();
				auto& group = sys.GetGroup( s_inGameConfigGroup );
				auto& var = group.GetVarListString( s_inGameConfigVar_QuickPresets );

				//
				// Var must be uninitialized. This function is meant to be run single time during the app's lifetime.
				//
				RED_FATAL_ASSERT( var.GetValues().Empty(), "Called LoadQuickPresetFromUserSettings more than once in app's lifetime." );

				//
				// Detect preset based on RAM/VRAM. We do that on the first run in order to set QuickPresets default.
				// This enables in-game options to simply bring back QuickPresets to default when requested and all
				// other settings will follow.
				//
				const auto detectedQuickPreset = ( Config::GraphicsQualityLevel ) Config::DetectQualityLevel( mem );

				//
				// Query available presets based on hardware capability. Always available are: Low, Medium, High, Ultra.
				// Conditionally available: Ray Tracing Medium, Ray Tracing Ultra. Then update InGameConfig variable with
				// the options and pick index from UserSettings.json. 
				//
				{
					red::DynArray< String > quickPresets { job::PoolJobScope() };
					red::DynArray< CName > quickPresetsLocKeys { job::PoolJobScope() };
					utils::GetAvailableQuickPresets( quickPresets, quickPresetsLocKeys );

					const Int32 presetIdx = utils::MapGraphicsQualityLevelToIdx( detectedQuickPreset );
					var.InitializeDynamic( quickPresets, presetIdx );
					var.SetDisplayValues( quickPresetsLocKeys );
				}

				//
				// Additional care must be taken for RTX as someone may run its up first with hardware supporting
				// it and then run on something older.
				//
				{
					auto& sys = InGameConfig::System::GetInstance();
					auto& group = sys.GetGroup( s_inGameConfigGroup_RayTracing );
					auto& rtVar = group.GetVarBool( s_inGameConfigVar_RayTracing );

					const Bool rtxSupported = GRender->GetHardwareRayTracingSupport();
					const Bool rtxSetupLoaded = rtVar.GetValue();

					rtVar.SetValue( rtxSupported && rtxSetupLoaded, InGameConfig::Source::Code );
					rtVar.SetEnabled( rtxSupported );
				}

				//
				// Run it to trigger visibility of dynamic resolution widgets and maximum fps slider.
				//
				OnDRSChanged();
				OnDLSSChanged();
				OnSRSChanged();
				OnMaximumFPSOnOffChanged();

				//
				// This pass/check is needed due UserSettings.json being editable by user. It might happen that
				// QuickPresets variable was saved to let's say Ultra while contact shadows were manually disabled. 
				// First, update GGlobalRenderingSettings with all relevant variables from InGameConfigSystem.
				// Second, analyze new content of GGlobalRenderingSettings in terms of predefined presets.
				// Then, in case it differers from currently set in QuickPreset variable update it. 
				//
				{
					Config::GGlobalRenderingSettings.UpdateWithInGameVideoSettings();
					const Config::GraphicsQualityLevel mappedQuickPreset = utils::FindMatchingQuickPreset( Config::GGlobalRenderingSettings );
					const Config::GraphicsQualityLevel igcQuickPreset = utils::MapIdxToGraphicsQualityLevel( var.GetIndex() );

					if ( mappedQuickPreset != igcQuickPreset )
						graphics_tab::SetQuickPreset( mappedQuickPreset );
				}
			}

			//
			// [PC]
			//
			void OnLoadWindowModesFromUserSettings()
			{
				//
				// Pull var from InGameConfigSystem
				//
				auto& sys = InGameConfig::System::GetInstance();
				auto& group = sys.GetGroup( s_inGameConfigGroup_Display );
				auto& var = group.GetVarListString( s_inGameConfigVar_WindowMode );

				//
				// Var must be uninitialized. This function is meant to be run single time during the app's lifetime.
				//
				RED_FATAL_ASSERT( var.GetValues().Empty(), "Called LoadQuickPresetFromUserSettings more than once in app's lifetime." );

				//
				// Query available window modes based on operating system. Always available are: Windowed, BorderlessWindowed.
				// Conditionally available: Fullscreen (disabled on Win7). Then update InGameConfig variaable with the options
				// and pick index from UserSettings.json.
				{
					red::DynArray< String > windowModes{ job::PoolJobScope() };
					red::DynArray< CName > windowModesLocKeys{ job::PoolJobScope() };
					utils::GetAvailableWindowMode( windowModes, windowModesLocKeys );

					var.InitializeDynamic( windowModes, 1 /*Not sure about it but seems safe to always pick borderless as a default one. */ );
					var.SetDisplayValues( windowModesLocKeys );
				}
			}

			//
			// [PC]
			//
			void OnProcessRayTracingTabVisibility()
			{
				//
				// Pull vars from InGameConfigSystem
				//
				auto& sys = InGameConfig::System::GetInstance();
				auto& group = sys.GetGroup( s_inGameConfigGroup_RayTracing );
				auto& rtVar = group.GetVarBool( s_inGameConfigVar_RayTracing );
				auto& rtReflectionsVar = group.GetVarBool( s_inGameConfigVar_RayTracedReflections );
				auto& rtSunShadowsVar = group.GetVarBool( s_inGameConfigVar_RayTracedSunShadows );
				auto& rtLightingVar = group.GetVarListString( s_inGameConfigVar_RayTracedLighting );

				//
				// Hide/Show corresponding variables depending on whether RTX was enabled.
				//
				const Bool rtxSupported = GRender->GetHardwareRayTracingSupport();
				const Bool rtEnabled = rtVar.GetValue() && rtxSupported;
				rtReflectionsVar.SetVisible( rtEnabled );
				rtSunShadowsVar.SetVisible( rtEnabled );
				rtLightingVar.SetVisible( rtEnabled );

				//
				// Ask InGameConfig to refresh itself.
				//
				sys.QueueNotifyStatus( InGameConfig::NotificationType::Refresh );
			}

			Bool OnRayTracingToggleValidation()
			{

				auto& sys = InGameConfig::System::GetInstance();
				auto& group = sys.GetGroup( s_inGameConfigGroup_RayTracing );
				auto& rtVar = group.GetVarBool( s_inGameConfigVar_RayTracing );

				//
				// In case we want to trigger RTX we must be sure that it is supported by hardware.
				//
				const Bool rtSupported = GRender->GetHardwareRayTracingSupport();
				const Bool rtRequestedValue = rtVar.GetRequestedValue();
				if ( rtRequestedValue )
					return rtSupported;

				//
				// In case someone wants to disable RTX there is nothing to check.
				// This split (instead of just returning rtSupported) comes from a situation
				// where you are loading UserSettings.json which has RTX=true saved but
				// the hardware does not support it so we need to allow trigger switch to 'off'.
				//
				return true;
			}

			Bool OnDLSSToggleValidation()
			{
				auto& sys = InGameConfig::System::GetInstance();
				auto& group = sys.GetGroup( s_inGameConfigGroup_DynamicResolution );
				auto& dlssVar = group.GetVarListString( s_inGameConfigVar_DLSS );

				//
				// In case it is not supported - no change can be applied.
				//
				const Bool dlssHardwareSupport = GRender->GetHardwareDLSSSupport();
				const Int32 dlssRequestedValue = dlssVar.GetRequestedIndex();
				if ( dlssRequestedValue > 0 )
					return dlssHardwareSupport;

				return true;
			}

			//
			// [PC]
			//
			Bool OnDRSMinimalResolutionValidation()
			{
				auto& sys = InGameConfig::System::GetInstance();
				auto& group = sys.GetGroup( s_inGameConfigGroup_DynamicResolution );
				auto& drsMinResVar = group.GetVarInt( s_inGameConfigVar_DRS_MinimalResolution );
				auto& drsMaxResVar = group.GetVarInt( s_inGameConfigVar_DRS_MaximalResolution );

				const Int32 requestedDrsMinVal = drsMinResVar.GetRequestedValue();
				const Int32 drsMaxVal = drsMaxResVar.GetValue();
				return requestedDrsMinVal <= drsMaxVal;
			}

			//
			// [PC]
			//
			Bool OnDRSMaximalResolutionValidation()
			{
				auto& sys = InGameConfig::System::GetInstance();
				auto& group = sys.GetGroup( s_inGameConfigGroup_DynamicResolution );
				auto& drsMinResVar = group.GetVarInt( s_inGameConfigVar_DRS_MinimalResolution );
				auto& drsMaxResVar = group.GetVarInt( s_inGameConfigVar_DRS_MaximalResolution );

				const Int32 requestedDrsMaxVal = drsMaxResVar.GetRequestedValue();
				const Int32 drsMinVal = drsMinResVar.GetValue();
				return requestedDrsMaxVal >= drsMinVal;
			}

			//
			// [PC]
			//
			void OnDRSChanged()
			{
				auto& sys = InGameConfig::System::GetInstance();
				auto& group = sys.GetGroup( s_inGameConfigGroup_DynamicResolution );
				auto& drsVar = group.GetVarBool( s_inGameConfigVar_DynamicResolutionScaling );

				//
				// If DRS was enabled, disable DLSS and show DRS variables.
				//
				if ( drsVar.GetValue() )
				{
					auto& dlssVar = group.GetVarListString( s_inGameConfigVar_DLSS );
					dlssVar.SetIndex( 0, InGameConfig::Source::Code );

					auto& srsVar = group.GetVarBool( s_inGameConfigVar_StaticResolutionScaling );
					auto& srsResolution = group.GetVarInt( s_inGameConfigVar_SRS_Resolution );

					srsVar.SetValue( false, InGameConfig::Source::Code );
					srsResolution.SetVisible( false );
				}

				auto& drsTargetFPS = group.GetVarInt( s_inGameConfigVar_DRS_TargetFPS );
				auto& drsMaximalResolution = group.GetVarInt( s_inGameConfigVar_DRS_MaximalResolution );
				auto& drsMinimalResolution = group.GetVarInt( s_inGameConfigVar_DRS_MinimalResolution );


				drsMaximalResolution.SetVisible( drsVar.GetValue() );
				drsMinimalResolution.SetVisible( drsVar.GetValue() );
				drsTargetFPS.SetVisible( drsVar.GetValue() );

				//
				// Ask InGameConfig to refresh itself.
				//
				sys.QueueNotifyStatus( InGameConfig::NotificationType::Refresh );
			}

			//
			// [PC]
			//
			void OnMaximumFPSOnOffChanged()
			{
				auto& sys = InGameConfig::System::GetInstance();
				auto& group = sys.GetGroup( s_inGameConfigGroup_Display );
				auto& maxFPSOnOffVar = group.GetVarBool( s_inGameConfigVar_MaximumFPSOnOff );
				auto& maxFPSValueVar = group.GetVarInt( s_inGameConfigVar_MaximumFPSValue );

				const Bool maxFPSEnabled = maxFPSOnOffVar.GetValue();
				maxFPSValueVar.SetVisible( maxFPSEnabled );

				//
				// Ask InGameConfig to refresh itself.
				//
				sys.QueueNotifyStatus( InGameConfig::NotificationType::Refresh );
			}

			//
			// [PC]
			//
			void OnDLSSChanged()
			{
				auto& sys = InGameConfig::System::GetInstance();
				auto& group = sys.GetGroup( s_inGameConfigGroup_DynamicResolution );
				auto& dlssVar = group.GetVarListString( s_inGameConfigVar_DLSS );

				//
				// If DLSS was enabled, disable DRS and hide DRS variables.
				//
				if ( dlssVar.GetIndex() > 0 )
				{
					auto& drsVar = group.GetVarBool( s_inGameConfigVar_DynamicResolutionScaling );
					auto& drsTargetFPS = group.GetVarInt( s_inGameConfigVar_DRS_TargetFPS );
					auto& drsMaximalResolution = group.GetVarInt( s_inGameConfigVar_DRS_MaximalResolution );
					auto& drsMinimalResolution = group.GetVarInt( s_inGameConfigVar_DRS_MinimalResolution );

					drsVar.SetValue( false, InGameConfig::Source::Code );
					drsMaximalResolution.SetVisible( false );
					drsMinimalResolution.SetVisible( false );
					drsTargetFPS.SetVisible( false );

					auto& srsVar = group.GetVarBool( s_inGameConfigVar_StaticResolutionScaling );
					auto& srsResolution = group.GetVarInt( s_inGameConfigVar_SRS_Resolution );

					srsVar.SetValue( false, InGameConfig::Source::Code );
					srsResolution.SetVisible( false );

					//
					// Ask InGameConfig to refresh itself.
					//
					sys.QueueNotifyStatus( InGameConfig::NotificationType::Refresh );
				}
			}

			//
			// [PC]
			//
			void OnSRSChanged()
			{
				auto& sys = InGameConfig::System::GetInstance();
				auto& group = sys.GetGroup( s_inGameConfigGroup_DynamicResolution );
				auto& srsVar = group.GetVarBool( s_inGameConfigVar_StaticResolutionScaling );

				//
				// If SRS was enabled, disable DLSS, DRS, and show SRS variables.
				//
				if( srsVar.GetValue() )
				{
					auto& dlssVar = group.GetVarListString( s_inGameConfigVar_DLSS );
					dlssVar.SetIndex( 0, InGameConfig::Source::Code );
					auto& drsVar = group.GetVarBool( s_inGameConfigVar_DynamicResolutionScaling );
					drsVar.SetValue( false, InGameConfig::Source::Code );

					auto& drsMaximalResolution = group.GetVarInt( s_inGameConfigVar_DRS_MaximalResolution );
					auto& drsMinimalResolution = group.GetVarInt( s_inGameConfigVar_DRS_MinimalResolution );
					auto& drsTargetFPS = group.GetVarInt( s_inGameConfigVar_DRS_TargetFPS );

					drsMaximalResolution.SetVisible( false );
					drsMinimalResolution.SetVisible( false );
					drsTargetFPS.SetVisible( false );
				}

				auto& srsResolution = group.GetVarInt( s_inGameConfigVar_SRS_Resolution );
				srsResolution.SetVisible( srsVar.GetValue() );

				//
				// Ask InGameConfig to refresh itself.
				//
				sys.QueueNotifyStatus( InGameConfig::NotificationType::Refresh );
			}

			//
			// [PC]
			//
			void OnDetectCapabilitiesOnFreshStart_GraphicsTab( const Config::MemoryInfo& mem )
			{
				//
				// Detect preset based on RAM/VRAM
				//
				const auto detectedQuickPreset = ( Config::GraphicsQualityLevel ) Config::DetectQualityLevel( mem );

				//
				// Load available presets and update InGameConfig "QuickPReset" variable.
				// This will triggeer OnPresetChanged() callback and will map GGloblaRenderingSettings 
				// to rest of InGameConfig entries.
				//
				{
					auto& sys = InGameConfig::System::GetInstance();
					auto& group = sys.GetGroup( s_inGameConfigGroup );
					auto& var = group.GetVarListString( s_inGameConfigVar_QuickPresets );

					red::DynArray< String > quickPresets { job::PoolJobScope() };
					red::DynArray< CName > quickPresetsLocKeys { job::PoolJobScope() };
					utils::GetAvailableQuickPresets( quickPresets, quickPresetsLocKeys );

					const Int32 presetIdx = utils::MapGraphicsQualityLevelToIdx( detectedQuickPreset );
					var.SetValues( quickPresets /*, false? */ );
					var.SetDisplayValues( quickPresetsLocKeys );
					var.SetDefaultIndex( presetIdx );
					var.SetIndex( presetIdx, InGameConfig::Source::Code );
				}
			}

			//
			// [PC]
			//
			void OnDetectCapabilitiesOnFreshStart_VideoTab()
			{
				//
				// Sanity check so we never run it when InGameConfig has correct data.
				//
				auto& sys = InGameConfig::System::GetInstance();
				RED_ASSERT( sys.IsFirstRun(), "OnDetectCapabilitiesOnFreshStart called not on first run or config variable missing." );

				//
				// We always start on default monitor so I will assume that upper left
				// point is (0,0). We query WinAPI for current resolution.
				//
				Uint32 width = 0;
				Uint32 height = 0;
				{
					POINT point;
					point.x = 0;
					point.y = 0;

					MONITORINFO targetMonitor;
					targetMonitor.cbSize = sizeof( MONITORINFO );

					HMONITOR hMon = MonitorFromPoint( point, MONITOR_DEFAULTTONEAREST );
					if ( GetMonitorInfo( hMon, &targetMonitor ) != 0 )
					{
						width = targetMonitor.rcMonitor.right - targetMonitor.rcMonitor.left;
						height = targetMonitor.rcMonitor.bottom - targetMonitor.rcMonitor.top;
					}
					else
					{
						RED_LOG_WARNING( "OnDetectCapabilitiesOnFreshStart: Failed loading monitor info" );
					}
				}

				//
				// If we extracted resolution from desktop apply it to InGameConfig variable.
				// We also assume that in this case the application starts in borderless mode.
				//
				if ( width != 0 && height != 0 )
				{
					video_tab::SetMonitorIdx( 0 );
					video_tab::SetResolution( width, height, 0 );
					video_tab::SetWindowMode( VWM_Borderless );
				}
			}

			void OnOutputMonitorChanged()
			{
				//
				// Pull requested value from InGameConfigSystem
				//
				auto& sys = InGameConfig::System::GetInstance();
				auto& group = sys.GetGroup( s_inGameConfigGroup_Display );
				auto& var = group.GetVarListInt( s_inGameConfigVar_OutputMonitor );
				const Int32 monitorIdx = var.GetRequestedValue();

				//
				// Trigger resolution option visual update. Not the cleanest way but there is
				// no reason really to repeat 80% of code inside GetResolution or introduce 
				// some artificial intermediate function.
				//
				UINT w, h;
				video_tab::GetResolution( w, h, monitorIdx );

				//
				// Trigger HDRModes option visual update. Same reason as for the Resolution.
				//
				auto windowMode = video_tab::GetWindowMode();
				video_tab::GetHDRMode( monitorIdx, windowMode == VWM_Fullscreen );

				//
				// Trigger VSync option visual update. Same reason as for the Resolution.
				//
				video_tab::GetVSyncInterval( monitorIdx );

				//
				// Ask InGameConfig to refresh itself so calls to GetResolution/GetHDRModes
				// can have a visual impact on the video tab.
				//
				sys.QueueNotifyStatus( InGameConfig::NotificationType::Refresh );
			}
#endif

#if defined( RED_PLATFORM_ORBIS ) || defined( RED_PLATFORM_DURANGO )
			void OnConsoleSettingsChanged( CName groupPath, CName varName, IViewport* viewport, InGameConfig::ChangeReason changeReason )
			{
				auto& sys = InGameConfig::System::GetInstance();
				auto& group = sys.GetGroup( groupPath );

				if ( varName == RED_NAME_CONSTEXPR( "FilmGrain" ) )
				{
					auto& var = group.GetVarBool( varName );
					Config::GGlobalRenderingSettings.AllowFilmGrain = var.GetValue();
				}
				else if ( varName == RED_NAME_CONSTEXPR( "FrameRateModes" ) )
				{
#ifdef RED_PLATFORM_DURANGO
					const CONSOLE_TYPE consoleType = ::GetConsoleType();
					const Bool isNextGenQuality = ( consoleType >= CONSOLE_TYPE_XBOX_SERIES_X );
					if ( isNextGenQuality )
					{
						auto& var = group.GetVarListString( varName );
						const Config::FrameRateMode chosenMode = Config::FrameRateMode( var.GetIndex() );
						Config::GlobalRenderingSettings performanceSettingsTemplate;
						switch ( chosenMode )
						{
						case Config::FrameRateMode::Performance:
							performanceSettingsTemplate = Config::GetRenderSettings_CONSOLEEARLYNEXTGEN_PERFORMANCE();
							if ( viewport )
							{
								viewport->RequestVSyncInterval( 1 ); // 60 hz
							}
							break;
						case Config::FrameRateMode::Quality:
							performanceSettingsTemplate = Config::GetRenderSettings_CONSOLEEARLYNEXTGEN_QUALITY();
							if ( viewport )
							{
								viewport->RequestVSyncInterval( 2 ); // 30 hz
							}
							break;
						case Config::FrameRateMode::DynamicQuality:
							RED_FATAL( "not expected" ); // TODO
							// No change, will be changing dynamically
							break;
						default: RED_FATAL("not expected");
						}

						Config::GGlobalRenderingSettings.NumShadowCascades = performanceSettingsTemplate.NumShadowCascades;
						Config::GGlobalRenderingSettings.ShadowCascadeResolution = performanceSettingsTemplate.ShadowCascadeResolution;
						Config::GGlobalRenderingSettings.ScreenSpaceReflectionLevel = performanceSettingsTemplate.ScreenSpaceReflectionLevel;
						Config::GGlobalRenderingSettings.AnisotropyLevel = performanceSettingsTemplate.AnisotropyLevel;
						Config::GGlobalRenderingSettings.ShadowLODBias = performanceSettingsTemplate.ShadowLODBias;
						Config::GGlobalRenderingSettings.TextureMipBias = performanceSettingsTemplate.TextureMipBias;
						Config::GGlobalRenderingSettings.VolFogTileSize = performanceSettingsTemplate.VolFogTileSize;
						Config::GGlobalRenderingSettings.VolFogNumSlices = performanceSettingsTemplate.VolFogNumSlices;
						Config::GGlobalRenderingSettings.VolFogNumSamples = performanceSettingsTemplate.VolFogNumSamples;
						Config::GGlobalRenderingSettings.ConeAOQuality = performanceSettingsTemplate.ConeAOQuality;
						Config::GGlobalRenderingSettings.MotionBlurQuality = performanceSettingsTemplate.MotionBlurQuality;
						Config::GGlobalRenderingSettings.LocalShadowsResolution = performanceSettingsTemplate.LocalShadowsResolution;
						Config::GGlobalRenderingSettings.LocalShadowsProcessedPerFrame = performanceSettingsTemplate.LocalShadowsProcessedPerFrame;
					}
#endif
				}
				else if ( varName == RED_NAME_CONSTEXPR( "ChromaticAberration" ) )
				{
					auto& var = group.GetVarBool( varName );
					Config::GGlobalRenderingSettings.allowChromaticAberration = var.GetValue();
				}
				else if ( varName == RED_NAME_CONSTEXPR( "DepthOfField" ) )
				{
					auto& var = group.GetVarBool( varName );
					Config::GGlobalRenderingSettings.allowDOF = var.GetValue();
				}
				else if ( varName == RED_NAME_CONSTEXPR( "LensFlares" ) )
				{
					auto& var = group.GetVarBool( varName );
					Config::GGlobalRenderingSettings.allowImageBasedFlares = var.GetValue();
				}
				else if ( varName == RED_NAME_CONSTEXPR( "MotionBlur" ) )
				{
					auto& var = group.GetVarBool( varName );
					Config::GGlobalRenderingSettings.allowMotionBlur = var.GetValue();
					Config::GGlobalRenderingSettings.MotionBlurQuality = 0;
				}
				else if ( varName == RED_NAME_CONSTEXPR( "TonemappingMidpoint" ) )
				{
					if ( viewport )
					{
						const Float tonemappingMidpoint = video_tab::GetTonemappingMidpoint();
						viewport->RequestHDRTonemappingMidpoint( tonemappingMidpoint );
					}
				}
				else if ( varName == RED_NAME_CONSTEXPR( "MaxMonitorBrightness" ) )
				{
					if ( viewport )
					{
						const Float maxMonitorBrightness = video_tab::GetMaxMonitorBrightness();
						viewport->RequestHDRMaxBrightness( maxMonitorBrightness );
					}
				}
				else if ( varName == RED_NAME_CONSTEXPR( "PaperWhiteLevel" ) )
				{
					if ( viewport )
					{
						const Float paperWhiteLevel = video_tab::GetPaperWhiteLevel();
						viewport->RequestHDRPaperWhite( paperWhiteLevel );
					}
				}
				else if ( varName == RED_NAME_CONSTEXPR( "Gamma" ) )
				{
					if ( viewport )
					{
						const Float gamma = video_tab::GetGamma();
						viewport->RequestGamma( gamma );
					}
				}
				else if ( varName == RED_NAME_CONSTEXPR( "HDRModes" ) )
				{
#ifdef RED_PLATFORM_ORBIS
					if( viewport )
					{
						const auto hdrMode = rend::settings::video_tab::GetHDRMode( 0, VWM_Borderless );
						viewport->RequestHDRMode( hdrMode );
					}
#endif

#ifdef RED_PLATFORM_DURANGO
					if ( viewport )
					{
						//
						// On Durango this hits because HDR mode saved in local settings differs from 
						// what is set in the viewport. We actually do not care about what was saved but
						// we need to update InGameConfig "HDRModes" variable so that scripts can 
						// pick up proper calibration screen based on it. Viewport already holds proper value
						// queried from app::platform::HasHdrEnabled()
						//
						const auto durangoHDRSupport = viewport->GetHDRMode() != GpuWrapApi::HDRMODE_None;
						video_tab::SetHDRMode( durangoHDRSupport );
					}
#endif
				}
			}
#endif

			void OnGameViewportCreated( IViewport* viewport, Bool isNextGen )
			{
				if ( viewport )
				{
					//
					// [PC/XBOX/PS4] Make sure that viewport has up to date values. Some of them may be
					//				 scheduled but not applied yet.
					//
					viewport->UpdateViewportProperties();

#ifdef RED_PLATFORM_DURANGO
					if ( isNextGen )
					{
						// We are so next gen! 60 fps by default!
						viewport->RequestVSyncInterval( 1 );
					}
					else
					{
						// If we are not so next-gen - hide the framerate mode option and contemplate on sadness...
						auto& sys = InGameConfig::System::GetInstance();
						auto& group = sys.GetGroup( s_inGameConfigGroup_Display );
						auto& var = group.GetVarListString( s_inGameConfigVar_FrameRateModes );
						var.SetVisible( false );

						// ... in 30hz
						viewport->RequestVSyncInterval( 2 );
					}
#endif

#ifdef RED_PLATFORM_WINPC
					//
					// [PC] Update Vsync/FPS clamps stuff.
					//
					video_tab::SetVSync( viewport->GetVSyncInterval(), viewport->GetOutputMonitor() );
					video_tab::SetFPSClamp( viewport->GetFPSClamp() );

					//
					// [PC] Update InGameConfig variables for monitor, resolution and window mode.
					//
					video_tab::SetMonitorIdx( ( Int32 )viewport->GetOutputMonitor() );
					video_tab::SetResolution( viewport->GetRendererWidth(), viewport->GetRendererHeight(), ( Int32 )viewport->GetOutputMonitor() );
					video_tab::SetWindowMode( viewport->GetWindowMode() );

					//
					// [PC] Update InGameConfig variables for DRS, DLSS and their settings.
					//
					graphics_tab::SetDRS( viewport->GetDRS() );
					graphics_tab::SetDLSS( viewport->GetDLSS(), viewport->GetDLSSQualityPreset(), InGameConfig::Source::PresetCode );
					graphics_tab::SetSRS( viewport->GetSRS() );
#endif

					//
					// [PC/XBOX/PS4] Update InGameConfig variables for calibration values. 
					//
					video_tab::SetMaxMonitorBrightness( viewport->GetHDRMaxMonitorBrightness(), ( Int32 )viewport->GetOutputMonitor() );
					video_tab::SetGamma( viewport->GetGamma() );
					video_tab::SetTonemappingMidpoint( viewport->GetHDRTonemappingMidpoint() );
					video_tab::SetPaperWhiteLevel( viewport->GetHDRPaperWhite() );

#if defined( RED_PLATFORM_WINPC ) || defined( RED_PLATFORM_ORBIS)
					//
					// [PC/PS4] Update InGameConfig variables for HDRModes. These two platforms can switch between available modes.
					//
					video_tab::SetHDRMode( viewport->GetHDRMode(), ( Int32 )viewport->GetOutputMonitor(), viewport->IsFullscreenMode() );
#else
					//
					// [XBOX] This is called on startup, before the engagement screen, in order to trigger post engagement screen
					//        settings loading callback in case viewport requested different HDR mode than the one was saved 
					//        (and the callback is diff-based).
					//
					video_tab::SetHDRMode( viewport->GetHDRMode() != GpuWrapApi::eHDRMode::HDRMODE_None );
#endif
				}
			}

		}

		namespace video_tab
		{
#ifdef RED_PLATFORM_WINPC

			void SetResolution( Uint32 inWidth, Uint32 inHeight, Int32 inMonitorIdx )
			{
				//
				// Pull var from InGameConfigSystem
				//
				auto& sys = InGameConfig::System::GetInstance();
				auto& group = sys.GetGroup( s_inGameConfigGroup_Display );
				auto& var = group.GetVarListString( s_inGameConfigVar_Resolution );

				//
				//
				//
				red::DynArray< String > availableResolutions{ job::PoolJobScope() };
				GRender->ListAvailableDisplayModes( availableResolutions, inMonitorIdx );
				RED_ASSERT( !availableResolutions.Empty() );

				//
				// Check if requested resolution is on the list and if it so pull its index.
				//
				const Int32 resolutionIndex = availableResolutions.GetIndex( String::Printf( job::PoolJobScope(), "%ix%i", inWidth, inHeight ) );
				var.InitializeDynamic( availableResolutions, resolutionIndex != red::INVALID_INDEX ? resolutionIndex : 0 );
			}

			void GetResolution( Uint32 &outWidth, Uint32 &outHeight, Int32 monitorIdx )
			{
				//
				// Pull var from InGameConfigSystem
				//
				auto& sys = InGameConfig::System::GetInstance();
				auto& group = sys.GetGroup( s_inGameConfigGroup_Display );
				auto& var = group.GetVarListString( s_inGameConfigVar_Resolution );

				//
				// If the Resolution variable is already initialized, pull resolution
				// it is pointing to. In next step we will try to match it with
				// with set of resolutions for specified monitor.
				//
				Int32 storedWidth = -1;
				Int32 storedHeight = -1;
				if ( !var.GetValues().Empty() )
				{
					const String storedResolutionStr = var.GetValue();
					Config::Helper::ExtractDisplayModeValues( storedResolutionStr, storedWidth, storedHeight );
				}

				//
				// This is a bit tricky cause monitorIdx might not be the one that was saved.
				// What is more, even if it is the same index, monitor could be potentially
				// swapped since last run.
				//
				red::DynArray< String > availableResolutions{ job::PoolJobScope() };
				GRender->ListAvailableDisplayModes( availableResolutions, monitorIdx );
				RED_ERROR( !availableResolutions.Empty(), "IRender::ListAvailableDisplayModes() for monitor %i returned empty list", monitorIdx );
				if ( !var.IsDynamicInitialized() && var.GetValues().Empty() )
					var.InitializeDynamic( availableResolutions, 0 );
				else
					var.SetValues( availableResolutions, false );
				
				//
				// In case Resolution was storing some value prior to setting it with current monitor options,
				// try to find this resolution on the list of the current monitor options. If it is possible
				// update the index without any notifications - just to update it visually. Otherwise continue.
				//
				if ( storedWidth != -1 && storedHeight != -1 )
				{
					const Int32 resolutionIndex = availableResolutions.GetIndex( String::Printf( job::PoolJobScope(), "%ix%i", storedWidth, storedHeight ) );
					if ( resolutionIndex != red::INVALID_INDEX && resolutionIndex != var.GetIndex() )
					{
						var.SetIndex( resolutionIndex, InGameConfig::Source::Code, false );
					}
				}

				//
				// Use index picked up from InGameConfig var to choose resolution from the available ones.
				// If for some reason something went wrong default to native resolution.
				//
				const Int32 currentResolutionIndex = var.GetIndex();
				Int32 width, height;
				if ( currentResolutionIndex >= 0 && Config::Helper::ExtractDisplayModeValues( availableResolutions[ currentResolutionIndex ], width, height ) )
				{
					outWidth = width;
					outHeight = height;
				}
				else
				{
					RED_FATAL_ASSERT( GRender, "Renderer expected for 'GetNativeResolution' fallback" );
					GRender->GetNativeResolution( outWidth, outHeight );
				}
			}

			void SetMonitorIdx( Int32 monitorIdx )
			{
				//
				// Pull var from InGameConfigSystem
				//
				auto& sys = InGameConfig::System::GetInstance();
				auto& group = sys.GetGroup( s_inGameConfigGroup_Display );
				auto& var = group.GetVarListInt( s_inGameConfigVar_OutputMonitor );

				//
				// Fill in array with monitors indices
				//
				const Int32 monitorsCount = GRender->GetMonitorCount();
				red::DynArray< Int32 > monitors{ job::PoolJobScope() };
				for ( Int32 monitor_i = 0; monitor_i < monitorsCount; monitor_i++ ) monitors.PushBack( monitor_i );

				//
				// Explicitly set values.
				//
				auto &monitorVar = sys.GetVarListInt( s_inGameConfigGroup_Display, s_inGameConfigVar_OutputMonitor );
				monitorVar.SetValues( monitors, false );
				monitorVar.SetIndex( monitorIdx < 0 ? 0 : monitorIdx, InGameConfig::Source::Code, false );
			}

			Int32 GetMonitorIdx()
			{
				//
				// Pull var from InGameConfigSystem
				//
				auto& sys = InGameConfig::System::GetInstance();
				auto& group = sys.GetGroup( s_inGameConfigGroup_Display );
				auto& var = group.GetVarListInt( s_inGameConfigVar_OutputMonitor );

				//
				// In case this is first time this variable is touched - pull number of monitors
				// and initialize it with index loaded from UserSettings.json if possible.
				// NOTE: check for monitor cout should be probably done every  time this var is accessed.
				//
				if ( !var.IsDynamicInitialized() && var.GetValues().Empty() )
				{
					red::DynArray< Int32 > monitorsVals{ job::PoolJobScope() };
					const Int32 monitorsCount = GRender->GetMonitorCount();
					for ( Int32 monitor_i = 0; monitor_i < monitorsCount; monitor_i++ )
					{
						monitorsVals.PushBack( monitor_i );
					}

					var.InitializeDynamic( monitorsVals, 0 );
				}

				//
				// Pull loaded value or default one in case it is missing from UserSettings.json
				//
				const Int32 monitorIdx = var.GetValue();
				return monitorIdx;
			}

			EViewportWindowMode GetWindowMode()
			{
				//
				// Pull var from InGameConfigSystem
				//
				auto& sys = InGameConfig::System::GetInstance();
				auto& group = sys.GetGroup( s_inGameConfigGroup_Display );
				auto& var = group.GetVarListString( s_inGameConfigVar_WindowMode );

				//
				// Index of InGameConfig variable match EViewportWindowMode enum.
				//
				return ( EViewportWindowMode )var.GetIndex();
			}

			void SetWindowMode( EViewportWindowMode windowMode )
			{
				//
				// Pull var from InGameConfigSystem
				//
				auto& sys = InGameConfig::System::GetInstance();
				auto& group = sys.GetGroup( s_inGameConfigGroup_Display );
				auto& var = group.GetVarListString( s_inGameConfigVar_WindowMode );

				var.SetIndex( ( Int32 )windowMode, InGameConfig::Source::Code, false );
			}

			void SetVSync( Uint32 vsyncInterval, Int32 outputMonitor )
			{
				auto& sys = InGameConfig::System::GetInstance();
				auto& group = sys.GetGroup( s_inGameConfigGroup_Display );
				auto& var = group.GetVarListString( s_inGameConfigVar_VSync );

				red::DynArray< String > availableVSyncFPSModes { job::PoolJobScope() };
				GRender->ListAvailableVSyncFPSModes( availableVSyncFPSModes, outputMonitor );

				var.SetValues( availableVSyncFPSModes, false );
				var.SetIndex( vsyncInterval, InGameConfig::Source::Code, false );
			}

			Uint32 GetVSyncInterval( Int32 monitorIdx )
			{
				auto& sys = InGameConfig::System::GetInstance();
				auto& group = sys.GetGroup( s_inGameConfigGroup_Display );
				auto& var = group.GetVarListString( s_inGameConfigVar_VSync );

				//
				// In case this is first time this variable is touched - pull vsync intervals in for
				// of target FPS strings and initialize it with index loaded from UserSettings.json if possible.
				//
				red::DynArray< String > availableVSyncFPSModes { job::PoolJobScope() };
				GRender->ListAvailableVSyncFPSModes( availableVSyncFPSModes, monitorIdx );
				if ( !var.IsDynamicInitialized() && var.GetValues().Empty() )
					var.InitializeDynamic( availableVSyncFPSModes, 0 );
				else
					var.SetValues( availableVSyncFPSModes, false );

				const Int32 vsyncIdx = var.GetRequestedIndex();
				return Max( vsyncIdx, 0 );
			}

			void SetFPSClamp( Int32 fpsClamp )
			{
				auto& sys = InGameConfig::System::GetInstance();
				auto& group = sys.GetGroup( s_inGameConfigGroup_Display );
				auto& varOnOff = group.GetVarBool( s_inGameConfigVar_MaximumFPSOnOff );
				auto& varValue = group.GetVarInt( s_inGameConfigVar_MaximumFPSValue );

				if ( fpsClamp > 0 )
				{
					varOnOff.SetValue( true, InGameConfig::Source::Code );
					varValue.SetValue( fpsClamp, InGameConfig::Source::Code );
				}
				else
					varOnOff.SetValue( false, InGameConfig::Source::Code );
			}

			Int32 GetFPSClamp()
			{
				auto& sys = InGameConfig::System::GetInstance();
				auto& group = sys.GetGroup( s_inGameConfigGroup_Display );
				auto& varOnOff = group.GetVarBool( s_inGameConfigVar_MaximumFPSOnOff );
				auto& varValue = group.GetVarInt( s_inGameConfigVar_MaximumFPSValue );

				const Bool fpsClampEnabled = varOnOff.GetValue();
				return fpsClampEnabled ? varValue.GetValue() : -1;
			}

#endif
#if defined( RED_PLATFORM_WINPC ) || defined( RED_PLATFORM_ORBIS )
			//
			// [PC/PS4]
			//
			GpuWrapApi::eHDRMode GetHDRMode( Int32 monitorIdx, Bool fullscreen )
			{
				//
				// Pull var from InGameConfigSystem
				//
				auto& sys = InGameConfig::System::GetInstance();
				auto& group = sys.GetGroup( s_inGameConfigGroup_Display );
				auto& var = group.GetVarListString( s_inGameConfigVar_HDRModes );

				//
				// Convert available HDR modes to list of indices and display values.
				//
				const Uint32 availableHDRModes = GRender->GetSupportedHDRModesMask( monitorIdx, fullscreen );
				red::DynArray< String > hdrModes{ job::PoolJobScope() };
				red::DynArray< CName > hdrModesDisplayNames{ job::PoolJobScope() };
				const GpuWrapApi::eHDRMode currentMode = ( GpuWrapApi::eHDRMode ) var.GetIndex();
				utils::GetHDRModesDynamicData( currentMode, monitorIdx, fullscreen, hdrModes, hdrModesDisplayNames );

				//
				// Initialize dynamic variable.
				//
				if ( !var.IsDynamicInitialized() && var.GetValues().Empty() )
					var.InitializeDynamic( hdrModes, 0 );
				else
					var.SetValues( hdrModes, false );
				var.SetDisplayValues( hdrModesDisplayNames );

				//
				// Pull loaded value or default one in case it is missing from UserSettings.json
				//
				const Int32 hdrModeIndex = var.GetIndex();
				return ( GpuWrapApi::eHDRMode ) hdrModeIndex;
			}

			//
			// [PC/PS4]
			//
			void SetHDRMode( GpuWrapApi::eHDRMode requestedMode, Int32 monitorIdx, Bool fullscreen )
			{
				//
				// Pull var from InGameConfigSystem
				//
				auto& sys = InGameConfig::System::GetInstance();
				auto& group = sys.GetGroup( s_inGameConfigGroup_Display );
				auto& var = group.GetVarListString( s_inGameConfigVar_HDRModes );

				//
				// Convert available HDR modes to list of indices and display values.
				//
				const Uint32 availableHDRModes = GRender->GetSupportedHDRModesMask( monitorIdx, fullscreen );
				red::DynArray< String > hdrModes{ job::PoolJobScope() };
				red::DynArray< CName > hdrModesDisplayNames{ job::PoolJobScope() };
				const Int32 matchingIndex = utils::GetHDRModesDynamicData( requestedMode, monitorIdx, fullscreen, hdrModes, hdrModesDisplayNames );

				var.SetValues( hdrModes, false );
				var.SetDisplayValues( hdrModesDisplayNames );
				var.SetEnabled( hdrModes.Size() > 1 );

				if ( matchingIndex != -1 )
				{
					var.SetIndex( matchingIndex, InGameConfig::Source::Code, false );
				}
			}
#else
			//
			// [XBOX]
			//
			Bool GetHDRMode()
			{
				auto& sys = InGameConfig::System::GetInstance();
				auto& group = sys.GetGroup( s_inGameConfigGroup_Display );
				auto& var = group.GetVarListString( s_inGameConfigVar_HDRModes );
				RED_ASSERT( var.GetIndex() >= 0 );

				return ( Bool )var.GetIndex();
			}

			//
			// [XBOX] 
			//
			void SetHDRMode( Bool hdrOn )
			{
				auto& sys = InGameConfig::System::GetInstance();
				auto& group = sys.GetGroup( s_inGameConfigGroup_Display );
				auto& var = group.GetVarListString( s_inGameConfigVar_HDRModes );

				var.SetIndex( hdrOn ? 1 : 0, InGameConfig::Source::Code, false );
			}

#endif
			Float GetTonemappingMidpoint()
			{
				auto& sys = InGameConfig::System::GetInstance();
				auto& group = sys.GetGroup( s_inGameConfigGroup_Display );
				auto& var = group.GetVarFloat( s_inGameConfigVar_TonemappingMidpoint );

				return var.GetValue();
			}

			Float GetMaxMonitorBrightness( Int32 monitorIdx /*= -1*/ )
			{
				auto& sys = InGameConfig::System::GetInstance();
				auto& group = sys.GetGroup( s_inGameConfigGroup_Display );
				auto& var = group.GetVarFloat( s_inGameConfigVar_MaxMonitorBrightness );
				
				//
				// In case there is no UserSettings.json file and monitor was specified
				// pull its max luminance and set it as the value. 
				//
				if ( sys.IsFirstRun() && monitorIdx != -1 )
				{
					var.SetValue( GRender->GetMonitorMaxLuminanceNits( monitorIdx ), InGameConfig::Source::Code );
				}

				return var.GetValue();
			}

			Float GetPaperWhiteLevel()
			{
				auto& sys = InGameConfig::System::GetInstance();
				auto& group = sys.GetGroup( s_inGameConfigGroup_Display );
				auto& var = group.GetVarFloat( s_inGameConfigVar_PaperWhiteLevel );

				return var.GetValue();
			}

			Float GetGamma()
			{
				auto& sys = InGameConfig::System::GetInstance();
				auto& group = sys.GetGroup( s_inGameConfigGroup_Display );
				auto& var = group.GetVarFloat( s_inGameConfigVar_Gamma );

				return var.GetValue();				
			}

			void SetMaxMonitorBrightness( Float maxMonitorBrightness, Int32 monitorIdx )
			{
				//
				// Pull the variable from InGameConfigSystem
				//
				auto& sys = InGameConfig::System::GetInstance();
				auto& group = sys.GetGroup( s_inGameConfigGroup_Display );
				auto& var = group.GetVarFloat( s_inGameConfigVar_MaxMonitorBrightness );

				//
				// If UserSettings.json do not exist at all - pull system value for monitor brightness.
				//
				if ( sys.IsFirstRun() )
				{
					var.SetValue( GRender->GetMonitorMaxLuminanceNits( monitorIdx ), InGameConfig::Source::Code );
				}
				else
				{
					var.SetValue( maxMonitorBrightness, InGameConfig::Source::Code );
				}
			}

			void SetTonemappingMidpoint( Float tonemappingMidpoint )
			{
				auto& sys = InGameConfig::System::GetInstance();
				auto& group = sys.GetGroup( s_inGameConfigGroup_Display );
				auto& var = group.GetVarFloat( s_inGameConfigVar_TonemappingMidpoint );
				var.SetValue( tonemappingMidpoint, InGameConfig::Source::Code );
			}

			void SetPaperWhiteLevel( Float paperWhiteLevel )
			{
				auto& sys = InGameConfig::System::GetInstance();
				auto& group = sys.GetGroup( s_inGameConfigGroup_Display );
				auto& var = group.GetVarFloat( s_inGameConfigVar_PaperWhiteLevel );
				var.SetValue( paperWhiteLevel, InGameConfig::Source::Code );
			}

			void SetGamma( Float gamma )
			{
				auto& sys = InGameConfig::System::GetInstance();
				auto& group = sys.GetGroup( s_inGameConfigGroup_Display );
				auto& var = group.GetVarFloat( s_inGameConfigVar_Gamma );
				var.SetValue( gamma, InGameConfig::Source::Code );
			}

		}

		namespace graphics_tab
		{
#ifdef RED_PLATFORM_WINPC 

			//
			// [PC]
			//
			void SetQuickPreset( Config::GraphicsQualityLevel qualityLevel )
			{
				//
				// In order for this method to be kinda fail-safe, we first map provided 
				// quality level to one that is supported by InGameConfig system. Other
				// quality levels than Low, Medium, High, Ultra, RTX Medium, RTX Ultra
				// will default to Custom.
				//
				qualityLevel = utils::MapUnsupportedQualityLevelsToCustom( qualityLevel );
					
				//
				// Pull var from InGameConfigSystem
				//
				auto& sys = InGameConfig::System::GetInstance();
				auto& group = sys.GetGroup( s_inGameConfigGroup );
				auto& var = group.GetVarListString( s_inGameConfigVar_QuickPresets );

				RED_ASSERT( !var.GetValues().Empty(), "OnLoadQuickPresetFromUserSettings was not called." );

				//
				// Find matching index and update the variable. This will trigger callbacks::OnPresetChange() function.
				//
				const Uint32 qualityAsIdx = utils::MapGraphicsQualityLevelToIdx( qualityLevel );
				RED_ASSERT( qualityAsIdx < var.GetValues().Size(), "Trying to set InGameConfig QuickPreset to a preset that does not extists" );
				var.SetIndex( qualityAsIdx, InGameConfig::Source::Code );
			}

			//
			// [PC]
			//
			Config::GraphicsQualityLevel GetQuickPreset()
			{
				//
				// Pull var from InGameConfigSystem
				//
				auto& sys = InGameConfig::System::GetInstance();
				auto& group = sys.GetGroup( s_inGameConfigGroup );
				auto& var = group.GetVarListString( s_inGameConfigVar_QuickPresets );

				RED_ASSERT( !var.GetValues().Empty(), "OnLoadQuickPresetFromUserSettings was not called." );

				return utils::MapIdxToGraphicsQualityLevel( var.GetIndex() );
			}

			//
			// [PC]
			//
			void SetDLSS( Bool enabled, Int32 dlssQualityPresetIdx, InGameConfig::Source source )
			{
				auto& sys = InGameConfig::System::GetInstance();
				auto& group = sys.GetGroup( s_inGameConfigGroup_DynamicResolution );
				auto& var = group.GetVarListString( s_inGameConfigVar_DLSS );

				// NOTE: This hardware support check is probably redundant but I do not trust RED Engine and its systems too much ;p
				const Bool dlssHardwareSupport = GRender->GetHardwareDLSSSupport();
				var.SetEnabled( dlssHardwareSupport );
				if ( enabled && dlssHardwareSupport )
				{
					//const Int32 dlssQualityMappedToIdx = dlssQuality + 1;
					var.SetIndex( dlssQualityPresetIdx, source );
				}
				else
				{
					var.SetIndex( 0, source );
				}
			}

			//
			// [PC]
			//
			void SetDRS( Bool enabled )
			{
				auto& sys = InGameConfig::System::GetInstance();
				auto& group = sys.GetGroup( s_inGameConfigGroup_DynamicResolution );
				auto& var = group.GetVarBool( s_inGameConfigVar_DynamicResolutionScaling );

				var.SetValue( enabled, InGameConfig::Source::Code );
			}

			//
			// [PC]
			//
			void SetSRS( Bool enabled )
			{
				auto& sys = InGameConfig::System::GetInstance();
				auto& group = sys.GetGroup( s_inGameConfigGroup_DynamicResolution );
				auto& var = group.GetVarBool( s_inGameConfigVar_StaticResolutionScaling );

				var.SetValue( enabled, InGameConfig::Source::Code );
			}

			//
			// [PC]
			//
			Bool GetDLSSEnabled()
			{
				auto& sys = InGameConfig::System::GetInstance();
				auto& group = sys.GetGroup( s_inGameConfigGroup_DynamicResolution );
				auto& var = group.GetVarListString( s_inGameConfigVar_DLSS );

				const Int32 varIdx = var.GetIndex();
				RED_ASSERT( varIdx != -1 );
				return varIdx > 0;
			}

			//
			// [PC]
			//
			Int32 GetDLSSQuality()
			{
				auto& sys = InGameConfig::System::GetInstance();
				auto& group = sys.GetGroup( s_inGameConfigGroup_DynamicResolution );
				auto& var = group.GetVarListString( s_inGameConfigVar_DLSS );

				const Int32 varIdx = var.GetIndex();
				RED_ASSERT( varIdx >= -1 );
				return varIdx;
			}

			//
			// [PC]
			//
			Bool GetDRSEnabled()
			{
				auto& sys = InGameConfig::System::GetInstance();
				auto& group = sys.GetGroup( s_inGameConfigGroup_DynamicResolution );
				auto& var = group.GetVarBool( s_inGameConfigVar_DynamicResolutionScaling );

				const Bool varVal = var.GetValue();
				return varVal;
			}

			//
			// [PC]
			//
			Bool GetSRSEnabled()
			{
				auto& sys = InGameConfig::System::GetInstance();
				auto& group = sys.GetGroup( s_inGameConfigGroup_DynamicResolution );
				auto& var = group.GetVarBool( s_inGameConfigVar_StaticResolutionScaling );

				const Bool varVal = var.GetValue();
				return varVal;
			}

			//
			// [PC]
			//
			Uint32 GetDRSMaximalResolutionPercentage()
			{
				auto& sys = InGameConfig::System::GetInstance();
				auto& group = sys.GetGroup( s_inGameConfigGroup_DynamicResolution );
				auto& var = group.GetVarInt( s_inGameConfigVar_DRS_MaximalResolution );

				const Uint32 varVal = ( Uint32 ) var.GetValue();
				return varVal;
			}

			//
			// [PC]
			//
			Uint32 GetDRSMinimalResolutionPercentage()
			{
				auto& sys = InGameConfig::System::GetInstance();
				auto& group = sys.GetGroup( s_inGameConfigGroup_DynamicResolution );
				auto& var = group.GetVarInt( s_inGameConfigVar_DRS_MinimalResolution );

				const Uint32 varVal = ( Uint32 ) var.GetValue();
				return varVal;
			}

			//
			// [PC]
			//
			Uint32 GetDRSTargetFPS()
			{
				auto& sys = InGameConfig::System::GetInstance();
				auto& group = sys.GetGroup( s_inGameConfigGroup_DynamicResolution );
				auto& var = group.GetVarInt( s_inGameConfigVar_DRS_TargetFPS );

				const Uint32 varVal = ( Uint32 ) var.GetValue();
				return varVal;
			}

			//
			// [PC]
			//
			Uint32 GetSRSResolutionPercentage()
			{
				auto& sys = InGameConfig::System::GetInstance();
				auto& group = sys.GetGroup( s_inGameConfigGroup_DynamicResolution );
				auto& var = group.GetVarInt( s_inGameConfigVar_SRS_Resolution );

				const Uint32 varVal = ( Uint32 ) var.GetValue();
				return varVal;
			}
#endif
		}
	}
}


