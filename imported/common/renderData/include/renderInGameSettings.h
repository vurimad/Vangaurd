enum EViewportWindowMode : Uint8;

namespace GpuWrapApi
{
	enum eHDRMode : Uint8;
}

namespace Config
{
	enum class GraphicsQualityLevel : Uint32;
	enum class FrameRateMode : Uint8;
	struct GlobalRenderingSettings;
	struct MemoryInfo;
}

namespace InGameConfig
{
	enum class NotificationType : Uint8;
	enum class ChangeReason : Int8;
	enum class VarType : Uint8;
	enum class Source : Int8;
}

class IViewport;

namespace rend
{
	namespace settings
	{
		namespace callbacks
		{
#ifdef RED_PLATFORM_WINPC
			//
			// [PC] Triggered when leaving options menu.
			//
			RENDER_DATA_API void OnSave( IViewport* viewport );

			//
			// [PC] Triggered when 'Apply' button was clicked.
			//
			RENDER_DATA_API void OnApply( IViewport* viewport );

			//
			// [PC] Triggered when OutputMonitor variable has been changed. Its sole purpose
			//		is to update HDRModes, Resolution and VSync options to match currently selected display.
			//
			RENDER_DATA_API void OnOutputMonitorChanged();

			//
			// [PC] Triggered when TonemappingMidpoint, MaxMonitorBrightness, PaperWhiteLevel or
			//		Gamma change so it can request proper values from the viewport. Main reason
			//		for this split is that the calibration values are immediate, while rest is
			//		confirmation only.
			//
			RENDER_DATA_API void OnDisplayCalibrationValuesChanged( IViewport* viewport );
			
			//
			// [PC] Triggered when InGameConfig "QuickPresets" variable was changed (manually clicked). It will cause
			//		dependent InGameConfig variables to update without calling any notifiers. Unless OnSave
			//		will be triggered, no changes will be applied neither to GGLobalRenderingSettings, nor to viewport.
			//
			RENDER_DATA_API void OnPresetChanged();

			//
			// [PC] Call it once on startup. Must be called before Get/SetQuickPreset is called.
			//
			RENDER_DATA_API void OnLoadQuickPresetFromUserSettings( const Config::MemoryInfo& );

			//
			// [PC] Call it once on startup. Must be called before Get/SetWindowMode is called.
			//
			RENDER_DATA_API void OnLoadWindowModesFromUserSettings();

			//
			// [PC] Triggered when InGameConfig "RayTracing" variable was changed. It will make rest of the 
			//		RTX settings visible or not, depending what was set.
			//
			RENDER_DATA_API void OnProcessRayTracingTabVisibility();
			
			//
			// [PC] Triggered when InGameConfig "RayTracing" variable was changed but not yet accepted.
			//		In case hardware do not support RTX, we never allow it to go to On/True.
			//
			RENDER_DATA_API Bool OnRayTracingToggleValidation();

			//
			// [PC] Triggered when InGameConfig "DLSS" variable was changed but not yet accepted.
			//		In case hardware do not support DLSS, we never allow it to go to On/True.
			//
			RENDER_DATA_API Bool OnDLSSToggleValidation();

			//
			// [PC] 
			//
			RENDER_DATA_API Bool OnDRSMinimalResolutionValidation();

			//
			// [PC] 
			//
			RENDER_DATA_API Bool OnDRSMaximalResolutionValidation();

			//
			// [PC] Triggered when InGameConfig "DRS" variable was changed. In case we set it to "true", 
			//		"DLSS" and "SRS" variable must be set to "Off".
			//
			RENDER_DATA_API void OnDRSChanged();

			//
			// [PC] Triggered when InGameConfig "SRS" variable was changed. In case we set it to "true", 
			//		"DLSS" and "DRS" variable must be set to "Off".
			//
			RENDER_DATA_API void OnSRSChanged();

			//
			// [PC] Triggered when InGameConfig "Maximum FPS" variable was changed. In case we set it to "false", 
			//		"Value" must be hidden.
			//
			RENDER_DATA_API void OnMaximumFPSOnOffChanged();

			//
			// [PC] Triggered when InGameConfig "DLSS" variable was changed and OnDLSSToggleValidation returned true.
			//		In case we set it to something else than "Off", "DRS" and "SRS" variable must be set to false and
			//		its variables hidden.
			//
			RENDER_DATA_API void OnDLSSChanged();


			//
			// [PC] Triggered when you run the application for the first time on PC and will handle
			//		graphics tab (so GGlobalRenderingSettings). Unfortunately, split between video and 
			//		graphics tab it needed due to different places they are initialized: the first one when 
			//		viewport is created, the second one after renderer is initialized.
			//
			RENDER_DATA_API void OnDetectCapabilitiesOnFreshStart_GraphicsTab( const Config::MemoryInfo& );

			//
			// [PC] Triggered when you run the application for the first time on PC and will handle
			//		video tab (so RenderViewport). Unfortunately, split between video and 
			//		graphics tab it needed due to different places they are initialized: the first one when 
			//		viewport is created, the second one after renderer is initialized.
			//
			RENDER_DATA_API void OnDetectCapabilitiesOnFreshStart_VideoTab();
#endif

#if defined( RED_PLATFORM_ORBIS ) || defined( RED_PLATFORM_DURANGO )

			//
			// [XBOX/PS4] Triggered when any graphics variable is changed. This is not only in game  but also when local 
			//			  user settings are loaded so that we can map stored values on startup. This is a completely 
			//			  separate path for consoles as they have less options and load local settings at different time than PC does.
			//
			RENDER_DATA_API void OnConsoleSettingsChanged( CName groupPath, CName varName, IViewport* viewport, InGameConfig::ChangeReason changeReason );
#endif
			//
			// [PC/XBOX/PS4] Triggered when game viewport was created. We use it to map its properties to InGameConfig
			//				 variables in order to have 1:1 match, fill in dynamic values, trigger differences between
			//			     upcoming local saving loading (consoles)... really different stuff between platforms but
			//				 all are considering video tab besides DLSS and DRS on PC.
			//
			RENDER_DATA_API void OnGameViewportCreated( IViewport* viewport, Bool isNextGen );
		}

		//
		// [PC/XBOX/PS4] Call it in order to decide whether InGameConfig is applicable 
		//				 within current application state.
		//
		RENDER_DATA_API Bool Valid();

		namespace video_tab
		{
#ifdef RED_PLATFORM_WINPC

			//
			// [PC] The procedure will pull list of available resolutions for specified monitor index
			//		and push it to InGameConfig "Resolution" variable. Then it will try to match
			//		specified width and height to one of the entry on the list and set it. If it fails
			//		default index will be set.
			//
			RENDER_DATA_API void SetResolution( Uint32 inWidth, Uint32 inHeight, Int32 inMonitorIdx );

			//
			//
			// [PC] The procedure will first save whichever resolution is currently stored in InGameConfig
			//		"Resolution" variable. If the variable was not set (0 entries) the stored value will be
			//		treated as invalid. Next, available set of resolutions for specified monitor will be pulled
			//		and InGameConfig "Resolution variable will be populated with it. If previous resolution was
			//	    a valid value we will try to match it with entries on the new list. 
			//
			RENDER_DATA_API void GetResolution( Uint32 &outWidth, Uint32 &outHeight, Int32 monitorIdx );

			//
			//  
			//
			// 
			RENDER_DATA_API void SetMonitorIdx( Int32 monitorIdx );

			//
			//
			//
			RENDER_DATA_API Int32 GetMonitorIdx();


			//
			// Window mode is not a dynamic InGameConfig variable so it will be just pulled from UserSettings.json
			// and its index mapped to EViewportWindowMode enum.
			//
			RENDER_DATA_API EViewportWindowMode GetWindowMode();

			RENDER_DATA_API void SetWindowMode( EViewportWindowMode windowMode );

			RENDER_DATA_API void SetVSync( Uint32 vsyncInterval, Int32 outputMonitor );
			RENDER_DATA_API Uint32 GetVSyncInterval( Int32 monitorIdx );
			RENDER_DATA_API void SetFPSClamp( Int32 fpsClamp );
			RENDER_DATA_API Int32 GetFPSClamp();
#endif



#if defined( RED_PLATFORM_WINPC ) || defined( RED_PLATFORM_ORBIS )

			//
			// [PC/PS4]  
			//  
			RENDER_DATA_API GpuWrapApi::eHDRMode GetHDRMode( Int32 monitorIdx, Bool fullscreen );

			//
			// [PC/PS4]  
			//
			RENDER_DATA_API void SetHDRMode( GpuWrapApi::eHDRMode, Int32 monitorIdx, Bool fullscreen );

#else
			//
			// [XBOX] Get InGameConfig "HDRModes" variable. Internally it is stored as list of "Enabled"/"Disabled" 
			//		  strings as scripts are using its index to determine whether HDR or SDR calibration screen 
			//		  needs to be displayed.
			//
			RENDER_DATA_API Bool GetHDRMode();

			//
			// [XBOX] Update InGameConfig "HDRModes" variable to be either enabled or not. Internally it is
			//		  stored as list of "Enabled"/"Disabled" strings as scripts are using its index to determine
			//		  whether HDR or SDR calibration screen needs to be displayed.
			//
			RENDER_DATA_API void SetHDRMode( Bool hdrOn );
#endif
			//
			// Access calibration values. All are immediate floats so no internal updates are done.
			//
			RENDER_DATA_API Float GetTonemappingMidpoint();
			RENDER_DATA_API Float GetMaxMonitorBrightness( Int32 monitorIdx = -1 );
			RENDER_DATA_API Float GetPaperWhiteLevel();
			RENDER_DATA_API Float GetGamma();

			//
			// In case UserSettings.json is not there - pull max brightness from system call.
			// Otherwise, set to what was supplied.
			//
			RENDER_DATA_API void SetMaxMonitorBrightness( Float maxMonitorBrightness, Int32 monitorIdx );

			//
			// Set InGameConfig "TonemappingMidpoint" variable.
			//
			RENDER_DATA_API void SetTonemappingMidpoint( Float tonemappingMidpoint );

			//
			// Set InGameConfig "PaperWhiteLevel" variable.
			//
			RENDER_DATA_API void SetPaperWhiteLevel( Float paperWhiteLevel );

			//
			// Set InGameConfig "Gamma" variable.
			//
			RENDER_DATA_API void SetGamma( Float gamma );
		}

		namespace graphics_tab
		{
#ifdef RED_PLATFORM_WINPC 
			//
			// [PC] 
			//
			RENDER_DATA_API void SetQuickPreset( Config::GraphicsQualityLevel qualityLevel );

			//
			// [PC] 
			//
			RENDER_DATA_API Config::GraphicsQualityLevel GetQuickPreset();

			//
			// [PC] 
			//
			RENDER_DATA_API void SetDLSS( Bool enabled, Int32 dlssQualityPresetIdx, InGameConfig::Source source );

			//
			// [PC] 
			//
			RENDER_DATA_API void SetDRS( Bool enabled );

			//
			// [PC] 
			//
			RENDER_DATA_API void SetSRS( Bool enabled );

			//
			// [PC] 
			//
			RENDER_DATA_API Bool GetDLSSEnabled();

			//
			// [PC] 
			//
			RENDER_DATA_API Int32 GetDLSSQuality();

			//
			// [PC] 
			//
			RENDER_DATA_API Bool GetDRSEnabled();

			//
			// [PC] 
			//
			RENDER_DATA_API Uint32 GetDRSMaximalResolutionPercentage();

			//
			// [PC] 
			//
			RENDER_DATA_API Uint32 GetDRSMinimalResolutionPercentage();

			//
			// [PC] 
			//
			RENDER_DATA_API Bool GetSRSEnabled();

			//
			// [PC] 
			//
			RENDER_DATA_API Uint32 GetSRSResolutionPercentage();

			//
			// [PC] 
			//
			RENDER_DATA_API Uint32 GetDRSTargetFPS();
			
#endif
		}


		//
		// This set of enums and functions was introduced in order to unify exchanging data between
		// in game settings and code variables. Many times it happens that single in game variable
		// corresponds to multiple code variables and we need to map from or map to GGlobalRenderingSettings
		// and it all was spread around different parts of the code. Ideally I would like to introduce
		// another header for these and move all in game options here but I do not have time right now.
		//

		enum class InGameEntry
		{
			LocalShadowsQuality,
			MaxDynamicDecals,
			VolumetricCloudsQuality,
		};

		enum class InGameOption
		{
			Off,
			Low,
			Medium,
			High,
			Ultra,
			Invalid
		};

		//
		// Investigate provided GlobalRenderingSettings for given InGameEntry and decide  to which InGameOption 
		// it corresponds to. In case where there is no match InGameOption::Invalid is returned.
		//
		template< InGameEntry> InGameOption Get( const Config::GlobalRenderingSettings& );

		//
		// Check if provided GlobalRenderingSettings for given InGameEntry is set to supplied InGameOption.
		//
		template< InGameEntry ige, InGameOption igo > Bool Is( const Config::GlobalRenderingSettings& grs ) { return Get< ige >( grs ) == igo; }

		//
		// Update GlobalRenderingSettings's entries corresponding to InGameEntry with set of values coupled with InGameOption.
		//
		template< InGameEntry, InGameOption > void Set( Config::GlobalRenderingSettings& grs );
	}
}