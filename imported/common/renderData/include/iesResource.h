/**
* Copyright (c) 2017 CD Projekt Red. All Rights Reserved.
*/
#ifndef REFLECTION_PROBE_RESOURCE_H
#define REFLECTION_PROBE_RESOURCE_H

#include "renderDataApi.h"

#include "../../redReflection/include/resource.h"

//*********************************************************************************************************************************************//
//*********************************************************************************************************************************************//

class RENDER_DATA_API CIESDataResource : public CResource
{

	RTTI_DECLARE_TYPE( CIESDataResource );
	IMPLEMENT_RESOURCE_INTERFACE( "ies", "ies", "IES Light Profile Data Resource" );
	RED_BASE_CLASS( CResource );
	RED_USE_MEMORY_POOL( rend::PoolLighting );

public:

	static constexpr Uint32		NUM_SAMPLES = 128;

private:

	Uint32				m_renderIndex;
	Uint8				m_samples[NUM_SAMPLES];
	red::RWSpinLock		m_lock;

public:

	CIESDataResource();

	virtual ~CIESDataResource();

	// - - - - - - - - - - - - - - - - - - - - - - - - - 

	Uint16									GetRenderResourceIndex();

	inline Float							Sample( Float NnL ) const
	{
		NnL *= NnL * (NUM_SAMPLES - 1);

		const Uint32 idx0 = (Uint32)NnL;
		const Uint32 idx1 = Min( idx0 + 1, NUM_SAMPLES - 1 );

		const Float lerp = NnL - idx0;

		return math::Lerp<Float>( lerp, m_samples[idx0], m_samples[idx1] ) / 255.0f;
	}

	Float									GetAverageLuminance( Float outerAngle ) const;

	// - - - - - - - - - - - - - - - - - - - - - - - - - 

	virtual void							RecreateInternalRenderResources( Uint32 flag ) override;

	virtual Uint32							GetRenderResourceSize() const override { return NUM_SAMPLES; };

	// - - - - - - - - - - - - - - - - - - - - - - - - - 

	void									Import( const Float* values , Uint32 numSamples = NUM_SAMPLES );

	// - - - - - - - - - - - - - - - - - - - - - - - - - 

};

//*********************************************************************************************************************************************//
//*********************************************************************************************************************************************//

#endif // REFLECTION_PROBE_RESOURCE_H
