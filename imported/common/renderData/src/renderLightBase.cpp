/**
* Copyright (c) 2019 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"
#include "renderLightBase.h"
#include "../../../common/redReflection/include/variant.h"
#include "../../redReflection/include/rttiPathParser.h"

// TODO{LI}: Used for legacy purpose, remove after resave
RTTI_BEGIN_ENUM_IN_NAMESPACE( GIVolume, rend )
{
	RTTI_ENUM_OPTION( GI_Exterior );
	RTTI_ENUM_OPTION( GI_Interior1 );
	RTTI_ENUM_OPTION( GI_Interior2 );
	RTTI_ENUM_OPTION( GI_Interior3 );
	RTTI_ENUM_OPTION( GI_Interior4 );
}
RTTI_END_ENUM();

RTTI_BEGIN_ENUM_IN_NAMESPACE( GIGroup, rend )
{
	RTTI_ENUM_OPTION( GI_Group0 );
	RTTI_ENUM_OPTION( GI_Group1 );
}
RTTI_END_ENUM();

RTTI_BEGIN_BITFIELD_IN_NAMESPACE( LightChannel , rend )
{
	RTTI_BITFIELD_OPTION( LC_Channel1 );
	RTTI_BITFIELD_OPTION( LC_Channel2 );
	RTTI_BITFIELD_OPTION( LC_Channel3 );
	RTTI_BITFIELD_OPTION( LC_Channel4 );
	RTTI_BITFIELD_OPTION( LC_Channel5 );
	RTTI_BITFIELD_OPTION( LC_Channel6 );
	RTTI_BITFIELD_OPTION( LC_Channel7 );
	RTTI_BITFIELD_OPTION( LC_Channel8 );
	RTTI_BITFIELD_OPTION( LC_ChannelWorld );
	RTTI_BITFIELD_OPTION( LC_Character );
	RTTI_BITFIELD_OPTION( LC_Player );

	RTTI_BITFIELD_OPTION( LC_Automated );
}
RTTI_END_BITFIELD();

RTTI_BEGIN_ENUM_IN_NAMESPACE( LightGroup, rend )
{
	RTTI_ENUM_OPTION( LG_Group0 );
	RTTI_ENUM_OPTION( LG_Group1 );
	RTTI_ENUM_OPTION( LG_Group2 );
	RTTI_ENUM_OPTION( LG_Group3 );
	RTTI_ENUM_OPTION( LG_Group4 );
	RTTI_ENUM_OPTION( LG_Group5 );
	RTTI_ENUM_OPTION( LG_Group6 );
	RTTI_ENUM_OPTION( LG_Group7 );
}
RTTI_END_ENUM();

RTTI_BEGIN_ENUM_IN_NAMESPACE( LightAttenuation, rend )
{
	RTTI_ENUM_OPTION( LA_InverseSquare );
	RTTI_ENUM_OPTION( LA_Linear );
}
RTTI_END_ENUM();

RTTI_BEGIN_ENUM_IN_NAMESPACE( ContactShadowReciever, rend )
{
	RTTI_ENUM_OPTION( CSR_None );
	RTTI_ENUM_OPTION( CSR_All );
	RTTI_ENUM_OPTION( CSR_CharacterOnly );
}
RTTI_END_ENUM();


namespace rend
{
	RenderLightBase::RenderLightBase()
		: m_enableLocalShadows( false )
		, m_enableLocalShadowsForceStaticsOnly( false )
		, m_contactShadows( CSR_None )
		, m_type( LT_Point )
		, m_unit( LU_Lumen )
		, m_shadowFadeRange( 5.0f )
		, m_shadowFadeDistance( 10.0f )
		, m_shadowSoftnessMode( ELightShadowSoftnessMode::LSSM_Default )
		, m_color( Color::WHITE() )
		, m_temperature( -1.0f )
		, m_radius( 5.0f )
		, m_intensity( 100.0f )
		, m_rayTracingIntensityScale( 1.0f )
		, m_EV( 0.0f )
		, m_envColorGroup( ECG_Default )
		, m_allowDistantLight( true )
		, m_directional( false )
		, m_useInGI( true )
		, m_useInEnvProbes( true )
		, m_useInFog( true )
		, m_useInTransparents( true )
		, m_useInParticles( true )
		, m_sceneDiffuse( true )
		, m_sceneSpecular( true )
		, m_roughnessBias( 0 )
		, m_innerAngle( 30.0f )
		, m_colorGroupSaturation( 100 )
		, m_outerAngle( 45.0f )
		, m_shadowAngle( -1.0f )
		, m_shadowRadius( -1.0f )
		, m_softness( 2.0f )
		, m_areaShape( ALS_Capsule )
		, m_areaTwoSided( true )
		, m_spotCapsule( false )
		, m_sourceRadius( 0.05f )
		, m_capsuleLength( 1.0f )
		, m_areaRectSideA( 1.0f )
		, m_areaRectSideB( 1.0f )
		, m_lightChannel( 0 )
		, m_portalAngleCutoff( 0 )
		, m_group( LG_Group0 )
		, m_clampAttenuation( false )
		, m_attenuation( LA_InverseSquare )
	{
	}

	RenderLightBase::~RenderLightBase()
	{
	}

	Bool RenderLightBase::OnPropertyMissing( CName propertyName, const rtti::Variant& readValue )
	{
		// Mapping from ELightShadowCastingMode to "Enable Local Shadows" and "Enable Contact Shadows" checkboxes.
		if ( propertyName == RED_NAME_CONSTEXPR_NOREG( "shadowCastingMode" ) )
		{
			bool disableShadowsForLargeCapsules = ( ( m_type == LT_Area ) && ( m_areaShape == ALS_Capsule ) && m_spotCapsule ) || ( m_type == LT_Spot ) ? ( m_outerAngle < 175.0f ) : true;

			ELightShadowCastingMode shadowMode;
			if ( !readValue.Get( shadowMode ) )
			{
				return false;
			}

			else if ( shadowMode == LSCM_OnlyDynamic || shadowMode == LSCM_OnlyStatic ||
				shadowMode == LSCM_NormalAndContact || shadowMode == LSCM_Normal )
			{
				m_enableLocalShadows = disableShadowsForLargeCapsules;
			}

			if ( shadowMode == LSCM_NormalAndContact || shadowMode == LSCM_OnlyContact )
			{
				m_contactShadows = disableShadowsForLargeCapsules ? CSR_All : CSR_None;
			}
		}
		else if( propertyName == RED_NAME_CONSTEXPR_NOREG( "enableContactShadows" ) )
		{
			Bool enableContactShadows;
			if( !readValue.Get( enableContactShadows ) )
			{
				return false;
			}

			m_contactShadows = enableContactShadows ? CSR_All : CSR_None;
		}
		// There was a mismatch between property naming in light node and light component.
		else if ( propertyName == RED_NAME_CONSTEXPR_NOREG( "useInEnvProbe" ) )
		{
			Bool useInEnvProbe;
			if ( !readValue.Get( useInEnvProbe ) )
			{
				return false;
			}

			m_useInEnvProbes = useInEnvProbe;
		}
		else if( propertyName == RED_NAME_CONSTEXPR_NOREG( "useInVolume" ) )
		{
			Bool useInVolume;
			if( !readValue.Get( useInVolume ) )
			{
				return false;
			}

			m_useInTransparents = useInVolume;
		}
		else if( propertyName == RED_NAME_CONSTEXPR_NOREG( "useInScene" ) )
		{
			Bool useInScene;
			if (!readValue.Get(useInScene))
			{
				return false;
			}

			m_sceneSpecular  = m_sceneDiffuse = useInScene;
		}
		else if( propertyName == RED_NAME_CONSTEXPR_NOREG( "noSpecular" ) )
		{
			Bool noSpecular;
			if (!readValue.Get(noSpecular))
			{
				return false;
			}

			m_sceneSpecular = !noSpecular;
		}
		else if( propertyName == RED_NAME_CONSTEXPR_NOREG( "enableShadowAngleControl" ) )
		{
			Bool enableShadowAngleControl;
			if ( !readValue.Get( enableShadowAngleControl ) )
			{
				return false;
			}

			m_shadowAngle = ( enableShadowAngleControl ) ? m_shadowAngle : 0.0f;
		}

		return true;
	}

	void RenderLightBase::GetCustomEditableProperties( rtti::EditableProperties& outRootProperties ) const
	{
		outRootProperties.RemoveIf( [this] ( const rtti::ClassEditablePropertyInfo& info )
		{
			// Remove useless properties for each light type.
			if ( GetType() == LT_Point )
			{
				if ( info.m_category == RED_NAME_CONSTEXPR_NOREG( "Spot" ) ||
					 info.m_category == RED_NAME_CONSTEXPR_NOREG( "Area" ) ||
					 info.m_category == RED_NAME_CONSTEXPR_NOREG( "Projection" ) )
				{
					return true;
				}
			}
			else if ( GetType() == LT_Area )
			{
				if ( info.m_category == RED_NAME_CONSTEXPR_NOREG( "Spot" ) )
				{
					if ( !((GetAreaLightShape() == ALS_Capsule) && GetSpotCapsule()) )
					{
						return true;
					}
				}
				else if ( info.m_category == RED_NAME_CONSTEXPR_NOREG( "Area" ) )
				{
					// Area light can have different shapes with distinguished parameters.
					if ( GetAreaLightShape() == ALS_Sphere )
					{
						if ( info.m_name == RED_NAME_CONSTEXPR_NOREG( "areaTwoSided" ) ||
							 info.m_name == RED_NAME_CONSTEXPR_NOREG( "spotCapsule" ) ||
							 info.m_name == RED_NAME_CONSTEXPR_NOREG( "capsuleLength" ) ||
							 info.m_name == RED_NAME_CONSTEXPR_NOREG( "areaRectSideA" ) ||
							 info.m_name == RED_NAME_CONSTEXPR_NOREG( "areaRectSideB" ) )
						{
							return true;
						}
					}
					else if ( GetAreaLightShape() == ALS_Capsule )
					{
						if ( info.m_name == RED_NAME_CONSTEXPR_NOREG( "areaRectSideA" ) ||
							 info.m_name == RED_NAME_CONSTEXPR_NOREG( "areaRectSideB" ) ||
							 info.m_name == RED_NAME_CONSTEXPR_NOREG( "areaTwoSided" ) )
						{
							return true;
						}
					}
				}
			}
			else if ( GetType() == LT_Spot )
			{
				if ( info.m_category == RED_NAME_CONSTEXPR_NOREG( "Area" ) )
				{
					return true;
				}
			}
			
			if( ( GetType() == LT_Point || 
				( GetType() == LT_Area && ( GetAreaLightShape() != ALS_Capsule || !GetSpotCapsule() ) ) ) &&
				( info.m_category == RED_NAME_CONSTEXPR_NOREG( "Shadows" ) && info.m_name != RED_NAME_CONSTEXPR_NOREG("contactShadows") && info.m_name != RED_NAME_CONSTEXPR_NOREG( "shadowRadius" )) )
			{
				return true;
			}

			if ( !GetEnableLocalShadows() )
			{
				if ( info.m_name == RED_NAME_CONSTEXPR_NOREG( "enableLocalShadowsForceStaticsOnly" ) ||
					 info.m_name == RED_NAME_CONSTEXPR_NOREG( "enableShadowAngleControl" ) ||
					 info.m_name == RED_NAME_CONSTEXPR_NOREG( "shadowFadeDistance" ) ||
					 info.m_name == RED_NAME_CONSTEXPR_NOREG( "shadowFadeRange" ) ||
					 info.m_name == RED_NAME_CONSTEXPR_NOREG( "shadowSoftnessMode" ) ||
					 info.m_name == RED_NAME_CONSTEXPR_NOREG( "shadowAngle" ) )
				{
					return true;
				}
			}

			if( !GetEnableLocalShadows() && !GetContactShadows() )
			{
				if( info.m_name == RED_NAME_CONSTEXPR_NOREG( "shadowRadius" ) )
				{
					return true;
				}
			}

			if( !GetUseInTransparents() )
			{
				if( info.m_name == RED_NAME_CONSTEXPR_NOREG( "useInFog" ) ||
					info.m_name == RED_NAME_CONSTEXPR_NOREG( "useInParticles" ) )
				{
					return true;
				}
			}

			if( !IsVisibleInScene() )
			{
				if( info.m_name == RED_NAME_CONSTEXPR_NOREG( "lightChannel" ) ||
					info.m_name == RED_NAME_CONSTEXPR_NOREG( "directional" ) )
				{
					return true;
				}

			}

			if ( !GetUseInTransparents() && !IsVisibleInScene())
			{
				if (info.m_name == RED_NAME_CONSTEXPR_NOREG("roughnessBias") )
				{
					return true;
				}
			}

			return false;
		} );

		outRootProperties.Modify( [this]( rtti::ClassEditablePropertyInfo& info )
								  {
									  // Lights with 
									  if( info.m_name == RED_NAME_CONSTEXPR_NOREG( "radius" ) )
									  {
										  info.m_maxValue = IsLightVisible() ? 60.0f : 300.0f;
									  }
								  } );

	}

	Bool RenderLightBase::IsDistantLightAllowed() const
	{
		if ( !m_allowDistantLight )
		{
			return false;
		}

		// if WorldLightChannel is present, then we want to create distant light for it
		if ( GetChannels() != 0 && ( 0 == ( GetChannels() & rend::LC_ChannelWorld ) ) )
		{
			return false;
		}

		return true;
	}

	void RenderLightBase::SetSpotOuterAngle( const Float& angle )
	{
		m_outerAngle = angle;
	}

#ifndef RED_CONFIGURATION_FINAL
	Bool RenderLightBase::PointWithinLocalShadowsRange( const Vector3& point, const Vector3& lightsPos ) const
	{
		if ( GetEnableLocalShadows() )
		{
			const Float realDistance = point.DistanceTo( lightsPos );
			const Float shadowRange = GetShadowFadeDistance() + GetShadowFadeRange();
			return realDistance < shadowRange;
		}

		return false;
	}

	Bool RenderLightBase::PointWithinContactShadowsRange( const Vector3& point, const Vector3& lightsPos ) const
	{
		if ( GetContactShadows() )
		{	// TODO: check shadow radius vs radius here.
			const Float realDistance = point.DistanceTo( lightsPos );
			const Float shadowRange = Min( GetShadowFadeDistance(), GetRadius() * 2.0f );
			return realDistance < shadowRange;
		}

		return false;
	}

#endif

	Bool RenderLightBase::CanHaveDistantLight( Float autohideDistance, const char** outReason ) const
	{
		Float radius = m_radius;

		if ( m_type == LT_Spot && m_outerAngle < 90.0f )
		{
			// compensate for "narrow" spot light - we do not care what's behind the spot direction
			radius /= 2.0f;
		}

		const Float radiusToDistanceRatio = 3.0f;
		if ( m_radius * radiusToDistanceRatio > autohideDistance )
		{
			if ( outReason ) *outReason = "Autohide distance is too low compared to the light radius";
			return false;
		}

		return true;
	}

	Bool RenderLightBase::ValidateLight( const ValidationContext& ctx, String* outString ) const
	{
		Bool success = true;

		if ( IsDistantLightAllowed() && !ctx.isInterior )
		{
			const char* reason = "";
			if ( !CanHaveDistantLight( ctx.autohideDistance, &reason ) )
			{
				if ( outString )
				{
					*outString += "Light won't have distant light generated: ";
					*outString += reason;
					*outString += "\n";
				}
				success = false;
			}
		}

		if ( m_color.R == 0 && m_color.G && m_color.B == 0 )
		{
			if ( outString )
			{
				*outString += "Light is black\n";
			}
			success = false;
		}

		if ( m_radius > 4.0f * ctx.autohideDistance )
		{
			if ( outString )
			{
				*outString += "Autohide distance is too low compared to the light radius";
			}
			success = false;
		}

		if ( m_radius * 20.0f < ctx.autohideDistance )
		{
			if ( outString )
			{
				*outString += "Autohide distance is too high compared to the light radius";
			}
			success = false;
		}

		if ( ctx.numTrianglesRenderedToShadowMap > c_maxTrianglesRenderedToShadowMap )
		{
			if ( outString )
			{
				*outString += String::Printf( "Too many triangles rendered to shadow map (%uK)", ctx.numTrianglesRenderedToShadowMap / 1000 );
			}
			success = false;
		}

		return success;
	}

	Bool RenderLightBase::IsLightSettingSimilar( const RenderLightBase& other ) const
	{
		return (m_type == other.m_type)
			&& (m_color == other.m_color)
			&& (m_enableLocalShadows == other.m_enableLocalShadows)
			&& (m_contactShadows == other.m_contactShadows)
			&& m_useInFog == other.m_useInFog
			&& m_useInGI == other.m_useInGI
			&& m_sceneDiffuse == other.m_sceneDiffuse
			&& m_sceneSpecular == other.m_sceneSpecular
			;
	}

	void RenderLightBase::OnPropertyPostChange( NotificationInfo& outNotificationInfo, const rtti::AccessPath& path, const rtti::ValuePtr& oldValue, const rtti::ValuePtr& value )
	{
		outNotificationInfo = {};

		if ( path.ToString() == "type" )
		{

			// Assure that Point light won't cast any shadows. In case of Point lights we are 
			// actually hiding these properties, hence no NotifyPropertyChanged.
			if ( m_type == LT_Point )
			{
				m_enableLocalShadows = false;
			}

			m_unit = ( m_type == LT_Area ) ? LU_Nit : LU_Lumen;

			// Notification needed - no particular property. Also, m_property needs to be empty for new parameters to show up
			outNotificationInfo.m_notify = true;
		}
		else if ( path.ToString() == "enableLocalShadows" || path.ToString() == "contactShadows" )
		{
			// In case we are turning on local shadows, we only care about outer angle (shadow angle is disabled on the entrance).
			// If it turns out that current outer angle cannot cast local shadows we set the property to false. Unfortunately,
			// I do not see an easy way to inform user about it via UI and WinAPI's MessageBox is most probably out of the question (although it works ;p).
			if ( m_enableLocalShadows && !CanOuterAngleCastLocalShadows() )
			{
				m_enableLocalShadows = false;
			}

			// Notification needed - no particular property. Also, m_property needs to be empty for new parameters to show up
			outNotificationInfo.m_notify = true;
		}
		else if ( path.ToString() == "areaShape" )
		{			
			// Notification needed - no particular property. Also, m_property needs to be empty for new parameters to show up
			outNotificationInfo.m_notify = true;
		}
		else if ( path.ToString() == "autoHideDistance" )
		{
			outNotificationInfo.m_notify = true;
			outNotificationInfo.m_property = rtti::AccessPath( "defaultStreamingDistance" );
		}
		else if ( path.ToString() == "radius" )
		{
			outNotificationInfo.m_notify = true;
			outNotificationInfo.m_property = rtti::AccessPath( "streamingBoundingBox" );
		}
		else if ( path.ToString() == "useInTransparents" || path.ToString() == "sceneDiffuse" || path.ToString() == "sceneSpecular" )
		{
			// Notification needed - no particular property. Also, m_property needs to be empty for new parameters to show up
			outNotificationInfo.m_notify = true;
		}
		else if ( path.ToString() == "spotCapsule" )
		{
			// Notification needed - no particular property. Also, m_property needs to be empty for new parameters to show up
			outNotificationInfo.m_notify = true;
		}
		else if ( path.ToString() == "enableShadowAngleControl" )
		{
			// Since we allow light to cast shadow when its outer angle >= 175 && shadow angle < 175,
			// it might happens that one will disable shadow angle control while outer angle >= 175.
			// In this case we must provide that local and contact shadows will be turned off.
			if ( !CanOuterAngleCastLocalShadows() )
			{
				m_enableLocalShadows = false;
			}

			// Notification needed - no particular property. Also, m_property needs to be empty for new parameters to show up
			outNotificationInfo.m_notify = true;
		}
		else if ( path.ToString() == "outerAngle" || path.ToString() == "shadowAngle" )
		{
			// If someone goes above 175 degrees and shadow angle is not enabled (or disable shadow angle via moving slider to -1 and outer angle is > 175),
			// we hide local shadows panel and disable local shadows in general.
			if ( m_enableLocalShadows && !GetEnableShadowAngleControl() && !CanOuterAngleCastLocalShadows() )
			{
				m_enableLocalShadows = false;
				// Notification needed - no particular property. Also, m_property needs to be empty for new parameters to show up
				outNotificationInfo.m_notify = true;
			}
		}
	}

}
