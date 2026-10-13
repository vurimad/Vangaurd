#include "build.h"
#include "envParameters.h"
#include "renderSettingsVars.h"

//******************************************************************************************************//
//******************************************************************************************************//

CameraSettings::CameraSettings()
	: m_cameraNearPlane( 0.4f )
	, m_cameraFarPlane( 8000.f )
	, m_automated( 0.0f )
	, m_ISO( 100 )
	, m_shutterTime( 125.0f )
	, m_fStop( 8.0f )
{
}

Float CameraSettings::ComputeCameraExposure() const
{
	Float ev100 = ComputeEV100();
	Float maxLuminance = 1.2f * pow( 2.0f, ev100 );
	return Clamp( 1.0f / maxLuminance, 1e-5f, 1e5f );
}

Float CameraSettings::ComputeEV100() const
{
	// m_shutterTime is in 1/N, so we multiply it here
	return log2( (m_fStop * m_fStop) * m_shutterTime * 100.0f / (Float)m_ISO );
}

//******************************************************************************************************//
//******************************************************************************************************//

AtmosphereSettings::AtmosphereSettings()
	: m_skydomeColor( 1.0f, 1.0f, 1.0f, 100.0f )
	, m_skylightColor( 1.0f, 1.0f, 1.0f, 100.0f )
	, m_groundReflectance( 1.0f, 1.0f, 1.0f, 0.1f )
	, m_horizonMinZ( 0.05f )
	, m_sunMinZ( 0.0f )
	, m_horizonFalloff( 0.05f )
	, m_turbidity( 4.0f )
	, m_lutTurbidity( 1.0f )
	, m_artisticDarkeningColor( 1.0f, 1.0f, 1.0f, 1.0f )
	, m_artisticDarkeningStartPoint( 0.0f )
	, m_artisticDarkeningEndPoint( 1.0f )
	, m_artisticDarkeningExponent( 1.0f )
	, m_sunColor( 1.0f, 1.0f, 1.0f, 100.0f )
	, m_sunFalloff( 0.01f )
	, m_rayTracedSunShadowRange( 2000.0f )
	, m_moonColor( 1.0f, 1.0f, 1.0f )
	, m_moonGlowIntensity( 0.05f )
	, m_moonGlowFalloff( 10.0f )
	, m_galaxyIntensity( 0.3f )
	, m_starMapIntensity( 5.0f )
	, m_starMapBrightScale( 10.5f )
	, m_starMapDimScale( 25.0f )
	, m_starMapConstelationsScale( 10.0f )
	, m_starCornerLuminanceFix( 0.25f )
	, m_moonFalloff( 0.01f )
	, m_cloudSunShadowFaloff( 1.0f )
	, m_cloudSunScattering( 0.9f )
	, m_cloudMoonScattering( 0.85f )
	, m_cloudCirrusOpacity( 1.0f )
	, m_cloudAmbientIntensity( 1.0f )
	, m_cloudCirrusScale( 5.0f )
	, m_cloudCirrusAltitude( 1.0f )
	, m_volCoverage( 0.5f )
	, m_volHorizonCoverage( 1.f )
	, m_volDensity( 1.0f )
	, m_cloudsBottom( 400.0f )
	, m_cloudsTop( 1000.0f )
	, m_rayStartOffset( 25.0f )
	, m_rayStartFalloff( 50.0f )
	, m_lightIntensity( 1.0f )
	, m_reflectionProbeLightIntensity( 0.25f )
	, m_shadowIntensity( 1.0f )
	, m_worldShadowIntensity( 1.0f )
	, m_ambientIntensity( 1.0f )
	, m_inScatter( 0.2f )
	, m_outScatter( 0.1f )
	, m_inVsOutScatter( 0.5f )
	, m_silverLining( 0.8f )
	, m_ambientOutscatter( 0.9f )
	, m_volCoverageWindInfluence( 0.75f )
	, m_volNoiseWindInfluence( 1.0f )
	, m_volDetailWindInfluence( 1.25f )
	, m_volDistantFogOpacity( 1.0f )
	, m_probeColorOverrideHorizon( HDRColor::BLACK() )
	, m_probeAlphaOverrideHorizon( 0.0f )
	, m_probeColorOverrideZenith( HDRColor::BLACK() )
	, m_probeAlphaOverrideZenith( 0.0f )

{
}

//******************************************************************************************************//
//******************************************************************************************************//

ExposureSettings::ExposureSettings()
	: m_exposureAdaptationSpeedUp( 8.f )
	, m_exposureAdaptationSpeedDown( 5.f )
	, m_exposurePercentageThresholdLow( 0.05f )
	, m_exposurePercentageThresholdHigh( 0.3f )
	, m_exposureCompensation( 0.0f )
	, m_exposureSkyImpact( 1.0f )
	, m_exposureMin( -8.0f )
	, m_exposureMax( 30.0f )
	, m_exposureCenterImportance( 0.0f )
	, m_cameraVelocityFaloff( 0.0f )
{
}

//******************************************************************************************************//
//******************************************************************************************************//

TonemappingSettings::TonemappingSettings()
{
}

//******************************************************************************************************//
//******************************************************************************************************//

ColorGradingResolvedLutParams::ColorGradingResolvedLutParams()
	: m_inputMapping( CMF_sRGB )
	, m_outputMapping( CMF_sRGB )
{
}

LutBlendState::LutBlendState()
	: m_weight( 1.0f )
	, m_inputColorMapping( EColorMappingFunction::CMF_MAX )
	, m_outputColorMapping( EColorMappingFunction::CMF_MAX )
{
}

ColorGradingSettings::ColorGradingSettings()
	: m_contrast( 1.0f )
	, m_contrastPivot( 0.435f )
	, m_saturation( 1.0f )
	, m_hue( 0.0f )
	, m_brightness( 1.0f )
	, m_lowRange( 0.1f )
	, m_highRange( 0.45f )
	, m_lift( 0.0f, 0.0f, 0.0f, 0.0f )
	, m_gammaValue( 1.0f, 1.0f, 1.0f, 1.0f )
	, m_gain( 1.0f, 1.0f, 1.0f, 1.0f )
	, m_offset( 0.0f, 0.0f, 0.0f, 0.0f )
	, m_shadowOffset( 0.0f, 0.0f, 0.0f, 0.0f )
	, m_midtoneOffset( 0.0f, 0.0f, 0.0f, 0.0f )
	, m_highlightOffset( 0.0f, 0.0f, 0.0f, 0.0f )
{
}

//******************************************************************************************************//
//******************************************************************************************************//

FilmGrainSettings::FilmGrainSettings()
	: m_strength( 0.0f, 0.0f, 0.0f )
	, m_grainSize( 0.5f, 0.5f, 0.5f )
	, m_luminanceBias( 0.8f )
	, m_applyAfterUpsampling( false )
{
}

//******************************************************************************************************//
//******************************************************************************************************//

SSAOSettings::SSAOSettings()
	: m_noiseFilterTolerance( -4.23f )
	, m_blurTolerance( -1.47f )
	, m_upsampleTolerance( -6.34f )
	, m_rejectionFalloff( 4.78f )
	, m_combineResolutionsBeforeBlur( true )
	, m_combineResolutionsWithMul( true )
	, m_normalsEnable( true )
	, m_hierarchyDepth( 3 )
	, m_normalAOMultiply( 1.5f )
	, m_normalBackProjectTolerance( 10.0f )
	, m_qualityLevel( ESSAOQualityLevel::SSAOQUALITY_VeryHigh )
	, m_coneAoDiffuseStrength( 0.75f )
	, m_coneAoSpecularStrength( 0.75f )
	, m_coneAoSpecularTreshold( 0.0f )
	, m_lightAoDiffuseStrength( 1.0f )
	, m_lightAoSpecularStrength( 0.75f )
	, m_foliageDimDiffuse( 0.75f )
	, m_foliageDimSpecular( 0.25f )
{
}


//******************************************************************************************************//
//******************************************************************************************************//

RTAOSettings::RTAOSettings()
	: m_RangeNear(50.0f)
	, m_RangeFar(250.0f)
	, m_RadiusNear(10.0f)
	, m_RadiusFar(200.0f)
	// SSAO:
	, m_coneAoDiffuseStrength(0.75f)
	, m_coneAoSpecularStrength(0.75f)
	, m_coneAoSpecularTreshold(0.0f)
	, m_lightAoDiffuseStrength(1.0f)
	, m_lightAoSpecularStrength(0.75f)
{
}

//******************************************************************************************************//
//******************************************************************************************************//

ShaftsSettings::ShaftsSettings()
	: m_shaftsLevelIndex( 1 )
	, m_shaftsIntensity( 1 )
	, m_shaftsThresholdsScale( 1 )
{}

//******************************************************************************************************//
//******************************************************************************************************//

BloomSettings::BloomSettings()
	: m_luminanceThresholdMin( 0.0f )
	, m_luminanceThresholdMax( 2.0f )
	, m_sceneColorScale( 0.8f )
	, m_bloomColorScale( 0.2f )
	, m_numDownsamplePasses( 6 )
{
}

//******************************************************************************************************//
//******************************************************************************************************//

ImageBasedFlaresSettings::ImageBasedFlaresSettings()
	: m_dispersal( 0.3f )
	, m_treshold( 2.0f )
	, m_haloWidth( .55f )
	, m_distortion( 1.0f )
	, m_scale( 0.25f )
	, m_saturation( 1.0f )
	, m_curve( 1.0f )
{
	for( auto& c : m_tint )
	{
		c = Color::WHITE();
	}
}

//******************************************************************************************************//
//******************************************************************************************************//

BlursSettings::BlursSettings()
	: m_motionBlurStrength( 0.2f )		// Small strength for now in order to run this in editor and not piss off anyone
	, m_sharpeningStrength( 0.0f )
	, m_circularBlurRadius( 0.0f )
{}

//******************************************************************************************************//
//******************************************************************************************************//

VolumetricFogSettings::VolumetricFogSettings()
	: m_density( 0.005f )
	, m_range( 250.0f )
	, m_localLightRange( 250.0f )
	, m_absorption( 1.0f )
	, m_globalLightAnisotropy( 0.9f )
	, m_globalLightAnisotropyBase( 0.0f )
	, m_globalLightAnisotropyScale( 1.0f )
	, m_albedo( HDRColor( 1.0f, 1.0f, 0.9f, 1.0f ) )
	, m_distantAlbedo( HDRColor( 1.0f, 1.0f, 0.9f, 0.0f ) )
	, m_ambientScale( 1.f )
	, m_localAmbientScale( 1.0f )
	, m_globalLightScale( 1.f )
	, m_localLightScale( 1.f )
	, m_fogHeight( 0.0f )
	, m_fogHeightFalloff( 0.01f )
	, m_fogHeightMaxCut( 10.0f )
	, m_noiseScale( 1.0f )
	, m_noiseThreshold( 0.0f )
	, m_noiseVelocity( Vector3(0,0,0) )
	, m_distantGroundIrradiance( 1.0f )
	, m_distantSkyIrradiance( 1.0f )
	, m_distantGroundSaturation( 1.0f )
	, m_distantGlobalLightScale( -1.0f )
	, m_distantShadowAmbientDarkening( 1.0f )
{}

//******************************************************************************************************//
//******************************************************************************************************//

DistantFogSettings::DistantFogSettings()
	: m_density( 6.0f )
	, m_heightFallof( 5.0f )
	, m_height( 0.0f )
	, m_densitySecond( 1.0f )
	, m_heightFallofSecond( 20.0f )
	, m_heightSecond( 0.0f )
	, m_range( 0.0f )
	, m_albedoNear( HDRColor( 1.0f, 1.0f, 0.9f, 1.0f ) )
	, m_albedoFar( HDRColor( 1.0f, 1.0f, 0.9f, 1.0f ) )
	, m_nearDistance( 128.0 )
	, m_farDistance( 2000.0f )
	, m_simpleColor( HDRColor::GRAY() )
	, m_simpleDensity( 0.0f )
	, m_envProbeColor( HDRColor::GRAY() )
	, m_envProbeDensity( 0.0f )
{}

//******************************************************************************************************//
//******************************************************************************************************//

GISettings::GISettings()
	: m_multiBouceScale( 1 )
	, m_multiBouceSaturation( 1 )
	, m_diffuseScale( 1 )
	, m_emissiveScale( 1 )
	, m_localLightsScale( 1 )
	, m_reflectionCompensation( 0.0f )
	, m_lightScaleCompenensation( 0.05f )
	, m_ambientBase( 0, 0, 0 )
	, m_rayTracedSkyRadianceScale( 1.0f )
{}

DistantGISettings::DistantGISettings()
	: m_distantRange( 80.0f, 150.0f )
	, m_distantHeightRange( 10, 150.0f, 1.0f )
	, m_distantLights( 1.0f )
	, m_distantLightsRange( 300, 600 )
	, m_blendDistance( 8.0f )
{}

//******************************************************************************************************//
//******************************************************************************************************//

FoliageSettings::FoliageSettings()
	: m_foliageShadowCascadeGradient( 1.0f )
	, m_foliageShadowCascadeFilterScale( 1.0f )
	, m_foliageShadowCascadeGradientDistanceRange( 30.0f )
{}

//******************************************************************************************************//
//******************************************************************************************************//

ChromaticAberrationSettings::ChromaticAberrationSettings()
	: m_chromaticAberrationEnabled( false )
	, m_chromaticAberrationOffset( 1.4f )
	, m_chromaticAberrationExp( 0.8f )
	, m_subpixelDispersal( 0.0f )
	, m_chromaticAberrationMargin( 0.0f )
{
}

//******************************************************************************************************//
//******************************************************************************************************//

VignetteSettings::VignetteSettings()
	: m_vignetteEnabled( false )
	, m_vignetteRadius( 0.5f )
	, m_vignetteExp( 2.0f )
	, m_vignetteColor( 0, 0, 0, 255 )
{
}

//******************************************************************************************************//
//******************************************************************************************************//

DistantLightsSettings::DistantLightsSettings()
	: m_distantLightStartDistance( 50.0f )
	, m_distantLightFadeDistance( 15.0f )
	, m_groupFade{1,1,1,1,1,1,1,1}
{
}

//******************************************************************************************************//
//******************************************************************************************************//

ShadowCascadesConfig::ShadowCascadesConfig()
{
	m_cascade[ 0 ] = { Config::cvCascadeRange[0].Get(),	0.01f,	0.15f,	Config::cvCascadeDepthBias[0].Get()	};
	m_cascade[ 1 ] = { Config::cvCascadeRange[1].Get(),	0.05f,	0.15f,	Config::cvCascadeDepthBias[1].Get()	};
	m_cascade[ 2 ] = { Config::cvCascadeRange[2].Get(),	0.1f,	0.15f,	Config::cvCascadeDepthBias[2].Get()	};
	m_cascade[ 3 ] = { Config::cvCascadeRange[3].Get(),	0.2f,	0.15f,	Config::cvCascadeDepthBias[3].Get()	};
}

//******************************************************************************************************//
//******************************************************************************************************//

RainSettings::RainSettings()
	: m_numParticles( 0 )
	, m_radius( 45.f )
	, m_heightRange( 40.f )
	, m_globalLightResponse( 1.f )
	, m_tiling( 1.f )
	, m_speed( 1.f )
	, m_roughnessShift( 0.0f )
	, m_roughnessClip( 0.0f )
	, m_roughnessExponent( 5.0f )
	, m_baseColorShift(0.0f)
	, m_baseColorClip(0.0f)
	, m_baseColorExponent(5.0f)
	, m_porosityThreshold(3.0f)
	, m_moistureAccumulationSpeed( 5.0f )
	, m_puddlesAccumulationSpeed( 1.5f )
	, m_moistureEvaporationSpeed( 2.5f )
	, m_puddlesEvaporationSpeed( 0.25f )
	, m_rainIntensity( 0.f )
	, m_rainMoisture( 0.f )
	, m_rainPuddles( 0.f )
	, m_rainOverride( 0.0f )
	, m_rainMoistureOverride( 0.0f )
	, m_rainPuddlesOverride( 0.0f )
{
}

//******************************************************************************************************//
//******************************************************************************************************//

WindSettings::WindSettings()
	: m_strength(0.1f)
	, m_direction( Vector3(1.0f, 0.0f, 0.0f) )
{
}

//******************************************************************************************************//
//******************************************************************************************************//

WaterSettings::WaterSettings()
	: m_blurMin( 0.0f )
	, m_blurMax( 10.0f )
	, m_blurExponent( 1.0f )
	, m_density( 10.0f )
	, m_depth( 50.0f )
	, m_lightAbsorptionColor( HDRColor( 0.1f, 0.5f, 1.0f, 1.0f ) )
	, m_lightDecayColor( HDRColor( 0.1f, 0.13f, 0.13f, 1.0f ) )
{
}

//******************************************************************************************************//
//******************************************************************************************************//

/* -------- LIGHT SETTINGS --------- */

RTTI_BEGIN_ENUM( ETimeOfYearSeason );
{
	RTTI_ENUM_OPTION( ETOYS_Spring );
	RTTI_ENUM_OPTION( ETOYS_Summer );
	RTTI_ENUM_OPTION( ETOYS_Autumn );
	RTTI_ENUM_OPTION( ETOYS_Winter );
}
RTTI_END_ENUM();

LightSettings::LightSettings()
	: m_unit( LU_Lumen )
	, m_sunColor( HDRColor( 1.0f, 1.0f, 1.0f, 1.0f ) )
	, m_moonColor( HDRColor( 1.0f, 1.0f, 1.0f, 1.0f ) )
	, m_specularTint( HDRColor::WHITE() )
	, m_sunSize( 0.02f )
	, m_moonSize( 0.017f )
	, m_latitude( 0.0f )
	, m_sunRotationOffset( 0.0f )
	, m_moonRotationOffset( 0.0f )
	, m_season( ETOYS_Spring )
	, m_lightBrighthnessClamp( 96.0f )
{
}

CustomLightSettings::CustomLightSettings()
	: m_characterLocalLightRoughnesBias( 0.2f )
{}

//******************************************************************************************************//
//******************************************************************************************************//

ColorGroupSettings::ColorGroupSettings()
	: m_skyTint( HDRColor::WHITE() )
{
	for( auto& c : m_colorGroup ) c = HDRColor::WHITE();
}

//******************************************************************************************************//
//******************************************************************************************************//

GlobalLightOverrideSettings::GlobalLightOverrideSettings()
	: m_enabled( false )
{
	m_color = HDRColor::BLACK();
	m_direction = Vector3::ZEROS();
}

//******************************************************************************************************//
//******************************************************************************************************//

AmbientOverrideSettings::AmbientOverrideSettings()
	: m_enabled( false )
{
	for( auto& c : m_color ) c = HDRColor::BLACK();
}

//******************************************************************************************************//
//******************************************************************************************************//

HeatHazeSettings::HeatHazeSettings()
{
	m_effectStrength = 0.0f;
	m_startDistance = 5.0f;
	m_maxDistance = 600.0f;
	m_patternScale = 1.0f;
	m_movementSpeedScale = 1.0f;
}

//******************************************************************************************************//
//******************************************************************************************************//

EmissiveSettings::EmissiveSettings()
	: m_tint( HDRColor::WHITE() )
	, m_saturation( 1.0f )
	, m_brigtness( 100000.0f )
	, m_exposure( 8.0f, 4.0f )
	, m_cameraLuminance( 0.1f, 10.0f )
	, m_evBlend( 1.0f )
	, m_exposureIBL( 8.0f )
	, m_luminanceIBL( 256.0f )
	, m_exposureScale( 0.05f )
	, m_curveRampIBL( 2.0f )
{

}

//******************************************************************************************************//
//******************************************************************************************************//

DistantProxies::DistantProxies()
	: m_distantProxiesEmissive(0.0f)
	, m_distantProxiesEmissiveHeight(500.0f)
	, m_distantProxiesEmissivePower(5.0f)
	, m_distantProxiesBboxzBlend(0.0f)
{

}

//******************************************************************************************************//
//******************************************************************************************************//

RTTI_BEGIN_TYPE( EmitterGroupParams );
{
	RTTI_PROPERTY( m_group ).editable();
	RTTI_PROPERTY( m_emissionScale ).editable();
	RTTI_PROPERTY( m_opacityScale ).editable();
}
RTTI_END_TYPE();

EmitterGroupParams::EmitterGroupParams()
	: m_group ( EEmitterGroup::EG_Default )
	, m_emissionScale( 1.0f )
	, m_opacityScale( 1.0 )
{}

EmitterGroupSettings::EmitterGroupSettings()
{
	m_emitterGroupParams.Clear();
	for (Uint32 i = 0; i < m_emitterGroupParams.Capacity(); i++)
	{
		EmitterGroupParams params = EmitterGroupParams();
		params.m_group = (EEmitterGroup)i;
		m_emitterGroupParams.EmplaceBack( params );
	}
}

//******************************************************************************************************//
//******************************************************************************************************//

HACK_ENV_Settings::HACK_ENV_Settings()
#if ALLOW_HACK_AREA_SETTINGS
	: m_surfelScale( 1.0f )
	, m_skyScale( 1.0f )
	, m_emissiveScale( 1.0f )
	, m_bottomHemisphereTint(0,0,0,0)
	, m_bottomHemisphereStrength( 0 )
	, m_missingEnergyScale(1.0f)
	, m_overrideOnPureView( true )
	, m_surfAlbedoOverrideRatio( 0.0f )
	, m_surfAlbedoOverride( Color::WHITE() )
#endif
{}

//******************************************************************************************************//
//******************************************************************************************************//

/* -------- Base scene lighting --------- */

CRenderBaseLightParams::CRenderBaseLightParams()
	: m_sunColor( 1.0f, 1.0f, 1.0f, 1.0f )
	, m_moonColor( 1.0f, 1.0f, 1.0f, 1.0f )
	, m_lightColor( 1.1f, 1.1f, 1.1f, 1.0f )
	, m_specularTint( 1, 1, 1, 1 )
	, m_hdrAdaptationDisabled( false )
	, m_sunSize( 0.02f )
	, m_moonSize( 0.05f )
{
	m_lightDirection = Vector4( 1, 2, 3 ).Normalized3();
	m_sunDirection	= m_lightDirection;
	m_moonDirection	= m_lightDirection;
}

CRenderBaseLightParams::~CRenderBaseLightParams()
{
}

//******************************************************************************************************//
//******************************************************************************************************//

/* -------- DAY POINT ENVIRONMENT PARAMETERS --------- */

DayPointEnvironmentParams::DayPointEnvironmentParams()
{
	Reset();
}

void DayPointEnvironmentParams::Reset()
{
	m_skyBoxWeatherBlend = Vector4( 0, 0, 0 );

	m_immediateWetSurfaceEffectStrength = 0.0f;
	m_delayedWetSurfaceEffectStrength = 0.0f;

	m_weatherEffectStrength = 0.0f;

	m_gamma = 2.2f;
	m_cloudsShadowTexture.Reset();

	m_fakeCloudsShadowTexture.Reset();
	m_fakeCloudsShadowSize = 0.05f;
	m_fakeCloudsShadowSpeed = 0.01f;

	m_forcedEnvProbeRotation = Matrix::IDENTITY();
	m_forcedEnvProbeDataSource.Reset();
	m_cameraDirtTexture.Reset();
	m_cameraDirtNumVerticalTiles = 1;
	m_vignetteTexture.Reset();
	m_skyDayAmount = 1.f;
	m_nearPlaneWeightForced = 1.f;
	m_useMoonForShafts = false;
	m_bokehDofParams.Reset();
}

//******************************************************************************************************//
//******************************************************************************************************//

