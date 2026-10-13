/**;
* Copyright (c) 2018 CD Projekt Red. All Rights Reserved.
*/
#pragma once

//****************************************************************************************************************//
//****************************************************************************************************************//

namespace Config
{
	// #NOTE: Please keep this in sync with TaskBarQualityRadioEvent in editorTaskBar.h!
	enum class GraphicsQualityLevel : Uint32
	{
		Low,				// Gameplay quality level
		Medium,				// Gameplay quality level
		High,				// Gameplay quality level
		Ultra,				// Gameplay quality level
		RTXMedium,			// Base Quality: Ultra, Ray Traced Lighting: Medium, Ray Traced Reflections: Off, Ray Traced Shadows: On, DLSS: Auto
		RTXUltra,			// Base Quality: Ultra, Ray Traced Lighting: Ultra, Ray Traced Reflections: On, Ray Traced Shadows: On, DLSS: Auto
		Cinematic,			// Cinematic capture mode
		Cinematic_RTX,
		CinematicEXR,		// Cinematic capture mode with EXR render target grab (super high quality)
		CinematicEXR_RTX,
		Console,			// Base quality level for consoles. Needs probably few tweaks.
		ConsolePro,
		ConsoleEarlyNextGen,	// This is for Next Gen consoles running in back-compat mode (i.e. not native title)
		ConsoleEarlyNextGenQuality,		// This is for Next Gen consoles running in back-compat mode (i.e. not native title)
		GeForceNow,			// Base quality level for streaming (60Hz + high quality graphics settings).
		IconsGeneration,	// Preset for UI Icons Generation for all items.
		SafeMode,			// Most basic quality level - show just albedo (when OOM, consoles, low end)

		Auto,				// Pick up quality based on hardware limitations

#ifdef RED_PLATFORM_WINPC
		FromUserSettings,	// Pick up quality from locally saved settings. In case it matches with Low/Medium/Hight/Ultra it will pick it up.
		Custom,
#endif
		Count,
	};



	// #NOTE: Please keep this in sync with TaskBarTextureQualityRadioEvent in editorTaskBar.h!
	enum class TextureQualityLevel : Uint32
	{
		Console,	// Dropping mipmaps based on console bias that the texture was set up with
		ConsoleSafe,

		Low,		// Two mipmaps get dropped
		Medium,		// One mipmap gets dropped
		High,		// No mipmap gets dropped

		SafeMode,	// Drop as many mips as possible (when OOM, consoles, low end)

		Auto,		// Pick up quality based on hardware limitations

		Count,
	};


	// TODO add seperate 30/40 switch for resolution and quality modes when upgraded to XBox GDK 
	enum class FrameRateMode : Uint8
	{
		Performance,			// 60hz
		Quality,				// 30hz, 
	//	Resolution,				// TODO support more than 1800p HD, that is, make HUD switchable between 1800p and 4k on XSX, or better, stick with 1800p HUD and have the facility for the 3D render to be higher resolution than the HUD (This should be easy with the GDK, since we can go back to using display planes for the HUD even with HDR)
		DynamicQuality,			// Performance when in combat, otherwise quality
		Count
	};


}

//****************************************************************************************************************//
//****************************************************************************************************************//

RTTI_DECLARE_ENUM_IN_NAMESPACE( GraphicsQualityLevel, Config );
RTTI_DECLARE_ENUM_IN_NAMESPACE( TextureQualityLevel, Config );

//****************************************************************************************************************//
//****************************************************************************************************************//

