/**
* Copyright (c) 2019 CD Projekt Red. All Rights Reserved.
*/
#pragma once

class CBitmapTexture;
class CIESDataResource;

#include "renderSkinningData.h"
#include "renderProxyInitData.h"
#include "../../../../engine/shaders/include/areaLightsShared.h"

namespace rend
{
	// TODO{LI}: Used for legacy purpose, remove after resave
	enum GIVolume : Uint8
	{
		GI_Exterior = 0,
		GI_Interior1 = 1,
		GI_Interior2 = 2,
		GI_Interior3 = 3,
		GI_Interior4 = 4,
		GI_SIZE = 5
	};

	// Used by GI Volume
	enum GIGroup : Uint8
	{
		GI_Group0 = 0,
		GI_Group1 = 1,
	};

	enum LightChannel : Uint16
	{
		LC_DefaultAll	= 0,

		// Ones that are based on the volumes placed around the scene
		LC_Channel1		= RED_FLAG(0),
		LC_Channel2		= RED_FLAG(1),
		LC_Channel3		= RED_FLAG(2),
		LC_Channel4		= RED_FLAG(3),
		LC_Channel5		= RED_FLAG(4),
		LC_Channel6		= RED_FLAG(5),
		LC_Channel7		= RED_FLAG(6),
		LC_Channel8		= RED_FLAG(7),
		LC_ChannelWorld	= RED_FLAG(8),

		LC_AllChannels	= RED_FLAG(8)-1,
		LC_All			= RED_FLAG(9)-1,

		// Ones that are related to the object type
		LC_Character	= RED_FLAG(9),
		LC_Player		= RED_FLAG(10),


		// Used only for automated Light Channel assignments
		LC_Automated	= RED_FLAG(15),
	};

	// Checks if given LC need automated patching based on some free slots mechanism
	RED_FORCE_INLINE  Bool IsAutomatedChannel( LightChannel lc, Bool conditional_LC1 = true )
	{
		// SUpport for LC1. In CP77 its used just for cars
		return lc == LC_Automated || ( lc == LC_Channel1 && conditional_LC1 );
	}

	RED_FORCE_INLINE  LightChannel MapLightChannel( LightChannel lc )
	{
		return lc == LC_DefaultAll ? LC_ChannelWorld : lc;
	}

	enum LightChannelGroup : Uint8
	{
		LCG_LightClippingVolume,
		LCG_GIInterior,

		LCG_Max
	};


	enum LightGroup : Uint8
	{
		LG_Group0,
		LG_Group1,
		LG_Group2,
		LG_Group3,
		LG_Group4,
		LG_Group5,
		LG_Group6,
		LG_Group7,

		LG_MAX
	};

	enum LightAttenuation : Uint8
	{
		LA_InverseSquare,
		LA_Linear
	};

	enum ContactShadowReciever : Uint8
	{
		CSR_None			= 0,
		CSR_All				= RED_FLAG( 0 ) | RED_FLAG( 1 ),
		CSR_CharacterOnly	= RED_FLAG( 1 ),
	};

	class RENDER_DATA_API RenderLightBase
	{
	public:
		RenderLightBase();
		~RenderLightBase();

		// Get light color (raw, LDR)
		RED_FORCE_INLINE const Color& GetColor() const { return m_color; }

		// Get light color temperature, set to -1 to use color directly
		RED_FORCE_INLINE const Float GetTemperature() const { return m_temperature; }

		// Get light radius settings
		RED_FORCE_INLINE const Float GetRadius() const { return m_radius; }

		// Get light unit
		RED_FORCE_INLINE const ELightUnit GetUnit() const { return m_unit; }

		// Get light intensity
		RED_FORCE_INLINE const Float GetIntensity() const { return m_intensity; }

		// Get light intensity scale with ray tracing
		RED_FORCE_INLINE const Float GetRayTracingIntensityScale() const { return m_rayTracingIntensityScale; }

		// Get light EV comp
		RED_FORCE_INLINE const Float GetEV() const { return m_EV; }

		// Is this a point/spot or area light?
		RED_FORCE_INLINE const ELightType GetType() const { return m_type; }

		// 
		RED_FORCE_INLINE LightGroup GetGroup() const { return m_group; }

		RED_FORCE_INLINE LightAttenuation GetAttenuationMode() const { return m_attenuation; }

		RED_FORCE_INLINE Uint16 GetChannels() const { return m_lightChannel; }

		RED_FORCE_INLINE void SetChannels( Uint16 val ) { m_lightChannel = val; }

		RED_FORCE_INLINE Bool IsLightVisible() const { return IsVisibleInScene() || GetUseInEnvProbes() || GetUseInFog() || GetUseInGI(); }

		// Allow light to be used in GI lighting
		RED_FORCE_INLINE const Bool GetUseInGI() const { return m_useInGI; }

		// Allow light to be used in Env Probes
		RED_FORCE_INLINE const Bool GetUseInEnvProbes() const { return m_useInEnvProbes; }

		// Allow light to be used in Volumetric Fog
		RED_FORCE_INLINE const Bool GetUseInFog() const { return m_useInFog; }

		// Allow light to be used in Volume effects
		RED_FORCE_INLINE const Bool GetUseInTransparents() const { return m_useInTransparents; }

		RED_FORCE_INLINE const Bool GetUseInParticles() const { return m_useInParticles; }

		// Bounce lights fake GI so they're not generating any specular
		RED_FORCE_INLINE const Bool GetUseSceneSpecular() const { return m_sceneSpecular; }
		RED_FORCE_INLINE const Bool GetUseSceneDiffuse() const { return m_sceneDiffuse; }

		RED_FORCE_INLINE const Bool IsVisibleInScene() const { return GetUseSceneDiffuse() || GetUseSceneSpecular(); }

		RED_FORCE_INLINE const Int8	GetRoughnessBias() const { return m_roughnessBias; }

		// Light escapes light source in uniform directions
		RED_FORCE_INLINE const Bool IsDirectional() const { return m_directional; }

		// Clamps attenuation to close surfaces to 1.0
		RED_FORCE_INLINE const Bool IsClampingAttenuation() const { return m_clampAttenuation; }

		// Is light casting local shadows
		RED_FORCE_INLINE const Bool GetEnableLocalShadows() const 
		{ 
			const Float effectiveOuterAngle = GetEnableShadowAngleControl() ? m_shadowAngle : m_outerAngle;
			return ( ( m_type == LT_Area ) && ( m_areaShape == ALS_Capsule ) && m_spotCapsule ) || ( m_type == LT_Spot ) ?
				( effectiveOuterAngle < 175.0f ) && m_enableLocalShadows : false;
		}

		// Is light casting local shadows only for static proxies
		RED_FORCE_INLINE const Bool GetEnableLocalShadowsForceStaticsOnly() const
		{
			return m_enableLocalShadowsForceStaticsOnly;
		}

		// Is light casting contact shadows
		RED_FORCE_INLINE const ContactShadowReciever GetContactShadows() const
		{ 
			return m_contactShadows;
		}

		// Does the light override its cone angle for shadows.
		RED_FORCE_INLINE const Bool GetEnableShadowAngleControl() const
		{
			return m_shadowAngle > 0.0f;
		}

		// Does the light override its radius for shadows.
		RED_FORCE_INLINE const Bool GetEnableShadowRadiusControl() const
		{
			return m_shadowRadius > 0.0f;
		}

		// Get the distance from light at which the shadow should start to fade away (if 0 then disabled)
		RED_FORCE_INLINE const Float GetShadowFadeDistance() const { return m_shadowFadeDistance; }

		// The range over which the shadows from this light fade
		RED_FORCE_INLINE const Float GetShadowFadeRange() const { return m_shadowFadeRange; }

		// Amout of shadow softness caused by the light
		RED_FORCE_INLINE const ELightShadowSoftnessMode GetShadowSoftnessMode() const { return m_shadowSoftnessMode; }

		// Get built-in flickering settings
		RED_FORCE_INLINE const rend::SLightFlickering& GetFlickerSettings() const { return m_flicker; }

		// Get the environment group this light is assigned to
		RED_FORCE_INLINE const EEnvColorGroup GetEnvColorGroup() const { return m_envColorGroup; }

		RED_FORCE_INLINE const Uint8 GetPortalAngleCutoff() const { return m_portalAngleCutoff; }

		RED_FORCE_INLINE const Uint8 GetEnvColorGroupSaturation() const { return m_colorGroupSaturation; }

		// Allow light to be a distance light
		Bool IsDistantLightAllowed() const;

		// Get spot light inner angle (only for spot lights, < then outer angle)
		RED_FORCE_INLINE const Float GetSpotInnerAngle() const { return m_innerAngle; }

		// Get spot light outer angle (only for spot lights, > than inner angle)
		RED_FORCE_INLINE const Float GetSpotOuterAngle() const { return m_outerAngle; }
		void SetSpotOuterAngle( const Float& angle );

		// get spot light shadow angle (only for spot lights and area spot lights, if the light casts shadows it will use this angle for shadowmap camera)
		RED_FORCE_INLINE const Float GetSpotShadowAngleClamped() const { return Max( 1.0f, m_shadowAngle ); }

		// get spot light shadow angle (only for spot lights and area spot lights, if the light casts shadows it will use this angle for shadowmap camera)
		RED_FORCE_INLINE const Float GetSpotShadowRadiusClamped() const { return Max( 1.0f, m_shadowRadius ); }

		// Get spot light softness - controls the angle based attenuation between inner and outer angle of the spot light
		RED_FORCE_INLINE const Float GetSpotSoftness() const { return m_softness; }

		// Get area light shape
		RED_FORCE_INLINE const EAreaLightShape GetAreaLightShape() const { return m_areaShape; }

		// Get area light is two sided
		RED_FORCE_INLINE const Bool GetAreaTwoSided() const { return m_areaTwoSided; }

		// Get if the capsule light is spot
		RED_FORCE_INLINE const Bool GetSpotCapsule() const { return m_spotCapsule; }

		// Get area light shape radius (if applicable)
		RED_FORCE_INLINE const Float GetSourceRadius() const { return m_sourceRadius; }

		// Get area light sphere radius
		RED_FORCE_INLINE const Float GetCapsuleLength() const { return m_capsuleLength; }

		// Get area light sphere radius
		RED_FORCE_INLINE const Float GetAreaRectSideA() const { return m_areaRectSideA; }

		// Get area light sphere radius
		RED_FORCE_INLINE const Float GetAreaRectSideB() const { return m_areaRectSideB; }

		// Get the assigned projection texture
		RED_FORCE_INLINE const TResAsyncRef< CBitmapTexture >& GetProjectionTexture() const { return m_projectionTexture; }

		// Get the assigned projection texture
		RED_FORCE_INLINE const TResAsyncRef< CIESDataResource >& GetIES() const { return m_iesProfile; }

		RED_FORCE_INLINE Bool CanOuterAngleCastLocalShadows() { return m_outerAngle < 175.0f; }

#ifndef RED_CONFIGURATION_FINAL
		// Tests will check whether supplied point is in local/contact shadow range (local: shadow fade distance + shadow fade range, contact: Min( shadow fade distance, radius * 2.0f ))
		// of light located at lightPos. They both follow rules defined in CClusteredDeferred::FillCullingData for culling purposes.In case of a light that does not cast local shadows 
		// it will return false. RenderLightBase does not store light's position, so one needs to pass it. It is moved here as both, light static node (instance) and light component,
		// use it for debug purposes and we already have too many places to remember about. 
		Bool PointWithinLocalShadowsRange( const Vector3& point, const Vector3& lightsPos ) const;
		Bool PointWithinContactShadowsRange( const Vector3& point, const Vector3& lightsPos ) const;
#endif
		// Check if the light can have distant light for given autohide
		// For example, we don't allow for distant lights if autohide distance is too low relative to radius, because
		// it will be slower to render the light because of overdraw compared to regular clustered lighting
		Bool CanHaveDistantLight( Float autohideDistance, const char** outReason = nullptr ) const;

		// NOTE: value picked rather arbitrary - when we will have results from designated tests it will be swapped.
		static const Uint32 c_maxTrianglesRenderedToShadowMap = 100000;

		struct ValidationContext
		{
			Float autohideDistance = 0.0f;
			Uint32 numTrianglesRenderedToShadowMap = 0;
			Bool isInterior = false;
		};

		// Validate light's properties
		Bool ValidateLight( const ValidationContext& ctx, String* outString ) const;

		Bool IsLightSettingSimilar( const RenderLightBase& other ) const; 


		struct NotificationInfo
		{
			rtti::AccessPath	m_property;
			Bool				m_notify = false;
		};

		// Since both, light component and static light node, have identical calls to OnPropertyPostChange they have been moved here to not duplicate code (and beeing forced to change stuff in two places etc.)
		void OnPropertyPostChange( NotificationInfo& outNorificationInfo, const rtti::AccessPath& path, const rtti::ValuePtr& oldValue, const rtti::ValuePtr& value );

	protected:
		Bool OnPropertyMissing( CName propertyName, const rtti::Variant& readValue );
		void GetCustomEditableProperties( rtti::EditableProperties& outRootProperties ) const;

		// Light
		Color								m_color;			// Color (LDR) of the light, multiplied by brightness
		Float								m_temperature;		// Light color temperature, used if >0
		Float								m_radius;			// Light radius (brightness reaches 0 at this distance - NON PHYSICAL)
		ELightUnit							m_unit;				// Light unit
		Float								m_intensity;		// Light intensity (physical: lumens for point lights etc.)
		Float								m_rayTracingIntensityScale; // Light intensity scale with ray tracing
		Float								m_EV;				// EV
		ELightType							m_type;				// Light type
		Bool								m_useInGI;			// Allow light to be used in GI
		Bool								m_useInEnvProbes;	// Allow light to be used in EnvProbes
		Bool								m_useInFog;			// Allow light to be used in Volumetric Fog
		Bool								m_useInTransparents;// Allow light to be used in forward/particles AND optionally fog
		Bool								m_useInParticles;	// Allow light to be used in DPL (particle lighting)
		Bool								m_sceneDiffuse;		// Bounce lights fake GI so they're not generating any specular
		Bool								m_sceneSpecular;	// Bounce lights fake GI so they're not generating any specular
		Bool								m_directional;		// Uses light direction and not world-to_light 
		Bool								m_clampAttenuation;	// Uses light direction and not world-to_light 
		Int8								m_roughnessBias;	// 
		Uint16								m_lightChannel;		// 
		LightGroup							m_group;
		LightAttenuation					m_attenuation;

		// Shadows
		Bool 								m_enableLocalShadows;					// Light casts local shadows
		Bool								m_enableLocalShadowsForceStaticsOnly;	// Light casts shadows only from static proxies
		ContactShadowReciever				m_contactShadows;						// Light casts contact shadows
		Float								m_shadowFadeDistance;					// Distance from light at which the shadow should start to fade away (if 0 then disabled)
		Float								m_shadowFadeRange;						// Shadow fading range
		ELightShadowSoftnessMode			m_shadowSoftnessMode;					// Shadow softness mode (weights for VSMs)

		// Effects
		SLightFlickering					m_flicker;			// Flickering parameters
		EEnvColorGroup						m_envColorGroup;	// Environment color group
		Uint8								m_colorGroupSaturation;
		Uint8								m_portalAngleCutoff;// Do not go lower than this angle

		// LOD
		Bool								m_allowDistantLight;			// Allow light to become distant light

		// Spot light parameters
		Float								m_innerAngle;					// Inner cone angle
		Float								m_outerAngle;					// Outer cone angle
		Float								m_shadowAngle;					// Cone angle for local shadow. Applied only when light casts shadow. Might differ from outer angle.
		Float								m_shadowRadius;					// Cone radius for local shadow. Applied only when light casts shadow. Might differ from inner angle.
		Float								m_softness;						// Cone softness

		// Area light parameters
		EAreaLightShape						m_areaShape;
		Bool								m_areaTwoSided;
		Bool								m_spotCapsule;
		Float								m_sourceRadius;
		Float								m_capsuleLength;
		Float								m_areaRectSideA;
		Float								m_areaRectSideB;

		// Projection
		TResAsyncRef< CBitmapTexture >		m_projectionTexture;	// Projection texture
		TResAsyncRef< CIESDataResource >	m_iesProfile;			// IES profile resource
	};
}

///////////////////////////////////////////////////////////////////////////////////////////////////////
// Since there is neither an option of multiple parents in RTTI nor RenderLightBase does inherit after 
// ISerializable (possible usage of _INTERNAL_RTTI_OFFSETOF) the following define seems to be the only 
// way to unify RTTI_PROPERTies for light components and nodes.
#define RENDER_LIGHT_BASE_RTTI_PROPERTIES\
	RTTI_PROPERTY_CATEGORY( "Light" );\
	{\
		RTTI_PROPERTY( m_type ).instanceEditable();\
		RTTI_PROPERTY( m_color ).instanceEditable();\
		RTTI_PROPERTY( m_radius ).instanceEditable().range( 0.f, 300.f ).tooltip("Cutoff distance from light origin.\nLights that are visible in scene/fog/env have radius clamped to 30 [m]");\
		RTTI_PROPERTY( m_unit ).readOnly();\
		RTTI_PROPERTY( m_intensity ).instanceEditable().range( 0.f, 2500000.f, true );\
		RTTI_PROPERTY( m_rayTracingIntensityScale ).instanceEditable().range( 0.f, 1.f, true );\
		RTTI_PROPERTY( m_EV ).instanceEditable().range( -30.0f, 30.0f );\
		RTTI_PROPERTY( m_temperature ).instanceEditable();\
		\
		RTTI_PROPERTY_BITFIELD(m_lightChannel,rend::LightChannel).instanceEditable();\
		RTTI_PROPERTY( m_sceneDiffuse ).instanceEditable().tooltip( "Removes specular from lighting." ); \
		RTTI_PROPERTY( m_sceneSpecular ).instanceEditable().tooltip( "Removes specular from lighting." ); \
		RTTI_PROPERTY( m_directional ).instanceEditable().tooltip( "Light shines in a uniform firection." ); \
		RTTI_PROPERTY( m_roughnessBias ).instanceEditable().tooltip( "Changes surface roughness.\nNegative values will make spec more shiny (-127 .. 0)\nPositive will make spec more rough (0..127)" ); \
		\
		RTTI_PROPERTY( m_useInGI ).instanceEditable().tooltip( "Enables light to being used in GI computations" );\
		RTTI_PROPERTY( m_useInEnvProbes ).instanceEditable().tooltip( "Enables light to being used in Env Probes computations" );\
		RTTI_PROPERTY( m_useInTransparents ).instanceEditable().tooltip("Enable light to be in transparent objects. Also this is required for particle lighting and vol fog");\
		RTTI_PROPERTY( m_useInFog ).instanceEditable();\
		RTTI_PROPERTY( m_useInParticles ).instanceEditable();\
		\
		RTTI_PROPERTY( m_attenuation ).instanceEditable();\
		RTTI_PROPERTY( m_clampAttenuation ).instanceEditable().tooltip( "For near surfaces clamps attenuation to 1.0" );\
		RTTI_PROPERTY(m_group).instanceEditable();\
	}\
	RTTI_PROPERTY_CATEGORY( "Area" );\
	{\
		RTTI_PROPERTY( m_areaShape ).instanceEditable();\
		RTTI_PROPERTY( m_areaTwoSided ).instanceEditable();\
		RTTI_PROPERTY( m_spotCapsule ).instanceEditable();\
		RTTI_PROPERTY( m_sourceRadius ).instanceEditable().range( AREA_LIGHT_RADIUS_COMPLETE_SPEC_DECAY, 2.0f ).tooltip( "Going below 0.01f reduces specular energy (and not radius)" );\
		RTTI_PROPERTY( m_capsuleLength ).instanceEditable().range( 0.0f, 50.0f );\
		RTTI_PROPERTY( m_areaRectSideA ).instanceEditable();\
		RTTI_PROPERTY( m_areaRectSideB ).instanceEditable();\
	}\
	RTTI_PROPERTY_CATEGORY( "Spot" );\
	{\
		RTTI_PROPERTY( m_innerAngle ).instanceEditable().range( 1.0f, 355.0f );\
		RTTI_PROPERTY( m_outerAngle ).instanceEditable().range( 1.0f, 355.0f );\
		RTTI_PROPERTY( m_softness ).instanceEditable();\
	}\
	RTTI_PROPERTY_CATEGORY( "Shadows" );\
	{\
		RTTI_PROPERTY( m_enableLocalShadows ).instanceEditable().tooltip( "Should this light cast local shadows." );\
		RTTI_PROPERTY( m_enableLocalShadowsForceStaticsOnly ).instanceEditable().tooltip( "Should this light cast local shadows only from static geometry." );\
		RTTI_PROPERTY( m_contactShadows ).instanceEditable().tooltip( "Should this light cast local contact shadows" );\
		RTTI_PROPERTY( m_shadowAngle ).instanceEditable().tooltip( "Light's custom cone angle for shadow map. -1 means NOT ACTIVATED. Otherwise, it will clamp the values from 1 to 174." ).range( -1.0f, 174.0f );\
		RTTI_PROPERTY( m_shadowRadius ).instanceEditable().tooltip( "Light's custom cone radius for shadow map. -1 means NOT ACTIVATED. Otherwise, it will clamp the values from 1 to 30." ).range( -1.0f, 30.0f );\
		RTTI_PROPERTY( m_shadowFadeDistance ).instanceEditable();\
		RTTI_PROPERTY( m_shadowFadeRange ).instanceEditable();\
		RTTI_PROPERTY( m_shadowSoftnessMode ).instanceEditable();\
	}\
	RTTI_PROPERTY_CATEGORY( "Projection" );\
	{\
		RTTI_PROPERTY( m_iesProfile ).instanceEditable();\
	}\
	RTTI_PROPERTY_CATEGORY( "Effects" );\
	{\
		RTTI_PROPERTY( m_flicker ).instanceEditable();\
		RTTI_PROPERTY( m_envColorGroup ).instanceEditable();\
		RTTI_PROPERTY( m_colorGroupSaturation ).instanceEditable();\
		RTTI_PROPERTY( m_portalAngleCutoff ).instanceEditable().range(0,90);\
	}\
	RTTI_PROPERTY_CATEGORY( "LOD" );\
	{\
		RTTI_PROPERTY( m_allowDistantLight ).instanceEditable();\
	}
