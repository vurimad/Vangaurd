/**
* Copyright (c) 2017 CD Projekt Red. All Rights Reserved.
*/
#include "build.h"

#include "iesResource.h"
#include "renderer.h"

RTTI_BEGIN_TYPE( CIESDataResource );
{
	RTTI_PARENT_TYPE( CResource );

	RTTI_PROPERTY( m_samples ).readOnly();
}
RTTI_END_TYPE();

//*********************************************************************************************************************************************//
//*********************************************************************************************************************************************//

CIESDataResource::CIESDataResource()
	: m_renderIndex( 0 )
{
	red::Memset( m_samples, 0xFF, sizeof( m_samples ) );
}

CIESDataResource::~CIESDataResource()
{

}

Uint16 CIESDataResource::GetRenderResourceIndex()
{
	if( !m_renderIndex )
	{
		RED_SCOPE_LOCK( m_lock );

		if( !m_renderIndex )
		{
			RecreateInternalRenderResources( 0 );
		}
	}

	return m_renderIndex;
}

Float CIESDataResource::GetAverageLuminance( Float outerAngle ) const
{
	Float outerAngleCos		= std::cos( outerAngle * RED_PI / 180.0f );
	Float outerAngleCos2	= outerAngleCos * outerAngleCos;

	Float sampleSum = 0.0f;
	Float weightSum = 0.0f;

	for( Uint32 i = 0; i < NUM_SAMPLES; ++i )
	{
		Float angle = (i + 0.5f) / Float( NUM_SAMPLES );

		if( angle > outerAngleCos2 )
			break;

		// Its a circle of the given radius
		Float weight = std::cos( angle * angle * RED_PI );
		sampleSum += weight * m_samples[i];
		weightSum += weight;
	}

	return sampleSum / Max( 255.0f * weightSum, 1.0f );

}

void CIESDataResource::RecreateInternalRenderResources( Uint32 flag )
{
	CResource::RecreateInternalRenderResources( flag );

	m_renderIndex = GRender->UploadIESData( m_samples );
}

void CIESDataResource::Import( const Float* values, Uint32 numSamples /*= NUM_SAMPLES*/ )
{
	RED_FATAL_ASSERT( values );

	Float  maxCandels = 1e-5f;

	for( Uint32 i = 0; i < numSamples; ++i )
	{
		maxCandels = Max( maxCandels, values[i] );
	}

	// Normalize it
	{
		const Float invMaxCandels = 255.0f / maxCandels;

		Uint8* outData = m_samples;

		for( Uint32 i = 0; i < NUM_SAMPLES; ++i )
		{
			Uint32 srcIdx = Min( i * numSamples / NUM_SAMPLES, numSamples - 1 );

			outData[i] = Clamp<Uint32>( (Uint32)(values[srcIdx] * invMaxCandels), 0, 255 );
		}
	}
}

//*********************************************************************************************************************************************//
//*********************************************************************************************************************************************//
