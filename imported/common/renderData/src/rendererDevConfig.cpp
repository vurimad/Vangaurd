
#include "build.h"
#include "rendererDevConfig.h"

RTTI_BEGIN_ENUM_IN_NAMESPACE( SurfelView, renderDev );
{
	RTTI_ENUM_OPTION( ALBEDO );
	RTTI_ENUM_OPTION( NORMAL );
	RTTI_ENUM_OPTION( SHADOWS );
	RTTI_ENUM_OPTION( CLOSEST_PROBE );
	RTTI_ENUM_OPTION( EMISSIVE );
	RTTI_ENUM_OPTION( LIGHTING );
	RTTI_ENUM_OPTION( BOUNCE );
	RTTI_ENUM_OPTION( INSIDE );
	RTTI_ENUM_OPTION( SHADOW );
}
RTTI_END_ENUM();


RTTI_BEGIN_ENUM_IN_NAMESPACE( GIProbeView, renderDev );
{
	RTTI_ENUM_OPTION( RADIANCE );
	RTTI_ENUM_OPTION( SKY_VISIBILITY );
	RTTI_ENUM_OPTION( ENV_ID );
	RTTI_ENUM_OPTION( FLAG_0 );
	RTTI_ENUM_OPTION( FLAG_1 );
	RTTI_ENUM_OPTION( FLAG_2 );
	RTTI_ENUM_OPTION( CURRENT_ID );
}
RTTI_END_ENUM();


RTTI_BEGIN_ENUM_IN_NAMESPACE( EnvProbeView, renderDev );
{
	RTTI_ENUM_OPTION( RADIANCE );
	RTTI_ENUM_OPTION( ALBEDO );
	RTTI_ENUM_OPTION( NORMAL );
	RTTI_ENUM_OPTION( ROUGHNESS );
	RTTI_ENUM_OPTION( METALNESS );
	RTTI_ENUM_OPTION( EMISSIVE );
	RTTI_ENUM_OPTION( SKY_MASK );
}
RTTI_END_ENUM();

RTTI_BEGIN_ENUM_IN_NAMESPACE( TXAADebugMode, renderDev );
{
	RTTI_ENUM_OPTION( TXAA_NoDebug );
	RTTI_ENUM_OPTION( TXAA_ShowHistoryBlendFactor );
}
RTTI_END_ENUM();

//**********************************************************************************************//
//**********************************************************************************************//

namespace renderDev
{
	SConfig::SConfig()
		: m_surfelView( SurfelView::ALBEDO )
		, m_giProbeView( GIProbeView::RADIANCE )
		, m_envProbeView( EnvProbeView::RADIANCE )
	{
		Custom.m_renderToggle[0].SetValue( false );
		Custom.m_renderToggle[1].SetValue( false );
		Custom.m_renderToggle[2].SetValue( false );
		Custom.m_renderToggle[3].SetValue( false );
	}

	SConfig::~SConfig()
	{

	}

	//**********************************************************************************************//
	//**********************************************************************************************//

	Float ApplyDevOverride( Float timeOfDay, SConfig* devConfig )
	{
		if ( devConfig )
		{
			const SConfig::TimeOfDayOverride& timeOfDayOverride = devConfig->TimeOfDay;
			return timeOfDayOverride.m_enabled ? timeOfDayOverride.m_timeOfDay : timeOfDay;
		}

		return timeOfDay;
	}
}
