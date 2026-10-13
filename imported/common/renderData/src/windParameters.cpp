/**
* Copyright (c) 2007 CD Projekt Red. All Rights Reserved.
*/

#include "build.h"
#include "windParameters.h"
#include "../../redMath/include/random.h"


WindParameters::WindParameters()
	: m_speedTreeEnabled( true )	
	, m_windScale( 0.1f )
	, m_windDirection( 0.0f )
	, m_environmentWindStrengthOverride( 1.0f )
	, m_accumTimeSinceGameStart( 0.0f )
	, m_targetDirection( 0.0f )
	, m_windDirectionAdaptationSpeed( 0.002f )
	, m_frac( 0.0f )
	, m_cachedWindDirection(1.0f, 0.0f, 0.0f)
	, m_accumCloudOffset( 0.0f, 0.0f, 0.0f, 0.0f )
	, m_sourceWindDirection( 0.0f )
{	
	
}

void WindParameters::SetWindScale( Float scale )
{
	m_windScale = Clamp<Float>( scale, 0.0f, 1.0f );
}

void WindParameters::NotifyConditionsChanged()
{
	// randomize wind direction now
	Float rndZ = math::DefaultRandom().Get<Float>( -RED_PI, RED_PI );
	// make sure the turn is substantial
	while( fabs(rndZ) < 0.3f*RED_PI  )
	{
		rndZ = math::DefaultRandom().Get<Float>( -RED_PI, RED_PI );
	}

	m_targetDirection = m_windDirection + rndZ;

	if( m_targetDirection > RED_PI ) m_targetDirection = -RED_PI + ( m_targetDirection - MFloor( m_targetDirection / RED_PI ) );
	else if( m_targetDirection < -RED_PI ) m_targetDirection = RED_PI + ( MFloor( m_targetDirection / RED_PI ) - m_targetDirection );

	m_frac = 0.0f;
	m_sourceWindDirection = m_windDirection;
}

void WindParameters::TickWindParameters( Float timeDelta )
{	
	if( fabs(m_windDirection - m_targetDirection) > 0.01f )
	{		
		m_frac = Clamp<Float>( m_frac + timeDelta*m_windDirectionAdaptationSpeed, 0.0f, 1.0f );		
		m_windDirection = math::Lerp<Float>( m_frac, m_sourceWindDirection, m_targetDirection );
		
		m_cachedWindDirection.X = MCos( m_windDirection );
		m_cachedWindDirection.Y = MSin( m_windDirection );
	}

	m_accumCloudOffset += -m_cachedWindDirection * timeDelta * m_windScale;
	m_accumTimeSinceGameStart += timeDelta;
}

void WindParameters::ForceWindParameters( Float windScale, Bool forceDirectionStabilize, Float environmentWindStrengthOverride )
{
	m_windScale = windScale;
	m_environmentWindStrengthOverride = environmentWindStrengthOverride;

	if( forceDirectionStabilize )
	{
		m_windDirection = m_targetDirection;
		m_frac = 0.0f;
	}
}
