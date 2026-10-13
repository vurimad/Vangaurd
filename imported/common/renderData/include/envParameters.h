/**
* Copyright (c) 2013 CD Projekt Red. All Rights Reserved.
*/
#pragma once
#include "windParameters.h"
#include "renderResource.h"
#include "cameraDofParams.h"
#include "tonemappingModes.h"
#include "worldShadowConfig.h"

#include "../../../../engine/shaders/include/include_heatHaze.fx"

//******************************************************************************************************//
//******************************************************************************************************//

/// Time-based blending state
enum EEnvManagerTimeBlendState
{
	ETBS_Disabled = 0,		/* No time-based blending */
	ETBS_BlendIn,			/* Blending in */
	ETBS_BlendOut			/* Blending out */
};

//******************************************************************************************************//
//******************************************************************************************************//

/// Global lighting
class RENDER_DATA_API CRenderBaseLightParams
{
public:
	Vector4				m_lightDirection;						//!< Direction of global light
	Vector4				m_sunDirection;							//!< Direction of sun
	Vector4				m_moonDirection;						//!< Direction of moon
	Vector4				m_upVector;
	Vector4				m_skyDirection;
	HDRColor			m_lightColor;							//!< Color of global light
	HDRColor			m_sunColor;								//!< Sun light diffuse
	HDRColor			m_moonColor;							//!< Moon light diffuse
	HDRColor			m_specularTint;							//!< Moon light diffuse
	Float				m_sunSize;								//!< Size of sunlight
	Float				m_moonSize;								//!< Size of moonlight
	Float				m_timeOfDay;							
	Bool				m_hdrAdaptationDisabled;				//!< HDR disabled

public:
	CRenderBaseLightParams();
	~CRenderBaseLightParams();
};

//******************************************************************************************************//
//******************************************************************************************************//

struct RENDER_DATA_API DayPointEnvironmentParams
{
	WindParameters						m_windParameters;

	TRenderPtr<IRenderResource>	m_cloudsShadowTexture;
	TRenderPtr<IRenderResource>	m_fakeCloudsShadowTexture;
	Float								m_fakeCloudsShadowSize;
	Float								m_fakeCloudsShadowSpeed;
	Float								m_fakeCloudsShadowCurrentTextureIndex;
	Float								m_fakeCloudsShadowTargetTextureIndex;

	TRenderPtr<IRenderResource>	m_vignetteTexture;
	TRenderPtr<IRenderResource>	m_bokehTexture;
	TRenderPtr<IRenderResource>	m_cameraDirtTexture;
	Float								m_cameraDirtNumVerticalTiles;
	TRenderPtr<IRenderResource>	m_wetSurfaceTexture;
	TRenderPtr<IRenderResource>	m_forcedEnvProbeDataSource;
	Matrix								m_forcedEnvProbeRotation;

	Vector4								m_skyBoxWeatherBlend;
	Float								m_delayedWetSurfaceEffectStrength;
	Float								m_immediateWetSurfaceEffectStrength;
	Float								m_weatherEffectStrength;

	Float								m_gamma;
	Float								m_skyDayAmount;

	Float								m_nearPlaneWeightForced;

	Bool								m_useMoonForShafts;

	SBokehDofParams						m_bokehDofParams;

public:
	DayPointEnvironmentParams();

	/// Reset defaults
	void Reset();
};

//******************************************************************************************************//
//******************************************************************************************************//

struct RENDER_DATA_API CameraSettings
{
	Float							m_cameraNearPlane;
	Float							m_cameraFarPlane;

	Float							m_automated;
	Uint32							m_ISO;
	Float							m_shutterTime;
	Float							m_fStop;

	CameraSettings();

	Float ComputeCameraExposure() const;

private:
	Float ComputeEV100() const;
};

//******************************************************************************************************//
//******************************************************************************************************//

struct RENDER_DATA_API AtmosphereSettings
{
	HDRColor							m_skydomeColor;
	HDRColor							m_skylightColor;
	HDRColor							m_groundReflectance;
	Float								m_horizonMinZ;
	Float								m_sunMinZ;
	Float								m_horizonFalloff;
	Float								m_turbidity;
	Float								m_lutTurbidity;
	HDRColor							m_artisticDarkeningColor;		// For sky darkening on the side opposite to the sun.
	Float								m_artisticDarkeningStartPoint;
	Float								m_artisticDarkeningEndPoint;
	Float								m_artisticDarkeningExponent;

	HDRColor							m_sunColor;
	Float								m_sunFalloff;
	Float								m_rayTracedSunShadowRange;

	HDRColor							m_moonColor;
	Float								m_moonFalloff;	
	Float								m_moonGlowIntensity;
	Float								m_moonGlowFalloff;
	TRenderPtr<IRenderResource>			m_moonTexture;
	
	Float								m_galaxyIntensity;
	Float								m_starMapIntensity;
	Float								m_starMapBrightScale;
	Float								m_starMapDimScale;
	Float								m_starMapConstelationsScale;
	Float								m_starCornerLuminanceFix;
	TRenderPtr<IRenderResource>			m_starMapTexture;
	TRenderPtr<IRenderResource>			m_galaxyTexture;

	HDRColor							m_probeColorOverrideHorizon;
	Float								m_probeAlphaOverrideHorizon;
	HDRColor							m_probeColorOverrideZenith;
	Float								m_probeAlphaOverrideZenith;

	Float								m_cloudSunShadowFaloff;
	Float								m_cloudSunScattering;
	Float								m_cloudMoonScattering;
	Float								m_cloudCirrusOpacity;
	Float								m_cloudAmbientIntensity;
	Float								m_cloudCirrusScale;
	Float								m_cloudCirrusAltitude;

	TRenderPtr<IRenderResource>			m_cloudCirrusTexture;

	TRenderPtr<IRenderResource>			m_volWeatherTexture;
	TRenderPtr<IRenderResource>			m_volNoiseTexture;
	Float								m_volCoverage;
	Float								m_volHorizonCoverage;
	Float								m_volDensity;
	Float								m_volDistantFogOpacity;

	Float								m_cloudsBottom;
	Float								m_cloudsTop;

	Float								m_rayStartOffset;
	Float								m_rayStartFalloff;

	Float								m_lightIntensity;
	Float								m_reflectionProbeLightIntensity;
	Float								m_shadowIntensity;
	Float								m_worldShadowIntensity;
	Float								m_ambientIntensity;

	Float								m_inScatter;
	Float								m_outScatter;
	Float								m_inVsOutScatter;
	Float								m_silverLining;
	Float								m_ambientOutscatter;

	Float								m_volCoverageWindInfluence;
	Float								m_volNoiseWindInfluence;
	Float								m_volDetailWindInfluence;
	
	Vector2								m_volCoverageOffset;
	Vector2								m_volNoiseOffset;
	Vector2								m_volDetailOffset;

	AtmosphereSettings();
};

//******************************************************************************************************//
//******************************************************************************************************//

struct RENDER_DATA_API ExposureSettings
{
	Float								m_exposureAdaptationSpeedUp;
	Float								m_exposureAdaptationSpeedDown;
	Float								m_exposurePercentageThresholdLow;
	Float								m_exposurePercentageThresholdHigh;
	Float								m_exposureCompensation;
	Float								m_exposureSkyImpact;
	Float								m_exposureMin;
	Float								m_exposureMax;
	Float								m_exposureCenterImportance;
	Float								m_cameraVelocityFaloff;

	ExposureSettings();
};

//******************************************************************************************************//
//******************************************************************************************************//

struct RENDER_DATA_API TonemappingSettings
{
	THandle< ITonemappingMode > m_mode;

	TonemappingSettings();
};

//******************************************************************************************************//
//******************************************************************************************************//

struct RENDER_DATA_API LutBlendState
{
	TRenderPtr<IRenderResource> m_lut;
	Float m_weight;
	EColorMappingFunction m_inputColorMapping;
	EColorMappingFunction m_outputColorMapping;

	LutBlendState();
};

struct RENDER_DATA_API ColorGradingResolvedLutParams
{
	TRenderPtr<IRenderResource>		m_lut;
	EColorMappingFunction			m_inputMapping;
	EColorMappingFunction			m_outputMapping;

	ColorGradingResolvedLutParams();
};

struct RENDER_DATA_API ColorGradingSettings
{
	Float			m_contrast;
	Float			m_contrastPivot;
	Float			m_saturation;
	Float			m_hue;
	Float			m_brightness;
	Float			m_lowRange;
	Float			m_highRange;
	ColorBalance	m_lift;
	ColorBalance	m_gammaValue;
	ColorBalance	m_gain;
	ColorBalance	m_offset;
	ColorBalance	m_shadowOffset;
	ColorBalance	m_midtoneOffset;
	ColorBalance	m_highlightOffset;

	red::DynArray< LutBlendState > m_lutBlendStates{ rend::PoolEnvironment() };

	ColorGradingSettings();
};

//******************************************************************************************************//
//******************************************************************************************************//

struct RENDER_DATA_API FilmGrainSettings
{
	Vector4				m_strength;
	Vector3				m_grainSize;
	Float				m_luminanceBias;
	Bool				m_applyAfterUpsampling;

	FilmGrainSettings();
};

//******************************************************************************************************//
//******************************************************************************************************//

struct RENDER_DATA_API SSAOSettings
{
	// This is necessary to filter out pixel shimmer due to bilateral upsampling with too much lost resolution.  High
	// frequency detail can sometimes not be reconstructed, and the noise filter fills in the missing pixels with the
	// result of the higher resolution SSAO.
	Float	m_noiseFilterTolerance;
	Float	m_blurTolerance;
	Float	m_upsampleTolerance;

	// Controls how aggressive to fade off samples that occlude spheres but by so much as to be unreliable. This is what
	// gives objects a dark halo around them when placed in front of a wall.  If you want to fade off the halo, boost
	// your rejection falloff.  The tradeoff is that it reduces overall AO.
	Float	m_rejectionFalloff;

	// The higher quality modes blend wide and narrow sampling patterns.  The wide pattern is due to deinterleaving and
	// requires blurring.  The narrow pattern is not on a deinterleaved buffer, but it only samples every other pixel.
	// The blur on it is optional.  If you combine the two before blurring, the narrow will get blurred as well.  This
	// creates a softer effect but can remove any visible noise from having 50% sample coverage.
	Bool	m_combineResolutionsBeforeBlur;

	// When combining the wide and narrow patterns, a mul() operation can be used or a min() operation.  Multiplication
	// exaggerates the result creating even darker creases.  This is an artistic choice.  I think it looks less natural,
	// but often art teams prefer more exaggerated contrast.  For me, it's more about having the right AO falloff so
	// that it's a smooth gradient rather than falling off precipitously and forming overly dark recesses.
	Bool	m_combineResolutionsWithMul;

	Bool	m_normalsEnable;
	Int32	m_hierarchyDepth;
	Float	m_normalAOMultiply;
	Float	m_normalBackProjectTolerance;

	// High quality (and better) is barely a noticeable improvement when modulated properly with ambient light.
	// However, in the debug view the quality improvement is very apparent.
	// enum QualityLevel { kSsaoQualityVeryLow, kSsaoQualityLow, kSsaoQualityMedium, kSsaoQualityHigh, kSsaoQualityVeryHigh, kNumSsaoQualitySettings };
	ESSAOQualityLevel m_qualityLevel;

	// Cone ao settings
	Float m_coneAoDiffuseStrength;
	Float m_coneAoSpecularStrength;
	Float m_coneAoSpecularTreshold;

	Float m_lightAoDiffuseStrength;
	Float m_lightAoSpecularStrength;

	Float m_foliageDimDiffuse;
	Float m_foliageDimSpecular;

	SSAOSettings();
};

//******************************************************************************************************//
//******************************************************************************************************//

struct RENDER_DATA_API RTAOSettings
{
	// affects how RTAO is calculated
	Float m_RangeNear;
	Float m_RangeFar;
	Float m_RadiusNear;
	Float m_RadiusFar;

	// Cone ao settings
	Float m_coneAoDiffuseStrength;
	Float m_coneAoSpecularStrength;
	Float m_coneAoSpecularTreshold;

	Float m_lightAoDiffuseStrength;
	Float m_lightAoSpecularStrength;

	RTAOSettings();
};

//******************************************************************************************************//
//******************************************************************************************************//

struct RENDER_DATA_API ShaftsSettings
{
	Uint32								m_shaftsLevelIndex;
	Float								m_shaftsIntensity;
	Float								m_shaftsThresholdsScale;

	ShaftsSettings();
};

//******************************************************************************************************//
//******************************************************************************************************//

struct RENDER_DATA_API BloomSettings
{
	Float								m_luminanceThresholdMin;
	Float								m_luminanceThresholdMax;
	Float								m_sceneColorScale;
	Float								m_bloomColorScale;
	Uint8								m_numDownsamplePasses;

	ShaftsSettings						m_shaftsSettings;

	BloomSettings();
};

//******************************************************************************************************//
//******************************************************************************************************//

struct RENDER_DATA_API ImageBasedFlaresSettings
{
	Float							m_treshold;
	Float							m_dispersal;
	Float							m_haloWidth;
	Float							m_distortion;
	Float							m_scale;
	Float							m_saturation;
	Float							m_curve;
	Color							m_tint[8];

	ImageBasedFlaresSettings();
};

//******************************************************************************************************//
//******************************************************************************************************//

struct RENDER_DATA_API BlursSettings
{
	Float								m_motionBlurStrength;
	Float								m_sharpeningStrength;
	Float								m_circularBlurRadius;

	BlursSettings();
};

//******************************************************************************************************//
//******************************************************************************************************//

struct RENDER_DATA_API VolumetricFogSettings
{
	Float								m_range;
	Float								m_localLightRange;
	Float								m_fogHeight;
	Float								m_fogHeightFalloff;
	Float								m_fogHeightMaxCut;
	Float								m_density;
	Float								m_absorption;
	Float								m_globalLightAnisotropy;
	Float								m_globalLightAnisotropyBase;
	Float								m_globalLightAnisotropyScale;
	HDRColor							m_albedo;
	HDRColor							m_distantAlbedo;
	Float								m_ambientScale;
	Float								m_localAmbientScale;
	Float								m_globalLightScale;
	Float								m_localLightScale;
	Float								m_noiseScale;
	Float								m_noiseThreshold;
	Vector3								m_noiseVelocity;

	Float								m_distantGroundIrradiance;
	Float								m_distantSkyIrradiance;
	Float								m_distantGroundSaturation;
	Float								m_distantGlobalLightScale;
	Float								m_distantShadowAmbientDarkening;

	VolumetricFogSettings();
};

//******************************************************************************************************//
//******************************************************************************************************//

struct RENDER_DATA_API DistantFogSettings
{
	Float								m_density;
	Float								m_height;
	Float								m_heightFallof;

	Float								m_densitySecond;
	Float								m_heightSecond;
	Float								m_heightFallofSecond;

	Float								m_range;
	HDRColor							m_albedoNear;
	HDRColor							m_albedoFar;
	Float								m_nearDistance;
	Float								m_farDistance;

	HDRColor							m_simpleColor;
	Float								m_simpleDensity;

	HDRColor							m_envProbeColor;
	Float								m_envProbeDensity;

	DistantFogSettings();
};

//******************************************************************************************************//
//******************************************************************************************************//

struct RENDER_DATA_API GISettings
{
	Float								m_multiBouceScale;
	Float								m_multiBouceSaturation;
	Float								m_diffuseScale;
	Float								m_emissiveScale;
	Float								m_localLightsScale;
	Float								m_lightScaleCompenensation;
	Float								m_reflectionCompensation;
	HDRColor							m_ambientBase;
	Float								m_rayTracedSkyRadianceScale;

	GISettings();
};

struct RENDER_DATA_API DistantGISettings
{
	Vector2								m_distantRange;
	Vector3								m_distantHeightRange;
	Float								m_distantLights;
	Vector2								m_distantLightsRange;
	Float								m_blendDistance;

	DistantGISettings();
};


//******************************************************************************************************//
//******************************************************************************************************//

struct RENDER_DATA_API ChromaticAberrationSettings
{
	Bool								m_chromaticAberrationEnabled;
	Float								m_chromaticAberrationMargin;
	Float								m_chromaticAberrationOffset;
	Float								m_chromaticAberrationExp;
	Float								m_subpixelDispersal;

	ChromaticAberrationSettings();
};

//******************************************************************************************************//
//******************************************************************************************************//

struct RENDER_DATA_API VignetteSettings
{
	Bool								m_vignetteEnabled;
	Float								m_vignetteRadius;
	Float								m_vignetteExp;
	Color								m_vignetteColor;

	VignetteSettings();
};

//******************************************************************************************************//
//******************************************************************************************************//

struct RENDER_DATA_API FoliageSettings
{
	Float					m_foliageShadowCascadeGradient;
	Float					m_foliageShadowCascadeFilterScale;
	Float					m_foliageShadowCascadeGradientDistanceRange;

	FoliageSettings();
};

//******************************************************************************************************//
//******************************************************************************************************//

struct RENDER_DATA_API DistantLightsSettings
{
	Float								m_distantLightStartDistance;
	Float								m_distantLightFadeDistance;
	Float								m_groupFade[8];

	DistantLightsSettings();
};

//******************************************************************************************************//
//******************************************************************************************************//

struct RENDER_DATA_API ShadowCascadesConfig
{
	ShadowCascadeConfig m_cascade[MAX_CASCADES];

	ShadowCascadesConfig();
};

//******************************************************************************************************//
//******************************************************************************************************//

struct RENDER_DATA_API RainSettings
{
	Uint32			m_numParticles;
	Float			m_radius;
	Float			m_heightRange;
	Float			m_globalLightResponse;

	Float			m_tiling;
	Float			m_speed;

	Float			m_roughnessShift;
	Float			m_roughnessClip;
	Float			m_roughnessExponent;

	Float			m_baseColorShift;
	Float			m_baseColorClip;
	Float			m_baseColorExponent;
	
	Float			m_porosityThreshold;
	
	Float			m_moistureAccumulationSpeed;
	Float			m_puddlesAccumulationSpeed;
	Float			m_moistureEvaporationSpeed;
	Float			m_puddlesEvaporationSpeed;
	Float			m_rainIntensity;
	
	Float			m_rainOverride;
	Float			m_rainMoistureOverride;
	Float			m_rainPuddlesOverride;

	Float			m_rainMoisture;
	Float			m_rainPuddles;

	TRenderPtr<IRenderResource>					m_rainleaksMask;
	TRenderPtr<IRenderResource>					m_raindropsMask;
	TRenderPtr<IRenderResource>					m_rainRipplesMask;


	RainSettings();
};

//******************************************************************************************************//
//******************************************************************************************************//

struct RENDER_DATA_API HeatHazeSettings : public HeatHazeParameters
{
	HeatHazeSettings();
};

//******************************************************************************************************//
//******************************************************************************************************//

struct RENDER_DATA_API EmissiveSettings
{
	HDRColor		m_tint;
	Float			m_saturation;
	Float			m_brigtness;
	Vector2			m_exposure;
	Vector2			m_cameraLuminance;
	Float			m_evBlend;
	Float			m_exposureIBL;
	Float			m_luminanceIBL;
	Float			m_exposureScale;
	Float			m_curveRampIBL;

	EmissiveSettings();
};

//******************************************************************************************************//
//******************************************************************************************************//

struct RENDER_DATA_API DistantProxies
{
	Float			m_distantProxiesEmissive;
	Float			m_distantProxiesEmissiveHeight;
	Float			m_distantProxiesEmissivePower;
	Float			m_distantProxiesBboxzBlend;

	DistantProxies();
};

//******************************************************************************************************//
//******************************************************************************************************//

struct RENDER_DATA_API EmitterGroupParams
{
	RTTI_DECLARE_TYPE( EmitterGroupParams );
	EEmitterGroup	m_group;
	Float			m_emissionScale;
	Float			m_opacityScale;

	EmitterGroupParams();
};

struct RENDER_DATA_API EmitterGroupSettings
{
	red::StaticArray<EmitterGroupParams, 16u> m_emitterGroupParams;

	EmitterGroupSettings();
};

//******************************************************************************************************//
//******************************************************************************************************//

struct RENDER_DATA_API WindSettings
{
	Float			m_strength;
	Vector4			m_direction;

	WindSettings();
};

//******************************************************************************************************//
//******************************************************************************************************//

struct RENDER_DATA_API WaterSettings
{
	Float		m_blurMin;
	Float		m_blurMax;
	Float		m_blurExponent;
	Float		m_depth;
	Float		m_density;

	HDRColor	m_lightAbsorptionColor;
	HDRColor	m_lightDecayColor;
	
	TRenderPtr<IRenderResource>	m_globalWaterMask;


	WaterSettings();
};

//******************************************************************************************************//
//******************************************************************************************************//

enum ETimeOfYearSeason
{
	ETOYS_Spring,
	ETOYS_Summer,
	ETOYS_Autumn,
	ETOYS_Winter,
};

RTTI_DECLARE_ENUM( ETimeOfYearSeason );

struct RENDER_DATA_API LightSettings
{
	HDRColor						m_sunColor;
	HDRColor						m_moonColor;
	HDRColor						m_specularTint;
	ELightUnit						m_unit;
	Float							m_sunSize;
	Float							m_moonSize;

	Float							m_latitude;
	Float							m_sunRotationOffset;
	Float							m_moonRotationOffset;
	ETimeOfYearSeason				m_season;

	Float							m_lightBrighthnessClamp;

	LightSettings();
};

struct RENDER_DATA_API CustomLightSettings
{
	Float							m_characterLocalLightRoughnesBias;

	CustomLightSettings();
};

//******************************************************************************************************//
//******************************************************************************************************//

struct RENDER_DATA_API ColorGroupSettings
{
	HDRColor						m_skyTint;
	HDRColor						m_colorGroup[ECG_MAX- ECG_Group0];	// One less, bcoz ECG_Default is treated as no env color

	ColorGroupSettings();
};

//******************************************************************************************************//
//******************************************************************************************************//

// Note: this one is not used during rendering, only for overriding parameters
struct RENDER_DATA_API GlobalLightOverrideSettings
{
	Bool							m_enabled;
	Vector3							m_direction;
	HDRColor						m_color;

	GlobalLightOverrideSettings();
};

//******************************************************************************************************//
//******************************************************************************************************//

struct RENDER_DATA_API AmbientOverrideSettings
{
	Bool							m_enabled;
	HDRColor						m_color[6];

	AmbientOverrideSettings();
};

//******************************************************************************************************//
//******************************************************************************************************//

#if ( 1 || defined(RED_CONFIGURATION_FINAL) )
#	define ALLOW_HACK_AREA_SETTINGS 1
#else
#	define ALLOW_HACK_AREA_SETTINGS 0
#endif

struct RENDER_DATA_API HACK_ENV_Settings
{
#if ALLOW_HACK_AREA_SETTINGS
	Float								m_missingEnergyScale;
	Float								m_surfelScale;
	Float								m_skyScale;
	HDRColor							m_bottomHemisphereTint;
	Float								m_bottomHemisphereStrength;
	Float								m_emissiveScale;
	Bool								m_overrideOnPureView;

	Float								m_surfAlbedoOverrideRatio;
	HDRColor							m_surfAlbedoOverride;
#endif

	HACK_ENV_Settings();
};


//******************************************************************************************************//
//******************************************************************************************************//

/// World render settings
struct WorldRenderSettings
{
	Bool								m_isPreview;

	CameraSettings						m_camera;
	AtmosphereSettings					m_atmosphere;
	ExposureSettings					m_exposure;
	TonemappingSettings					m_tonemapping;
	ColorGradingSettings				m_colorGrading;
	FilmGrainSettings					m_filmGrain;
	SSAOSettings						m_ssao;
	RTAOSettings						m_rtao;
	BloomSettings						m_bloom;
	ImageBasedFlaresSettings			m_imageBasedFlares;
	BlursSettings						m_blurs;
	VolumetricFogSettings				m_volumetricFog;
	DistantFogSettings					m_distanceFog;
	GISettings							m_globalIllumination;
	DistantGISettings					m_distantIrradiance;
	ChromaticAberrationSettings			m_chromaticAberration;
	VignetteSettings					m_vignette;
	FoliageSettings						m_foliage;
	DistantLightsSettings				m_distantLights;
	ShadowCascadesConfig				m_shadows;
	ContactShadowsConfig				m_contactShadows;
	RainSettings						m_rain;
	WindSettings						m_wind;
	WaterSettings						m_water;
	HeatHazeSettings					m_heatHaze;
	LightSettings						m_lightSettings;
	CustomLightSettings					m_customLightSettings;
	ColorGroupSettings					m_colorGroups;
	EmissiveSettings					m_emissive;
	DistantProxies						m_distantProxies;
	EmitterGroupSettings				m_emitterGroupSettings;
	AmbientOverrideSettings				m_ambientOverride;
	GlobalLightOverrideSettings			m_globalLightOverride;

#if ALLOW_HACK_AREA_SETTINGS
	HACK_ENV_Settings					m_HACKS;
#endif

	WorldRenderSettings()
		: m_isPreview( true )
	{}
};

//******************************************************************************************************//
//******************************************************************************************************//
