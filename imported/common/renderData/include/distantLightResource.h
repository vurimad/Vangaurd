/**
* Copyright (c) 2017 CD Projekt Red. All Rights Reserved.
*/
#ifndef DISTANT_LIGHT_RESOURCE_H
#define DISTANT_LIGHT_RESOURCE_H

#include "renderDataApi.h"
#include "../../redReflection/include/streamedResource.h"
#include "../../redReflection/include/dataBuffer.h"

//*********************************************************************************************************************************************//
//*********************************************************************************************************************************************//

namespace distantLight
{
	struct Entry
	{
		// CPU structure								// GPU mapping

		Uint32				colorR : 8;					// uint
		Uint32				colorG : 8;					// 
		Uint32				colorB : 8;					// 
		Uint32				visibilityGroup : 8;		// 

		Float				intensity{0.0f};			// float

		Uint32				radius : 16;				// uint
		Uint32				fadeDistance : 16;			// 

		Uint32				flickeringScale : 8;		// uint
		Uint32				flickeringFrequency : 8;	// 
		Uint32				spotDirX : 8;				// 
		Uint32				spotDirY : 8;				// 

		Uint32				posX : 20;					// uint
		Uint32				posZ : 12;					// 

		Uint32				posY : 20;					// uint
		Uint32				spotDirZ : 1;				// 
		Uint32				useInverseSqrFalloff : 1;	// 
		Uint32				padding0 : 1;				// 
		Uint32				padding1 : 1;				// 
		Uint32				spotDirAngle : 8;			// 
	};

	static constexpr Uint32 DATA_SIZE = sizeof( Entry );

	static_assert(DATA_SIZE == 24, "Wronk sajz");

}

//*********************************************************************************************************************************************//
//*********************************************************************************************************************************************//

class RENDER_DATA_API CDistantLightsResource : public res::StreamedResource
{
	RTTI_DECLARE_TYPE( CDistantLightsResource );
	IMPLEMENT_RESOURCE_INTERFACE( "lights", "lights", "Distant Lights Resource" );
	RED_BASE_CLASS( res::StreamedResource );
	RED_USE_MEMORY_POOL( rend::PoolLighting );

	DataBuffer					m_data;

public:

	CDistantLightsResource();

	~CDistantLightsResource();

	// - - - - - - - - - - - - - - - - - - - - - - - 

	inline Uint32					GetNumLights() const { return m_data.Size() / sizeof( distantLight::Entry ); }

	inline const DataBuffer&		GetBuffer() const { return m_data; }

	// - - - - - - - - - - - - - - - - - - - - - - - 

	static THandle< CDistantLightsResource >	Create( red::ArraySpan< distantLight::Entry > lights );

	// - - - - - - - - - - - - - - - - - - - - - - - 

};

//*********************************************************************************************************************************************//
//*********************************************************************************************************************************************//

#endif
